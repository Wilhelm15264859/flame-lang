; ModuleID = 'flame'
source_filename = "flame"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

declare ptr @malloc(i64)

declare void @free(ptr)

define void @_panic_() {
entry:
  ret void
}

define i32 @main() {
entry:
  %x = alloca i32, align 4
  store i32 10, ptr %x, align 4
  %y = alloca i32, align 4
  store i32 20, ptr %y, align 4
  %tmp = alloca i32, align 4
  %x1 = load i32, ptr %x, align 4
  store i32 %x1, ptr %tmp, align 4
  %y2 = load i32, ptr %y, align 4
  store i32 %y2, ptr %x, align 4
  %tmp3 = load i32, ptr %tmp, align 4
  store i32 %tmp3, ptr %y, align 4
  ret i32 0
}

define void @_test2_() {
entry:
  %_ex_idx_2 = alloca i32, align 4
  store i32 5, ptr %_ex_idx_2, align 4
  %_ex_idx_21 = load i32, ptr %_ex_idx_2, align 4
  %lt = icmp slt i32 %_ex_idx_21, 0
  %bool = zext i1 %lt to i32
  %ifcond = icmp ne i32 %bool, 0
  br i1 %ifcond, label %then, label %else

then:                                             ; preds = %entry
  call void @_panic_()
  br label %ifend

else:                                             ; preds = %entry
  br label %ifend

ifend:                                            ; preds = %else, %then
  %arr = alloca [5 x i32], align 4
  store [5 x i32] zeroinitializer, ptr %arr, align 4
  %gep = getelementptr [5 x i32], ptr %arr, i32 0, i32 3
  store i32 0, ptr %gep, align 4
  %gep2 = getelementptr [5 x i32], ptr %arr, i32 0, i32 1
  store i32 0, ptr %gep2, align 4
  ret void
}

define void @_test3_() {
entry:
  %_ex_val_3 = alloca i32, align 4
  store i32 99, ptr %_ex_val_3, align 4
  %myVar = alloca i32, align 4
  %_ex_val_31 = load i32, ptr %_ex_val_3, align 4
  store i32 %_ex_val_31, ptr %myVar, align 4
  ret void
}
