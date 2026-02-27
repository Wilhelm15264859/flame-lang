; ModuleID = 'flame'
source_filename = "flame"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%Cons = type {}

@str = private unnamed_addr constant [7 x i8] c"Hello\0A\00", align 1

declare ptr @malloc(i64)

declare void @free(ptr)

define void @Cons_out(ptr %0, ptr %1, i64 %2) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %msg = alloca ptr, align 8
  store ptr %1, ptr %msg, align 8
  %len = alloca i64, align 8
  store i64 %2, ptr %len, align 8
  call void asm sideeffect alignstack inteldialect "mov rax, 1", "~{rax},~{rbx},~{rcx},~{rdx},~{rsi},~{rdi},~{memory}"()
  call void asm sideeffect alignstack inteldialect "mov rdi, 1", "~{rax},~{rbx},~{rcx},~{rdx},~{rsi},~{rdi},~{memory}"()
  %msg1 = load ptr, ptr %msg, align 8
  call void asm sideeffect alignstack inteldialect "mov rsi, $0", "r,~{rax},~{rbx},~{rcx},~{rdx},~{rsi},~{rdi},~{memory}"(ptr %msg1)
  %len2 = load i64, ptr %len, align 8
  call void asm sideeffect alignstack inteldialect "mov rdx, $0", "r,~{rax},~{rbx},~{rcx},~{rdx},~{rsi},~{rdi},~{memory}"(i64 %len2)
  call void asm sideeffect alignstack inteldialect "syscall", "~{rax},~{rbx},~{rcx},~{rdx},~{rsi},~{rdi},~{memory}"()
  ret void
}

define i32 @main() {
entry:
  %Console = alloca ptr, align 8
  %newptr = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Cons, ptr null, i32 1) to i64))
  store ptr %newptr, ptr %Console, align 8
  %msg = alloca ptr, align 8
  store ptr @str, ptr %msg, align 8
  %Console1 = load ptr, ptr %Console, align 8
  %msg2 = load ptr, ptr %msg, align 8
  call void @Cons_out(ptr %Console1, ptr %msg2, i64 7)
  %Console3 = load ptr, ptr %Console, align 8
  call void @free(ptr %Console3)
  ret i32 0
}
