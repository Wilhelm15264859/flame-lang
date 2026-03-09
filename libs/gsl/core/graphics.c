#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <shaderc/shaderc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void err() {
    __asm__ __volatile__ (
        "syscall"
        :
        : "a"(60), "D"(1)
    );
}

VkInstance inst;
VkSurfaceKHR surf;
VkDevice device;
VkPhysicalDevice phDevice;
VkQueue gQueue;
VkSwapchainKHR swapChain;
uint32_t imageCount;
VkImage* swapChainImages;
VkImageView* swapChainImageViews;
VkFormat swapChainImageFormat;
VkExtent2D swapChainExtent;
VkRenderPass renderPass;
VkFramebuffer* swapChainFramebuffers;

VkCommandPool commandPool;
VkCommandBuffer* commandBuffers;
VkSemaphore imageAvailableSemaphore;
VkSemaphore renderFinishedSemaphore;
VkFence* inFlightFences;

VkPipeline graphicsPipeline;
VkPipelineLayout pipelineLayout;
VkBuffer vertexBuffer;
VkDeviceMemory vertexBufferMemory;
uint32_t vertexCount = 0;

uint32_t findMemoryType(VkPhysicalDevice phDev, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(phDev, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    return 0;
}

VkShaderModule compileShader(const char* source, shaderc_shader_kind kind) {
    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    shaderc_compilation_result_t result = shaderc_compile_into_spv(
        compiler, source, strlen(source), kind, "shader.glsl", "main", NULL);

    if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) {
        fprintf(stderr, "Shader Error: %s\n", shaderc_result_get_error_message(result));
        err();
    }

    VkShaderModuleCreateInfo cInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shaderc_result_get_length(result),
        .pCode = (const uint32_t*)shaderc_result_get_bytes(result)
    };
    VkShaderModule module;
    vkCreateShaderModule(device, &cInfo, NULL, &module);
    shaderc_result_release(result);
    shaderc_compiler_release(compiler);
    return module;
}

void graphicInit(SDL_Window* win) {
    uint32_t extensionCount = 0;
    SDL_Vulkan_GetInstanceExtensions(win, &extensionCount, NULL);
    const char **extensions = malloc(sizeof(const char*) * extensionCount);
    SDL_Vulkan_GetInstanceExtensions(win, &extensionCount, extensions);

    VkApplicationInfo ainfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Flame Engine",
        .apiVersion = VK_API_VERSION_1_0
    };

    VkInstanceCreateInfo cinfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &ainfo,
        .enabledExtensionCount = extensionCount,
        .ppEnabledExtensionNames = extensions
    };

    if (vkCreateInstance(&cinfo, NULL, &inst) != VK_SUCCESS) err();
    if (!SDL_Vulkan_CreateSurface(win, inst, &surf)) err();
    free(extensions);

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(inst, &deviceCount, NULL);
    VkPhysicalDevice *devices = malloc(sizeof(VkPhysicalDevice) * deviceCount);
    vkEnumeratePhysicalDevices(inst, &deviceCount, devices);
    phDevice = devices[0];

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phDevice, &queueFamilyCount, NULL);
    VkQueueFamilyProperties *queueFamilies = malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(phDevice, &queueFamilyCount, queueFamilies);

    int graphicsFamily = -1;
    for (int i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsFamily = i;
            break;
        }
    }

    float queuePrior = 1.0f;
    VkDeviceQueueCreateInfo qcInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphicsFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePrior
    };

    const char* deviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkPhysicalDeviceFeatures deviceFeatures = {};
    VkDeviceCreateInfo dCInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qcInfo,
        .pEnabledFeatures = &deviceFeatures,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = deviceExtensions
    };

    if (vkCreateDevice(phDevice, &dCInfo, NULL, &device) != VK_SUCCESS) err();
    vkGetDeviceQueue(device, graphicsFamily, 0, &gQueue);

    int width, height;
    SDL_Vulkan_GetDrawableSize(win, &width, &height);
    swapChainExtent = (VkExtent2D){(uint32_t)width, (uint32_t)height};
    swapChainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    VkSwapchainCreateInfoKHR swInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surf,
        .minImageCount = 2,
        .imageFormat = swapChainImageFormat,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = swapChainExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE
    };
    if (vkCreateSwapchainKHR(device, &swInfo, NULL, &swapChain) != VK_SUCCESS) err();

    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, NULL);
    swapChainImages = malloc(sizeof(VkImage) * imageCount);
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages);

    swapChainImageViews = malloc(sizeof(VkImageView) * imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo vInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapChainImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapChainImageFormat,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
        };
        vkCreateImageView(device, &vInfo, NULL, &swapChainImageViews[i]);
    }

    VkAttachmentDescription colorAttr = {
        .format = swapChainImageFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };

    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorRef
    };

    VkRenderPassCreateInfo rpInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &colorAttr,
        .subpassCount = 1, .pSubpasses = &sub
    };
    vkCreateRenderPass(device, &rpInfo, NULL, &renderPass);

    swapChainFramebuffers = malloc(sizeof(VkFramebuffer) * imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        VkImageView attach[] = { swapChainImageViews[i] };
        VkFramebufferCreateInfo fbInfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = renderPass,
            .attachmentCount = 1, .pAttachments = attach,
            .width = swapChainExtent.width, .height = swapChainExtent.height, .layers = 1
        };
        vkCreateFramebuffer(device, &fbInfo, NULL, &swapChainFramebuffers[i]);
    }

    VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = graphicsFamily};
    vkCreateCommandPool(device, &poolInfo, NULL, &commandPool);

    commandBuffers = malloc(sizeof(VkCommandBuffer) * imageCount);
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = imageCount
    };
    vkAllocateCommandBuffers(device, &allocInfo, commandBuffers);

    inFlightFences = malloc(sizeof(VkFence) * imageCount);
    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    for (uint32_t i = 0; i < imageCount; i++)
        vkCreateFence(device, &fenceInfo, NULL, &inFlightFences[i]);

    VkSemaphoreCreateInfo semInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(device, &semInfo, NULL, &imageAvailableSemaphore);
    vkCreateSemaphore(device, &semInfo, NULL, &renderFinishedSemaphore);

    free(devices);
    free(queueFamilies);

    const char* vertSrc =
        "#version 450\n"
        "layout(location = 0) in ivec2 inPos;\n"
        "layout(location = 1) in vec4 inColor;\n"
        "layout(location = 0) out vec4 fragColor;\n"
        "void main() {\n"
        "    gl_Position = vec4(vec2(inPos) / 400.0, 0.0, 1.0);\n"
        "    fragColor = inColor;\n"
        "}\n";

    const char* fragSrc =
        "#version 450\n"
        "layout(location = 0) in vec4 fragColor;\n"
        "layout(location = 0) out vec4 outColor;\n"
        "void main() { outColor = fragColor; }\n";

    VkShaderModule vertMod = compileShader(vertSrc, shaderc_glsl_vertex_shader);
    VkShaderModule fragMod = compileShader(fragSrc, shaderc_glsl_fragment_shader);

    VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertMod, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragMod, .pName = "main"}
    };

    VkVertexInputBindingDescription binding = { .binding = 0, .stride = 12, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription attrs[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SINT, .offset = 0 },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = 8 }
    };

    VkPipelineVertexInputStateCreateInfo vertexInput = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attrs
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };

    VkViewport viewport = { 0, 0, (float)swapChainExtent.width, (float)swapChainExtent.height, 0, 1 };
    VkRect2D scissor = { {0, 0}, swapChainExtent };
    VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .lineWidth = 1.0f, .cullMode = VK_CULL_MODE_NONE, .polygonMode = VK_POLYGON_MODE_FILL
    };

    VkPipelineMultisampleStateCreateInfo multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    VkPipelineColorBlendStateCreateInfo colorBlending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &colorBlendAttachment
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL, &pipelineLayout);

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly, .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer, .pMultisampleState = &multisampling,
        .pColorBlendState = &colorBlending, .layout = pipelineLayout,
        .renderPass = renderPass, .subpass = 0
    };

    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &graphicsPipeline);

    vkDestroyShaderModule(device, fragMod, NULL);
    vkDestroyShaderModule(device, vertMod, NULL);
}

void drawFrame() {
    uint32_t imageIndex;
    vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, 
        imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    vkWaitForFences(device, 1, &inFlightFences[imageIndex], VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFences[imageIndex]);

    vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    vkResetCommandBuffer(commandBuffers[imageIndex], 0);
    VkCommandBufferBeginInfo bInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(commandBuffers[imageIndex], &bInfo);

    VkClearValue clearColor = {{{0.1f, 0.1f, 0.1f, 1.0f}}};
    VkRenderPassBeginInfo rpBegin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = renderPass, .framebuffer = swapChainFramebuffers[imageIndex],
        .renderArea = {{0,0}, swapChainExtent}, .clearValueCount = 1, .pClearValues = &clearColor
    };

    vkCmdBeginRenderPass(commandBuffers[imageIndex], &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffers[imageIndex], 0, 1, vertexBuffers, offsets);

    vkCmdDraw(commandBuffers[imageIndex], vertexCount, 1, 0, 0);

    vkCmdEndRenderPass(commandBuffers[imageIndex]);
    vkEndCommandBuffer(commandBuffers[imageIndex]);

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &imageAvailableSemaphore,
        .pWaitDstStageMask = waitStages, .commandBufferCount = 1, .pCommandBuffers = &commandBuffers[imageIndex],
        .signalSemaphoreCount = 1, .pSignalSemaphores = &renderFinishedSemaphore
    };
    vkQueueSubmit(gQueue, 1, &submitInfo, inFlightFences[imageIndex]);

    VkPresentInfoKHR presInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &renderFinishedSemaphore,
        .swapchainCount = 1, .pSwapchains = &swapChain, .pImageIndices = &imageIndex
    };
    vkQueuePresentKHR(gQueue, &presInfo);
}

void createVertexBuffer(void* vertices, uint32_t size) {
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    if (vkCreateBuffer(device, &bufferInfo, NULL, &vertexBuffer) != VK_SUCCESS) { 
        fprintf(stderr, "vkCreateBuffer failed\n"); 
        err(); 
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, vertexBuffer, &memReqs);

    uint32_t memType = findMemoryType(phDevice, memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = memType
    };
    if (vkAllocateMemory(device, &allocInfo, NULL, &vertexBufferMemory) != VK_SUCCESS) { 
        fprintf(stderr, "vkAllocateMemory failed\n"); 
        err(); 
    }
    if (vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0) != VK_SUCCESS) { 
        fprintf(stderr, "vkBindBufferMemory failed\n"); 
        err(); 
    }

    void* data = NULL;
    if (vkMapMemory(device, vertexBufferMemory, 0, size, 0, &data) != VK_SUCCESS) { 
        fprintf(stderr, "vkMapMemory failed\n"); 
        err(); 
    }
    if (!data) { 
        fprintf(stderr, "data is NULL\n"); 
        err(); 
    }

    fprintf(stderr, "Copying %u bytes from %p to GPU\n", size, vertices);
    memcpy(data, vertices, size);
    vkUnmapMemory(device, vertexBufferMemory);

    vertexCount = size / 12;
    fprintf(stderr, "Created vertex buffer with %u vertices\n", vertexCount);
}

void destroyVertexBuffer() {
    if (vertexBuffer != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        vkDestroyBuffer(device, vertexBuffer, NULL);
        vertexBuffer = VK_NULL_HANDLE;
    }
    if (vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, vertexBufferMemory, NULL);
        vertexBufferMemory = VK_NULL_HANDLE;
    }
    vertexCount = 0;
}

void *openWindow(const char *title, int w, int h) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) err();
    return (void*)SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
}

void closeWindow(SDL_Window* win) {
    vkDeviceWaitIdle(device);
    SDL_DestroyWindow(win);
    SDL_Quit();
}

int appEvent() {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            return 1;
        }
    }

    return 0;
}