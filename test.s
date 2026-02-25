; ModuleID = 'test.fc'
source_filename = "flame"

declare ptr @malloc(i32)

declare void @free(ptr)

define i32 @maint() {
entry:
  %a = alloca ptr, align 8
  %call = call ptr @malloc(i32 4)
  store ptr %call, ptr %a, align 8
  %a1 = load ptr, ptr %a, align 8
  call void @free(ptr %a1)
  ret i32 0
}
