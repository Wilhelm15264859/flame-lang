; ModuleID = 'flame'
source_filename = "flame"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@str = private unnamed_addr constant [21 x i8] c"=== basic tests ===\0A\00", align 1
@str.1 = private unnamed_addr constant [15 x i8] c"add(3,4) = %d\0A\00", align 1
@str.2 = private unnamed_addr constant [20 x i8] c"factorial(10) = %d\0A\00", align 1
@str.3 = private unnamed_addr constant [20 x i8] c"fibonacci(10) = %d\0A\00", align 1

declare ptr @malloc(i64)

declare void @free(ptr)

declare i32 @printf(ptr)

declare i32 @scanf(ptr, ptr)

define i32 @add(i32 %0, i32 %1) {
entry:
  %a = alloca i32, align 4
  store i32 %0, ptr %a, align 4
  %b = alloca i32, align 4
  store i32 %1, ptr %b, align 4
  %a1 = load i32, ptr %a, align 4
  %b2 = load i32, ptr %b, align 4
  %add = add i32 %a1, %b2
  ret i32 %add
}

define i32 @factorial(i32 %0) {
entry:
  %n = alloca i32, align 4
  store i32 %0, ptr %n, align 4
  %n1 = load i32, ptr %n, align 4
  %le = icmp sle i32 %n1, 1
  %bool = zext i1 %le to i32
  %ifcond = icmp ne i32 %bool, 0
  br i1 %ifcond, label %then, label %else

then:                                             ; preds = %entry
  ret i32 1

else:                                             ; preds = %entry
  br label %ifend

ifend:                                            ; preds = %else
  %n2 = load i32, ptr %n, align 4
  %n3 = load i32, ptr %n, align 4
  %sub = sub i32 %n3, 1
  %call = call i32 @factorial(i32 %sub)
  %mul = mul i32 %n2, %call
  ret i32 %mul
}

define i32 @fibonacci(i32 %0) {
entry:
  %n = alloca i32, align 4
  store i32 %0, ptr %n, align 4
  %n1 = load i32, ptr %n, align 4
  %le = icmp sle i32 %n1, 1
  %bool = zext i1 %le to i32
  %ifcond = icmp ne i32 %bool, 0
  br i1 %ifcond, label %then, label %else

then:                                             ; preds = %entry
  %n2 = load i32, ptr %n, align 4
  ret i32 %n2

else:                                             ; preds = %entry
  br label %ifend

ifend:                                            ; preds = %else
  %a = alloca i32, align 4
  store i32 0, ptr %a, align 4
  %b = alloca i32, align 4
  store i32 1, ptr %b, align 4
  %i = alloca i32, align 4
  store i32 2, ptr %i, align 4
  br label %wcond

wcond:                                            ; preds = %wbody, %ifend
  %i3 = load i32, ptr %i, align 4
  %n4 = load i32, ptr %n, align 4
  %le5 = icmp sle i32 %i3, %n4
  %bool6 = zext i1 %le5 to i32
  %wcond7 = icmp ne i32 %bool6, 0
  br i1 %wcond7, label %wbody, label %wend

wbody:                                            ; preds = %wcond
  %tmp = alloca i32, align 4
  %a8 = load i32, ptr %a, align 4
  %b9 = load i32, ptr %b, align 4
  %add = add i32 %a8, %b9
  store i32 %add, ptr %tmp, align 4
  %b10 = load i32, ptr %b, align 4
  store i32 %b10, ptr %a, align 4
  %tmp11 = load i32, ptr %tmp, align 4
  store i32 %tmp11, ptr %b, align 4
  %i12 = load i32, ptr %i, align 4
  %add13 = add i32 %i12, 1
  store i32 %add13, ptr %i, align 4
  br label %wcond

wend:                                             ; preds = %wcond
  %b14 = load i32, ptr %b, align 4
  ret i32 %b14
}

define i32 @main() {
entry:
  %call = call i32 @printf(ptr @str)
  %r = alloca i32, align 4
  %call1 = call i32 @add(i32 3, i32 4)
  store i32 %call1, ptr %r, align 4
  %r2 = load i32, ptr %r, align 4
  %call3 = call i32 @printf(ptr @str.1, i32 %r2)
  %f = alloca i32, align 4
  %call4 = call i32 @factorial(i32 10)
  store i32 %call4, ptr %f, align 4
  %f5 = load i32, ptr %f, align 4
  %call6 = call i32 @printf(ptr @str.2, i32 %f5)
  %fib = alloca i32, align 4
  %call7 = call i32 @fibonacci(i32 10)
  store i32 %call7, ptr %fib, align 4
  %fib8 = load i32, ptr %fib, align 4
  %call9 = call i32 @printf(ptr @str.3, i32 %fib8)
  ret i32 0
}
