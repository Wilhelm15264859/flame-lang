#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <vulkan/vulkan.h>
#include <shaderc/shaderc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_GEOM_VERTS 65536

void err() {
    __asm__ __volatile__ (
        "syscall"
        :
        : "a"(60), "D"(1)
    );
}





static uint8_t geom_verts[MAX_GEOM_VERTS * 12];
static uint32_t geom_vert_count = 0;

static VkBuffer       geom_vbuf        = VK_NULL_HANDLE;
static VkDeviceMemory geom_vbuf_memory = VK_NULL_HANDLE;
static uint32_t       geom_vbuf_size   = 0;

float clearR = 0.1f, clearG = 0.1f, clearB = 0.1f, clearA = 1.0f;
int width;
int height;

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





static float g_originAnchorX = 0.5f;
static float g_originAnchorY = 0.5f;

void setGlobalOrigin(float anchorX, float anchorY) {
    g_originAnchorX = anchorX;
    g_originAnchorY = anchorY;
}

void setClearColor(int r, int g, int b, int a) {
    clearR = r / 255.0f;
    clearG = g / 255.0f;
    clearB = b / 255.0f;
    clearA = a / 255.0f;
}

uint32_t findMemoryType(VkPhysicalDevice phDev, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(phDev, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    error("findMemoryType: no suitable memory type found\n");
    err();
    return 0;
}

VkShaderModule compileShader(const char* source, shaderc_shader_kind kind) {
    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    shaderc_compilation_result_t result = shaderc_compile_into_spv(
        compiler, source, strlen(source), kind, "shader.glsl", "main", NULL);

    if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) {
        error("Shader Error: %s\n", shaderc_result_get_error_message(result));
        err();
    }

    VkShaderModuleCreateInfo cInfo = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shaderc_result_get_length(result),
        .pCode    = (const uint32_t*)shaderc_result_get_bytes(result)
    };
    VkShaderModule module;
    vkCreateShaderModule(device, &cInfo, NULL, &module);
    shaderc_result_release(result);
    shaderc_compiler_release(compiler);
    return module;
}





#define MAX_GLYPHS      128
#define ATLAS_W         1024
#define ATLAS_H         1024
#define MAX_TEXT_VERTS  65536

typedef struct {
    int32_t  x, y;
    float    u, v;
    uint8_t  r, g, b, a;
} TextVertex;

typedef struct {
    int   codepoint;
    float u0, v0, u1, v1;
    int   bw, bh;
    int   bearing_x, bearing_y;
    int   advance;
} GlyphInfo;

typedef struct {
    TTF_Font       *font;
    int             loaded;
    int             atlas_x, atlas_y, row_h;
    GlyphInfo       glyphs[MAX_GLYPHS];
    int             glyph_count;
    VkImage         atlas_image;
    VkDeviceMemory  atlas_memory;
    VkImageView     atlas_view;
    VkSampler       atlas_sampler;
    uint8_t        *atlas_pixels;
    int             atlas_dirty;
    VkDescriptorSet descriptor_set;
} FontState;

static VkPipeline            text_pipeline        = VK_NULL_HANDLE;
static VkPipelineLayout      text_pipeline_layout = VK_NULL_HANDLE;
static VkDescriptorSetLayout text_dsl             = VK_NULL_HANDLE;
static VkDescriptorPool      text_desc_pool       = VK_NULL_HANDLE;

static FontState g_font;
static int       text_system_ready = 0;

static TextVertex  text_verts[MAX_TEXT_VERTS];
static uint32_t    text_vert_count = 0;

static VkBuffer       text_vbuf        = VK_NULL_HANDLE;
static VkDeviceMemory text_vbuf_memory = VK_NULL_HANDLE;
static uint32_t       text_vbuf_size   = 0;



static void text_create_image(uint32_t w, uint32_t h,
                               VkImage *img, VkDeviceMemory *mem)
{
    VkImageCreateInfo ii = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_R8G8B8A8_UNORM,
        .extent        = {w, h, 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_LINEAR,
        .usage         = VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
    };
    vkCreateImage(device, &ii, NULL, img);

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device, *img, &mr);

    VkMemoryAllocateInfo ai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = mr.size,
        .memoryTypeIndex = findMemoryType(phDevice, mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    vkAllocateMemory(device, &ai, NULL, mem);
    vkBindImageMemory(device, *img, *mem, 0);
}

static void text_transition_layout(VkImage img,
                                    VkImageLayout old_l,
                                    VkImageLayout new_l)
{
    VkCommandBuffer cb;
    VkCommandBufferAllocateInfo ai = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = commandPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(device, &ai, &cb);

    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cb, &bi);

    VkImageMemoryBarrier barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout           = old_l,
        .newLayout           = new_l,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = img,
        .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        .srcAccessMask       = VK_ACCESS_HOST_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
    };
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);

    vkEndCommandBuffer(cb);

    VkSubmitInfo si = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &cb,
    };
    vkQueueSubmit(gQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(gQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &cb);
}

static void upload_atlas(FontState *fs)
{
    void *mapped;
    VkImageSubresource sub = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(device, fs->atlas_image, &sub, &layout);
    vkMapMemory(device, fs->atlas_memory, 0, VK_WHOLE_SIZE, 0, &mapped);

    uint8_t *dst = (uint8_t *)mapped + layout.offset;
    for (int row = 0; row < ATLAS_H; row++) {
        memcpy(dst + row * layout.rowPitch,
               fs->atlas_pixels + row * ATLAS_W * 4,
               ATLAS_W * 4);
    }
    vkUnmapMemory(device, fs->atlas_memory);

    text_transition_layout(fs->atlas_image,
        VK_IMAGE_LAYOUT_PREINITIALIZED,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    fs->atlas_dirty = 0;
}

static void build_text_pipeline(void)
{
    VkDescriptorSetLayoutBinding dslb = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo dsli = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &dslb,
    };
    vkCreateDescriptorSetLayout(device, &dsli, NULL, &text_dsl);

    VkDescriptorPoolSize pool_size = {
        .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 4,
    };
    VkDescriptorPoolCreateInfo dpi = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = 4,
        .poolSizeCount = 1,
        .pPoolSizes    = &pool_size,
    };
    vkCreateDescriptorPool(device, &dpi, NULL, &text_desc_pool);

    VkPipelineLayoutCreateInfo pli = {
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts    = &text_dsl,
    };
    vkCreatePipelineLayout(device, &pli, NULL, &text_pipeline_layout);

    char vert_src[512];
    snprintf(vert_src, sizeof(vert_src),
        "#version 450\n"
        "layout(location=0) in ivec2 inPos;\n"
        "layout(location=1) in vec2  inUV;\n"
        "layout(location=2) in vec4  inColor;\n"
        "layout(location=0) out vec2 fragUV;\n"
        "layout(location=1) out vec4 fragColor;\n"
        "void main() {\n"
        "    float nx = float(inPos.x) / %f - 1.0;\n"
        "    float ny = float(inPos.y) / %f - 1.0;\n"
        "    gl_Position = vec4(nx, ny, 0.0, 1.0);\n"
        "    fragUV    = inUV;\n"
        "    fragColor = inColor;\n"
        "}\n",
        width / 2.0f, height / 2.0f
    );

    const char *frag_src =
        "#version 450\n"
        "layout(location=0) in vec2 fragUV;\n"
        "layout(location=1) in vec4 fragColor;\n"
        "layout(set=0, binding=0) uniform sampler2D fontAtlas;\n"
        "layout(location=0) out vec4 outColor;\n"
        "void main() {\n"
        "    float alpha = texture(fontAtlas, fragUV).r;\n"
        "    outColor = vec4(fragColor.rgb, fragColor.a * alpha);\n"
        "}\n";

    VkShaderModule vert_mod = compileShader(vert_src, shaderc_glsl_vertex_shader);
    VkShaderModule frag_mod = compileShader(frag_src, shaderc_glsl_fragment_shader);

    VkPipelineShaderStageCreateInfo stages[] = {
        {.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage  = VK_SHADER_STAGE_VERTEX_BIT,   .module = vert_mod, .pName = "main"},
        {.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage  = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_mod, .pName = "main"},
    };

    VkVertexInputBindingDescription binding = {
        .binding   = 0,
        .stride    = sizeof(TextVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attrs[] = {
        {.location=0, .binding=0, .format=VK_FORMAT_R32G32_SINT,    .offset=0},
        {.location=1, .binding=0, .format=VK_FORMAT_R32G32_SFLOAT,  .offset=8},
        {.location=2, .binding=0, .format=VK_FORMAT_R8G8B8A8_UNORM, .offset=16},
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1, .pVertexBindingDescriptions   = &binding,
        .vertexAttributeDescriptionCount = 3, .pVertexAttributeDescriptions = attrs,
    };
    VkPipelineInputAssemblyStateCreateInfo input_asm = {
        .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkViewport vp = {0, 0,
        (float)swapChainExtent.width, (float)swapChainExtent.height, 0, 1};
    VkRect2D sc = {{0,0}, swapChainExtent};
    VkPipelineViewportStateCreateInfo vp_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &vp,
        .scissorCount  = 1, .pScissors  = &sc,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .lineWidth   = 1.0f,
        .cullMode    = VK_CULL_MODE_NONE,
        .polygonMode = VK_POLYGON_MODE_FILL,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType               = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState blend_att = {
        .blendEnable         = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
        .colorWriteMask      =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend_att,
    };
    VkGraphicsPipelineCreateInfo pipe_info = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = 2,           .pStages             = stages,
        .pVertexInputState   = &vertex_input,
        .pInputAssemblyState = &input_asm,
        .pViewportState      = &vp_state,
        .pRasterizationState = &raster,
        .pMultisampleState   = &ms,
        .pColorBlendState    = &blend,
        .layout              = text_pipeline_layout,
        .renderPass          = renderPass,
        .subpass             = 0,
    };
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipe_info, NULL, &text_pipeline);

    vkDestroyShaderModule(device, vert_mod, NULL);
    vkDestroyShaderModule(device, frag_mod, NULL);
}

static GlyphInfo *get_or_cache_glyph(FontState *fs, int cp)
{
    for (int i = 0; i < fs->glyph_count; i++)
        if (fs->glyphs[i].codepoint == cp)
            return &fs->glyphs[i];

    if (fs->glyph_count >= MAX_GLYPHS) return NULL;

    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface *surf_raw = TTF_RenderGlyph_Blended(fs->font, (Uint32)cp, white);
    if (!surf_raw) return NULL;

    SDL_Surface *conv = SDL_ConvertSurface(surf_raw, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surf_raw);
    if (!conv) return NULL;

    int bw = conv->w, bh = conv->h;

    if (fs->atlas_x + bw > ATLAS_W) {
        fs->atlas_x = 0;
        fs->atlas_y += fs->row_h;
        fs->row_h   = 0;
    }
    if (fs->atlas_y + bh > ATLAS_H) { SDL_DestroySurface(conv); return NULL; }

    SDL_LockSurface(conv);
    uint8_t *src = (uint8_t *)conv->pixels;
    for (int row = 0; row < bh; row++) {
        for (int col = 0; col < bw; col++) {
            int di = ((fs->atlas_y + row) * ATLAS_W + (fs->atlas_x + col)) * 4;
            int si = row * conv->pitch + col * 4;
            uint8_t alpha = src[si + 3];
            fs->atlas_pixels[di+0] = alpha;
            fs->atlas_pixels[di+1] = alpha;
            fs->atlas_pixels[di+2] = alpha;
            fs->atlas_pixels[di+3] = alpha;
        }
    }
    SDL_UnlockSurface(conv);
    SDL_DestroySurface(conv);

    int minx, maxx, miny, maxy, advance;
    TTF_GetGlyphMetrics(fs->font, (Uint32)cp, &minx, &maxx, &miny, &maxy, &advance);

    GlyphInfo *gi  = &fs->glyphs[fs->glyph_count++];
    gi->codepoint  = cp;
    gi->u0         = (float)fs->atlas_x        / ATLAS_W;
    gi->v0         = (float)fs->atlas_y        / ATLAS_H;
    gi->u1         = (float)(fs->atlas_x + bw) / ATLAS_W;
    gi->v1         = (float)(fs->atlas_y + bh) / ATLAS_H;
    gi->bw         = bw;
    gi->bh         = bh;
    gi->bearing_x  = minx;
    gi->bearing_y  = maxy;
    gi->advance    = advance;

    fs->atlas_x   += bw + 1;
    if (bh > fs->row_h) fs->row_h = bh;
    fs->atlas_dirty = 1;

    return gi;
}





void initTextSystem(const char *font_path, int font_size)
{
    if (text_system_ready) return;

    if (!TTF_Init()) {
        error("TTF_Init failed: %s\n", SDL_GetError());
        return;
    }

    g_font.font = TTF_OpenFont(font_path, (float)font_size);
    if (!g_font.font) {
        error("TTF_OpenFont('%s') failed: %s\n", font_path, SDL_GetError());
        return;
    }

    g_font.loaded       = 1;
    g_font.atlas_x      = 0;
    g_font.atlas_y      = 0;
    g_font.row_h        = 0;
    g_font.glyph_count  = 0;
    g_font.atlas_dirty  = 0;
    g_font.atlas_pixels = calloc(ATLAS_W * ATLAS_H * 4, 1);

    text_create_image(ATLAS_W, ATLAS_H, &g_font.atlas_image, &g_font.atlas_memory);

    VkImageViewCreateInfo vi = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = g_font.atlas_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCreateImageView(device, &vi, NULL, &g_font.atlas_view);

    VkSamplerCreateInfo si = {
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = VK_FILTER_LINEAR,
        .minFilter    = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
    };
    vkCreateSampler(device, &si, NULL, &g_font.atlas_sampler);

    build_text_pipeline();

    VkDescriptorSetAllocateInfo dsai = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = text_desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &text_dsl,
    };
    vkAllocateDescriptorSets(device, &dsai, &g_font.descriptor_set);

    VkDescriptorImageInfo dii = {
        .sampler     = g_font.atlas_sampler,
        .imageView   = g_font.atlas_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet wds = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = g_font.descriptor_set,
        .dstBinding      = 0,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo      = &dii,
    };
    vkUpdateDescriptorSets(device, 1, &wds, 0, NULL);

    text_system_ready = 1;
    error("Text system ready (font '%s', size %d)\n", font_path, font_size);
}

void shutdownTextSystem(void)
{
    if (!text_system_ready) return;
    vkDeviceWaitIdle(device);

    if (text_vbuf        != VK_NULL_HANDLE) vkDestroyBuffer(device, text_vbuf, NULL);
    if (text_vbuf_memory != VK_NULL_HANDLE) vkFreeMemory(device, text_vbuf_memory, NULL);

    vkDestroyPipeline(device, text_pipeline, NULL);
    vkDestroyPipelineLayout(device, text_pipeline_layout, NULL);
    vkDestroyDescriptorPool(device, text_desc_pool, NULL);
    vkDestroyDescriptorSetLayout(device, text_dsl, NULL);

    vkDestroySampler(device, g_font.atlas_sampler, NULL);
    vkDestroyImageView(device, g_font.atlas_view, NULL);
    vkDestroyImage(device, g_font.atlas_image, NULL);
    vkFreeMemory(device, g_font.atlas_memory, NULL);

    free(g_font.atlas_pixels);
    TTF_CloseFont(g_font.font);
    TTF_Quit();

    text_system_ready = 0;
}

void measureText(const char *txt, int *out_w, int *out_h)
{
    if (!text_system_ready || !txt) { *out_w = 0; *out_h = 0; return; }
    TTF_GetStringSize(g_font.font, txt, 0, out_w, out_h);
}

void queueText(const char *txt, int x, int y, int r, int g, int b, int a)
{
    if (!text_system_ready || !txt || !g_font.loaded) return;

    int pen_x    = x;
    int baseline = y + TTF_GetFontAscent(g_font.font);

    for (const char *p = txt; *p; p++) {
        int cp = (unsigned char)*p;
        GlyphInfo *gi = get_or_cache_glyph(&g_font, cp);
        if (!gi) continue;

        int gx0 = pen_x + gi->bearing_x;
        int gy0 = baseline - gi->bearing_y;
        int gx1 = gx0 + gi->bw;
        int gy1 = gy0 + gi->bh;

        if (text_vert_count + 6 > MAX_TEXT_VERTS) break;

        TextVertex verts[6] = {
            {gx0, gy0, gi->u0, gi->v0, r, g, b, a},
            {gx1, gy0, gi->u1, gi->v0, r, g, b, a},
            {gx1, gy1, gi->u1, gi->v1, r, g, b, a},
            {gx0, gy0, gi->u0, gi->v0, r, g, b, a},
            {gx1, gy1, gi->u1, gi->v1, r, g, b, a},
            {gx0, gy1, gi->u0, gi->v1, r, g, b, a},
        };
        memcpy(text_verts + text_vert_count, verts, sizeof(verts));
        text_vert_count += 6;

        pen_x += gi->advance;
    }

    if (g_font.atlas_dirty)
        upload_atlas(&g_font);
}

static void flush_text(VkCommandBuffer cmd)
{
    if (!text_system_ready || text_vert_count == 0) return;

    uint32_t needed = text_vert_count * sizeof(TextVertex);

    if (needed > text_vbuf_size) {
        if (text_vbuf        != VK_NULL_HANDLE) vkDestroyBuffer(device, text_vbuf, NULL);
        if (text_vbuf_memory != VK_NULL_HANDLE) vkFreeMemory(device, text_vbuf_memory, NULL);

        VkBufferCreateInfo bi = {
            .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size        = needed,
            .usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        vkCreateBuffer(device, &bi, NULL, &text_vbuf);

        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(device, text_vbuf, &mr);

        VkMemoryAllocateInfo ai = {
            .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize  = mr.size,
            .memoryTypeIndex = findMemoryType(phDevice, mr.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        };
        vkAllocateMemory(device, &ai, NULL, &text_vbuf_memory);
        vkBindBufferMemory(device, text_vbuf, text_vbuf_memory, 0);
        text_vbuf_size = needed;
    }

    void *mapped;
    vkMapMemory(device, text_vbuf_memory, 0, needed, 0, &mapped);
    memcpy(mapped, text_verts, needed);
    vkUnmapMemory(device, text_vbuf_memory);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, text_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        text_pipeline_layout, 0, 1, &g_font.descriptor_set, 0, NULL);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &text_vbuf, &offset);
    vkCmdDraw(cmd, text_vert_count, 1, 0, 0);

    text_vert_count = 0;
}





void graphicInit(SDL_Window* win) {
    uint32_t extensionCount = 0;
    const char * const *sdl_exts = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    if (!sdl_exts) {
        error("SDL_Vulkan_GetInstanceExtensions failed: %s\n", SDL_GetError());
        err();
    }

    VkApplicationInfo ainfo = {
        .sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Flame Engine",
        .apiVersion       = VK_API_VERSION_1_0
    };
    VkInstanceCreateInfo cinfo = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo        = &ainfo,
        .enabledExtensionCount   = extensionCount,
        .ppEnabledExtensionNames = sdl_exts
    };

    VkResult res;
    res = vkCreateInstance(&cinfo, NULL, &inst);
    if (res != VK_SUCCESS) {
        error("vkCreateInstance failed: %d\n", res);
        err();
    }

    if (!SDL_Vulkan_CreateSurface(win, inst, NULL, &surf)) {
        error("SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        err();
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(inst, &deviceCount, NULL);
    if (deviceCount == 0) {
        error("No Vulkan physical devices found\n");
        err();
    }
    VkPhysicalDevice *devices = malloc(sizeof(VkPhysicalDevice) * deviceCount);
    vkEnumeratePhysicalDevices(inst, &deviceCount, devices);
    phDevice = devices[0];
    free(devices);

    
    VkPhysicalDeviceProperties devProps;
    vkGetPhysicalDeviceProperties(phDevice, &devProps);
    error("Using GPU: %s\n", devProps.deviceName);

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phDevice, &queueFamilyCount, NULL);
    VkQueueFamilyProperties *queueFamilies = malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(phDevice, &queueFamilyCount, queueFamilies);

    int graphicsFamily = -1;
    for (int i = 0; i < (int)queueFamilyCount; i++) {
        
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(phDevice, i, surf, &presentSupport);
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
            graphicsFamily = i;
            break;
        }
    }
    free(queueFamilies);

    if (graphicsFamily < 0) {
        error("No suitable queue family found\n");
        err();
    }
    error("Queue family index: %d\n", graphicsFamily);

    float queuePrior = 1.0f;
    VkDeviceQueueCreateInfo qcInfo = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphicsFamily,
        .queueCount       = 1,
        .pQueuePriorities = &queuePrior
    };

    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkPhysicalDeviceFeatures deviceFeatures = {0};
    VkDeviceCreateInfo dCInfo = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &qcInfo,
        .pEnabledFeatures        = &deviceFeatures,
        .enabledExtensionCount   = 1,
        .ppEnabledExtensionNames = deviceExtensions
    };

    res = vkCreateDevice(phDevice, &dCInfo, NULL, &device);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vkCreateDevice failed: %d\n", res);
        err();
    }
    vkGetDeviceQueue(device, graphicsFamily, 0, &gQueue);

    
    SDL_GetWindowSizeInPixels(win, &width, &height);
    fprintf(stderr, "Window size: %dx%d\n", width, height);

    
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phDevice, surf, &caps);

    
    if (caps.currentExtent.width != UINT32_MAX) {
        swapChainExtent = caps.currentExtent;
    } else {
        swapChainExtent.width  = (uint32_t)width;
        swapChainExtent.height = (uint32_t)height;
        if (swapChainExtent.width  < caps.minImageExtent.width)  swapChainExtent.width  = caps.minImageExtent.width;
        if (swapChainExtent.width  > caps.maxImageExtent.width)  swapChainExtent.width  = caps.maxImageExtent.width;
        if (swapChainExtent.height < caps.minImageExtent.height) swapChainExtent.height = caps.minImageExtent.height;
        if (swapChainExtent.height > caps.maxImageExtent.height) swapChainExtent.height = caps.maxImageExtent.height;
    }
    fprintf(stderr, "Swapchain extent: %dx%d\n", swapChainExtent.width, swapChainExtent.height);

    
    uint32_t minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && minImageCount > caps.maxImageCount)
        minImageCount = caps.maxImageCount;

    
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phDevice, surf, &formatCount, NULL);
    if (formatCount == 0) {
        fprintf(stderr, "No surface formats supported\n");
        err();
    }
    VkSurfaceFormatKHR *formats = malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phDevice, surf, &formatCount, formats);

    
    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (uint32_t i = 0; i < formatCount; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = formats[i];
            break;
        }
    }
    free(formats);
    swapChainImageFormat = chosenFormat.format;
    fprintf(stderr, "Surface format: %d colorspace: %d\n",
            chosenFormat.format, chosenFormat.colorSpace);

    
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phDevice, surf, &presentModeCount, NULL);
    VkPresentModeKHR *presentModes = malloc(sizeof(VkPresentModeKHR) * presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(phDevice, surf, &presentModeCount, presentModes);
    
    VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    free(presentModes);
    fprintf(stderr, "Present mode: %d\n", chosenPresentMode);

    VkSwapchainCreateInfoKHR swInfo = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = surf,
        .minImageCount    = minImageCount,
        .imageFormat      = swapChainImageFormat,
        .imageColorSpace  = chosenFormat.colorSpace,
        .imageExtent      = swapChainExtent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = caps.currentTransform,  
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = chosenPresentMode,
        .clipped          = VK_TRUE,
        .oldSwapchain     = VK_NULL_HANDLE
    };
    res = vkCreateSwapchainKHR(device, &swInfo, NULL, &swapChain);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vkCreateSwapchainKHR failed: %d\n", res);
        err();
    }
    fprintf(stderr, "Swapchain created OK\n");

    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, NULL);
    swapChainImages = malloc(sizeof(VkImage) * imageCount);
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages);
    fprintf(stderr, "Swapchain image count: %d\n", imageCount);

    swapChainImageViews = malloc(sizeof(VkImageView) * imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo vInfo = {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image            = swapChainImages[i],
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = swapChainImageFormat,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
        };
        vkCreateImageView(device, &vInfo, NULL, &swapChainImageViews[i]);
    }

    VkAttachmentDescription colorAttr = {
        .format        = swapChainImageFormat,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp       = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {
        .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &colorRef
    };
    
    VkSubpassDependency dependency = {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    VkRenderPassCreateInfo rpInfo = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &colorAttr,
        .subpassCount    = 1, .pSubpasses   = &sub,
        .dependencyCount = 1, .pDependencies = &dependency
    };
    res = vkCreateRenderPass(device, &rpInfo, NULL, &renderPass);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vkCreateRenderPass failed: %d\n", res);
        err();
    }

    swapChainFramebuffers = malloc(sizeof(VkFramebuffer) * imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        VkImageView attach[] = { swapChainImageViews[i] };
        VkFramebufferCreateInfo fbInfo = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = renderPass,
            .attachmentCount = 1, .pAttachments = attach,
            .width           = swapChainExtent.width,
            .height          = swapChainExtent.height,
            .layers          = 1
        };
        res = vkCreateFramebuffer(device, &fbInfo, NULL, &swapChainFramebuffers[i]);
        if (res != VK_SUCCESS) {
            fprintf(stderr, "vkCreateFramebuffer[%d] failed: %d\n", i, res);
            err();
        }
    }

    VkCommandPoolCreateInfo poolInfo = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphicsFamily
    };
    vkCreateCommandPool(device, &poolInfo, NULL, &commandPool);

    commandBuffers = malloc(sizeof(VkCommandBuffer) * imageCount);
    VkCommandBufferAllocateInfo allocInfo = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = commandPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = imageCount
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

    
    char vertSrc[512];
    snprintf(vertSrc, sizeof(vertSrc),
        "#version 450\n"
        "layout(location = 0) in ivec2 inPos;\n"
        "layout(location = 1) in vec4 inColor;\n"
        "layout(location = 0) out vec4 fragColor;\n"
        "void main() {\n"
        "    // Делим на половину ширины/высоты. Теперь 0,0 - центр.\n"
        "    float nx = float(inPos.x) / %f;\n"
        "    // Минус переворачивает ось Y, чтобы она смотрела ВВЕРХ\n"
        "    float ny = -float(inPos.y) / %f;\n"
        "    gl_Position = vec4(nx, ny, 0.0, 1.0);\n"
        "    fragColor = inColor;\n"
        "}\n",
        width / 2.0f, height / 2.0f
    );

    const char* fragSrc =
        "#version 450\n"
        "layout(location = 0) in vec4 fragColor;\n"
        "layout(location = 0) out vec4 outColor;\n"
        "void main() { outColor = fragColor; }\n";

    VkShaderModule vertMod = compileShader(vertSrc, shaderc_glsl_vertex_shader);
    VkShaderModule fragMod = compileShader(fragSrc, shaderc_glsl_fragment_shader);

    VkPipelineShaderStageCreateInfo stages[] = {
        {.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage  = VK_SHADER_STAGE_VERTEX_BIT,   .module = vertMod, .pName = "main"},
        {.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage  = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragMod, .pName = "main"}
    };
    VkVertexInputBindingDescription binding = {
        .binding = 0, .stride = 12, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
    VkVertexInputAttributeDescription attrs[] = {
        {.location=0, .binding=0, .format=VK_FORMAT_R32G32_SINT,    .offset=0},
        {.location=1, .binding=0, .format=VK_FORMAT_R8G8B8A8_UNORM, .offset=8}
    };
    VkPipelineVertexInputStateCreateInfo vertexInput = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1, .pVertexBindingDescriptions   = &binding,
        .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attrs
    };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };
    VkViewport viewport = {0, 0,
        (float)swapChainExtent.width, (float)swapChainExtent.height, 0, 1};
    VkRect2D scissor    = {{0,0}, swapChainExtent};
    VkPipelineViewportStateCreateInfo viewportState = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &viewport,
        .scissorCount  = 1, .pScissors  = &scissor
    };
    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .lineWidth   = 1.0f,
        .cullMode    = VK_CULL_MODE_NONE,
        .polygonMode = VK_POLYGON_MODE_FILL
    };
    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType               = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {
        .blendEnable         = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
        .colorWriteMask      =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    VkPipelineColorBlendStateCreateInfo colorBlending = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &colorBlendAttachment
    };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    };
    vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL, &pipelineLayout);

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = 2,             .pStages             = stages,
        .pVertexInputState   = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState      = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState   = &multisampling,
        .pColorBlendState    = &colorBlending,
        .layout              = pipelineLayout,
        .renderPass          = renderPass,
        .subpass             = 0
    };
    res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &graphicsPipeline);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vkCreateGraphicsPipelines failed: %d\n", res);
        err();
    }

    vkDestroyShaderModule(device, fragMod, NULL);
    vkDestroyShaderModule(device, vertMod, NULL);

    fprintf(stderr, "graphicInit complete\n");
}





static void flush_geometry(VkCommandBuffer cmd) {
    if (geom_vert_count == 0) return;

    uint32_t needed = geom_vert_count * 12;

    if (needed > geom_vbuf_size) {
        if (geom_vbuf != VK_NULL_HANDLE) vkDestroyBuffer(device, geom_vbuf, NULL);
        if (geom_vbuf_memory != VK_NULL_HANDLE) vkFreeMemory(device, geom_vbuf_memory, NULL);

        VkBufferCreateInfo bi = {
            .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size        = needed,
            .usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        vkCreateBuffer(device, &bi, NULL, &geom_vbuf);

        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(device, geom_vbuf, &mr);

        VkMemoryAllocateInfo ai = {
            .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize  = mr.size,
            .memoryTypeIndex = findMemoryType(phDevice, mr.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        };
        vkAllocateMemory(device, &ai, NULL, &geom_vbuf_memory);
        vkBindBufferMemory(device, geom_vbuf, geom_vbuf_memory, 0);
        geom_vbuf_size = needed;
    }

    void *mapped;
    vkMapMemory(device, geom_vbuf_memory, 0, needed, 0, &mapped);
    memcpy(mapped, geom_verts, needed);
    vkUnmapMemory(device, geom_vbuf_memory);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &geom_vbuf, &offset);
    vkCmdDraw(cmd, geom_vert_count, 1, 0, 0);

    geom_vert_count = 0;
}

void drawFrame() {
    fprintf(stderr, "drawFrame called: vertexCount=%u\n", vertexCount);
    uint32_t imageIndex;

    
    
    static uint32_t currentFrame = 0;

    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    
    VkResult res = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX,
        imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        fprintf(stderr, "Swapchain out of date, skipping frame\n");
        return;
    } else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
        fprintf(stderr, "vkAcquireNextImageKHR failed: %d\n", res);
        return;
    }

    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    vkResetCommandBuffer(commandBuffers[imageIndex], 0);
    VkCommandBufferBeginInfo bInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(commandBuffers[imageIndex], &bInfo);

    VkClearValue clearColor = {{{clearR, clearG, clearB, clearA}}};
    VkRenderPassBeginInfo rpBegin = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass      = renderPass,
        .framebuffer     = swapChainFramebuffers[imageIndex],
        .renderArea      = {{0,0}, swapChainExtent},
        .clearValueCount = 1, .pClearValues = &clearColor
    };

    vkCmdBeginRenderPass(commandBuffers[imageIndex], &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    
    flush_geometry(commandBuffers[imageIndex]);

    
    flush_text(commandBuffers[imageIndex]);

    vkCmdEndRenderPass(commandBuffers[imageIndex]);
    vkEndCommandBuffer(commandBuffers[imageIndex]);

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submitInfo = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1, .pWaitSemaphores   = &imageAvailableSemaphore,
        .pWaitDstStageMask    = waitStages,
        .commandBufferCount   = 1, .pCommandBuffers   = &commandBuffers[imageIndex],
        .signalSemaphoreCount = 1, .pSignalSemaphores = &renderFinishedSemaphore
    };
    vkQueueSubmit(gQueue, 1, &submitInfo, inFlightFences[currentFrame]);

    VkPresentInfoKHR presInfo = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &renderFinishedSemaphore,
        .swapchainCount     = 1, .pSwapchains     = &swapChain,
        .pImageIndices      = &imageIndex
    };
    vkQueuePresentKHR(gQueue, &presInfo);

    currentFrame = (currentFrame + 1) % imageCount;
}





void createVertexBuffer(void* vertices, uint32_t size) {
    fprintf(stderr, "createVertexBuffer called: size=%u\n", size);
    VkBufferCreateInfo bufferInfo = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    if (vkCreateBuffer(device, &bufferInfo, NULL, &vertexBuffer) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateBuffer failed\n"); err();
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, vertexBuffer, &memReqs);

    uint32_t memType = findMemoryType(phDevice, memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkMemoryAllocateInfo allocInfo = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memReqs.size,
        .memoryTypeIndex = memType
    };
    if (vkAllocateMemory(device, &allocInfo, NULL, &vertexBufferMemory) != VK_SUCCESS) {
        fprintf(stderr, "vkAllocateMemory failed\n"); err();
    }
    if (vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0) != VK_SUCCESS) {
        fprintf(stderr, "vkBindBufferMemory failed\n"); err();
    }

    void* data = NULL;
    if (vkMapMemory(device, vertexBufferMemory, 0, size, 0, &data) != VK_SUCCESS) {
        fprintf(stderr, "vkMapMemory failed\n"); err();
    }
    if (!data) { fprintf(stderr, "data is NULL\n"); err(); }

    memcpy(data, vertices, size);
    vkUnmapMemory(device, vertexBufferMemory);

    vertexCount = size / 12;
    fprintf(stderr, "Created vertex buffer with %u vertices\n", vertexCount);
}

void destroyVertexBuffer() {
    fprintf(stderr, "destroyVertexBuffer called\n");
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

SDL_Window *openWindow(const char *title, int w, int h) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        err();
    }
    width  = w;
    height = h;
    SDL_Window *win = SDL_CreateWindow(title, w, h, SDL_WINDOW_VULKAN);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        err();
    }
    graphicInit(win);
    
    drawFrame();
    return win;
}

void closeWindow(SDL_Window *win) {
    shutdownTextSystem();
    vkDeviceWaitIdle(device);
    SDL_DestroyWindow(win);
    SDL_Quit();
}

int appEvent() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            return 1;
        }
    }
    return 0;
}

static float squareTemplate[6][2] = {
    {-1,-1},{1,-1},{1,1}, {-1,-1},{1,1},{-1,1}
};

#define CIRCLE_SEGMENTS 32
static float circleTemplate[CIRCLE_SEGMENTS * 3][2];
static int   circleTemplateReady = 0;

static void buildCircleTemplate() {
    if (circleTemplateReady) return;
    for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
        float a0 = (float)i       / CIRCLE_SEGMENTS * 6.2831853f;
        float a1 = (i == CIRCLE_SEGMENTS - 1)
                   ? 0.0f
                   : (float)(i + 1) / CIRCLE_SEGMENTS * 6.2831853f;
        circleTemplate[i*3+0][0] = 0;        circleTemplate[i*3+0][1] = 0;
        circleTemplate[i*3+1][0] = cosf(a0); circleTemplate[i*3+1][1] = sinf(a0);
        circleTemplate[i*3+2][0] = cosf(a1); circleTemplate[i*3+2][1] = sinf(a1);
    }
    circleTemplateReady = 1;
}

static float triangleTemplate[3][2] = {{0,-1},{1,1},{-1,1}};

static float islandTemplate[27][2] = {
    {0,0},{-0.3f,-1.0f},{0.3f,-1.0f},
    {0,0},{0.3f,-1.0f},{1.0f,-0.2f},
    {0,0},{1.0f,-0.2f},{0.8f,0.5f},
    {0,0},{0.8f,0.5f},{0.3f,1.0f},
    {0,0},{0.3f,1.0f},{-0.3f,1.0f},
    {0,0},{-0.3f,1.0f},{-0.8f,0.5f},
    {0,0},{-0.8f,0.5f},{-1.0f,-0.2f},
    {0,0},{-1.0f,-0.2f},{-0.5f,-0.7f},
    {0,0},{-0.5f,-0.7f},{-0.3f,-1.0f}
};

static float rhombusTemplate[6][2] = {
    {0,-1},{1,0},{0,1}, {0,-1},{0,1},{-1,0}
};

static float pentagonTemplate[15][2];
static int   pentagonTemplateReady = 0;

static void buildPentagonTemplate() {
    if (pentagonTemplateReady) return;
    for (int i = 0; i < 5; i++) {
        float a0 = (float)i       / 5.0f * 6.2831853f - 1.5707963f;
        float a1 = (float)(i + 1) / 5.0f * 6.2831853f - 1.5707963f;
        pentagonTemplate[i*3+0][0] = 0;        pentagonTemplate[i*3+0][1] = 0;
        pentagonTemplate[i*3+1][0] = cosf(a0); pentagonTemplate[i*3+1][1] = sinf(a0);
        pentagonTemplate[i*3+2][0] = cosf(a1); pentagonTemplate[i*3+2][1] = sinf(a1);
    }
    pentagonTemplateReady = 1;
}

static int formVertexCounts[] = {6, CIRCLE_SEGMENTS*3, 3, 27, 6, 15};

int getFormVertexCount(int form) { return formVertexCounts[form]; }

void buildFormVertices(int form, float x, float y, float w, float h,
                       float anchorX, float anchorY, // <-- Вот они, индивидуальные якоря!
                       int r, int g, int b, int a, void *out)
{
    buildCircleTemplate();
    buildPentagonTemplate();

    float (*tmpl)[2];
    int count = formVertexCounts[form];
    switch (form) {
        case 0: tmpl = squareTemplate;   break;
        case 1: tmpl = circleTemplate;   break;
        case 2: tmpl = triangleTemplate; break;
        case 3: tmpl = islandTemplate;   break;
        case 4: tmpl = rhombusTemplate;  break;
        case 5: tmpl = pentagonTemplate; break;
        default: return;
    }

    unsigned char *ptr = (unsigned char *)out;
    
    // --- ИНДИВИДУАЛЬНЫЙ ЯКОРЬ ФИГУРЫ ---
    // x, y - это точка на экране, куда мы хотим "приколоть" фигуру.
    // anchorX, anchorY - это место на самой фигуре, за которое мы ее держим.
    
    float left = x - (w * anchorX);
    float top  = y - (h * anchorY); 

    // -----------------------------------

    // Истинный центр фигуры (нужен для правильного масштабирования шаблонов)
    float cx = left + (w * 0.5f);
    float cy = top  + (h * 0.5f);
    
    float rx = w * 0.5f;
    float ry = h * 0.5f;

    for (int i = 0; i < count; i++) {
        int vx = (int)(cx + tmpl[i][0] * rx);
        int vy = (int)(cy + tmpl[i][1] * ry);
        
        memcpy(ptr + i*12 + 0, &vx, 4);
        memcpy(ptr + i*12 + 4, &vy, 4);
        ptr[i*12+8]  = (unsigned char)r;
        ptr[i*12+9]  = (unsigned char)g;
        ptr[i*12+10] = (unsigned char)b;
        ptr[i*12+11] = (unsigned char)a;
    }
}

void queueForm(int form, float x, float y, float w, float h, 
               float anchorX, float anchorY, 
               int r, int g, int b, int a) {
                   
    int count = getFormVertexCount(form);
    
    if (geom_vert_count + count > MAX_GEOM_VERTS) {
        fprintf(stderr, "Внимание: превышен лимит вершин геометрии!\n");
        return; 
    }

    buildFormVertices(form, x, y, w, h, anchorX, anchorY, r, g, b, a, &geom_verts[geom_vert_count * 12]);
    
    geom_vert_count += count;
}

int main() {
    SDL_Window* win = openWindow("Моя игра", 800, 600);
    setClearColor(40, 40, 40, 255);

    int quit = 0;
    while (!quit) {
        quit = appEvent();

        // Точка на экране (0, 0) — это ровно центр твоего окна 800x600

        // Красный квадрат: прицепится к центру левым верхним углом 
        // (нарисуется в правой верхней четверти экрана)
        queueForm(0, 0, 0, 100, 100, 0.0f, 0.0f, 255, 0, 0, 255);

        // Синий квадрат: прицепится к центру правым нижним углом 
        // (нарисуется в левой нижней четверти экрана)
        queueForm(0, 0, 0, 100, 100, 1.0f, 1.0f, 0, 0, 255, 255);

        // Зеленый квадрат: встанет ровно по центру экрана
        queueForm(0, 0, 0, 100, 100, 0.5f, 0.5f, 0, 255, 0, 255);
            
        // Рисуем всё разом
        drawFrame();
    }

    closeWindow(win);
    return 0;
}