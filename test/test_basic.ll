; ModuleID = 'flame'
source_filename = "flame"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%First = type { ptr }

declare ptr @malloc(i64)

declare void @free(ptr)

define void @First_new(ptr %0) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  ret void
}

define i32 @main() {
entry:
  %obj = alloca ptr, align 8
  %newptr = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%First, ptr null, i32 1) to i64))
  store ptr %newptr, ptr %obj, align 8
  call void @First_new(ptr %newptr)
  %delptr = load ptr, ptr %obj, align 8
  call void @free(ptr %delptr)
  ret i32 0
}
