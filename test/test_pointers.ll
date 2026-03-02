; ModuleID = 'flame'
source_filename = "flame"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@str = private unnamed_addr constant [23 x i8] c"=== pointer tests ===\0A\00", align 1
@str.1 = private unnamed_addr constant [10 x i8] c"sum = %d\0A\00", align 1
@str.2 = private unnamed_addr constant [19 x i8] c"x after *p=99: %d\0A\00", align 1

declare ptr @malloc(i64)

declare void @free(ptr)

declare i32 @printf(ptr)

define i32 @sum_array(ptr %0, i32 %1) {
entry:
  %arr = alloca ptr, align 8
  store ptr %0, ptr %arr, align 8
  %n = alloca i32, align 4
  store i32 %1, ptr %n, align 4
  %s = alloca i32, align 4
  store i32 0, ptr %s, align 4
  %i = alloca i32, align 4
  store i32 0, ptr %i, align 4
  br label %wcond

wcond:                                            ; preds = %wbody, %entry
  %i1 = load i32, ptr %i, align 4
  %n2 = load i32, ptr %n, align 4
  %lt = icmp slt i32 %i1, %n2
  %bool = zext i1 %lt to i32
  %wcond3 = icmp ne i32 %bool, 0
  br i1 %wcond3, label %wbody, label %wend

wbody:                                            ; preds = %wcond
  %s4 = load i32, ptr %s, align 4
  %i5 = load i32, ptr %i, align 4
  %ptr = load ptr, ptr %arr, align 8
  %gep = getelementptr i32, ptr %ptr, i32 %i5
  %elem = load i32, ptr %gep, align 4
  %add = add i32 %s4, %elem
  store i32 %add, ptr %s, align 4
  %i6 = load i32, ptr %i, align 4
  %add7 = add i32 %i6, 1
  store i32 %add7, ptr %i, align 4
  br label %wcond

wend:                                             ; preds = %wcond
  %s8 = load i32, ptr %s, align 4
  ret i32 %s8
}

define i32 @main() {
entry:
  %call = call i32 @printf(ptr @str)
  %arr = alloca [5 x i32], align 4
  store [5 x i32] zeroinitializer, ptr %arr, align 4
  %gep = getelementptr [5 x i32], ptr %arr, i32 0, i32 0
  store i32 10, ptr %gep, align 4
  %gep1 = getelementptr [5 x i32], ptr %arr, i32 0, i32 1
  store i32 20, ptr %gep1, align 4
  %gep2 = getelementptr [5 x i32], ptr %arr, i32 0, i32 2
  store i32 30, ptr %gep2, align 4
  %gep3 = getelementptr [5 x i32], ptr %arr, i32 0, i32 3
  store i32 40, ptr %gep3, align 4
  %gep4 = getelementptr [5 x i32], ptr %arr, i32 0, i32 4
  store i32 50, ptr %gep4, align 4
  %s = alloca i32, align 4
  %elemptr = getelementptr [5 x i32], ptr %arr, i32 0, i32 0
  %call5 = call i32 @sum_array(ptr %elemptr, i32 5)
  store i32 %call5, ptr %s, align 4
  %s6 = load i32, ptr %s, align 4
  %call7 = call i32 @printf(ptr @str.1, i32 %s6)
  %x = alloca i32, align 4
  store i32 42, ptr %x, align 4
  %p = alloca ptr, align 8
  store ptr %x, ptr %p, align 8
  %ptr = load ptr, ptr %p, align 8
  store i32 99, ptr %ptr, align 4
  %x8 = load i32, ptr %x, align 4
  %call9 = call i32 @printf(ptr @str.2, i32 %x8)
  ret i32 0
}
