; ModuleID = 'flame'
source_filename = "flame"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%Box = type { i32 }

@str = private unnamed_addr constant [19 x i8] c"Box(%d) destroyed\0A\00", align 1
@str.1 = private unnamed_addr constant [20 x i8] c"=== loop tests ===\0A\00", align 1
@str.2 = private unnamed_addr constant [19 x i8] c"for sum 0..4 = %d\0A\00", align 1
@str.3 = private unnamed_addr constant [18 x i8] c"do-while: n = %d\0A\00", align 1
@str.4 = private unnamed_addr constant [25 x i8] c"--- autodel in loop ---\0A\00", align 1
@str.5 = private unnamed_addr constant [17 x i8] c"created Box(%d)\0A\00", align 1
@str.6 = private unnamed_addr constant [12 x i8] c"after loop\0A\00", align 1

declare ptr @malloc(i64)

declare void @free(ptr)

declare i32 @printf(ptr)

define void @Box_new(ptr %0, i32 %1) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %v = alloca i32, align 4
  store i32 %1, ptr %v, align 4
  %ptr = load ptr, ptr %self, align 8
  %gep = getelementptr %Box, ptr %ptr, i32 0, i32 0
  %v1 = load i32, ptr %v, align 4
  store i32 %v1, ptr %gep, align 4
  ret void
}

define void @Box_delete(ptr %0) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %ptr = load ptr, ptr %self, align 8
  %gep = getelementptr %Box, ptr %ptr, i32 0, i32 0
  %val = load i32, ptr %gep, align 4
  %call = call i32 @printf(ptr @str, i32 %val)
  ret void
}

define i32 @Box_get(ptr %0) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %ptr = load ptr, ptr %self, align 8
  %gep = getelementptr %Box, ptr %ptr, i32 0, i32 0
  %val = load i32, ptr %gep, align 4
  ret i32 %val
}

define i32 @main() {
entry:
  %call = call i32 @printf(ptr @str.1)
  %sum = alloca i32, align 4
  store i32 0, ptr %sum, align 4
  %i = alloca i32, align 4
  store i32 0, ptr %i, align 4
  br label %fcond

fcond:                                            ; preds = %fstep, %entry
  %i1 = load i32, ptr %i, align 4
  %lt = icmp slt i32 %i1, 5
  %bool = zext i1 %lt to i32
  %fcond2 = icmp ne i32 %bool, 0
  br i1 %fcond2, label %fbody, label %fend

fbody:                                            ; preds = %fcond
  %sum3 = load i32, ptr %sum, align 4
  %i4 = load i32, ptr %i, align 4
  %add = add i32 %sum3, %i4
  store i32 %add, ptr %sum, align 4
  br label %fstep

fstep:                                            ; preds = %fbody
  %i5 = load i32, ptr %i, align 4
  %add6 = add i32 %i5, 1
  store i32 %add6, ptr %i, align 4
  br label %fcond

fend:                                             ; preds = %fcond
  %sum7 = load i32, ptr %sum, align 4
  %call8 = call i32 @printf(ptr @str.2, i32 %sum7)
  %n = alloca i32, align 4
  store i32 1, ptr %n, align 4
  br label %dobody

dobody:                                           ; preds = %docond, %fend
  %n9 = load i32, ptr %n, align 4
  %mul = mul i32 %n9, 2
  store i32 %mul, ptr %n, align 4
  br label %docond

docond:                                           ; preds = %dobody
  %n10 = load i32, ptr %n, align 4
  %lt11 = icmp slt i32 %n10, 100
  %bool12 = zext i1 %lt11 to i32
  %docond13 = icmp ne i32 %bool12, 0
  br i1 %docond13, label %dobody, label %doend

doend:                                            ; preds = %docond
  %n14 = load i32, ptr %n, align 4
  %call15 = call i32 @printf(ptr @str.3, i32 %n14)
  %call16 = call i32 @printf(ptr @str.4)
  %i17 = alloca i32, align 4
  store i32 0, ptr %i17, align 4
  br label %wcond

wcond:                                            ; preds = %wbody, %doend
  %i18 = load i32, ptr %i17, align 4
  %lt19 = icmp slt i32 %i18, 3
  %bool20 = zext i1 %lt19 to i32
  %wcond21 = icmp ne i32 %bool20, 0
  br i1 %wcond21, label %wbody, label %wend

wbody:                                            ; preds = %wcond
  %b = alloca ptr, align 8
  %newptr = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Box, ptr null, i32 1) to i64))
  store ptr %newptr, ptr %b, align 8
  %i22 = load i32, ptr %i17, align 4
  call void @Box_new(ptr %newptr, i32 %i22)
  %b23 = load ptr, ptr %b, align 8
  %call24 = call i32 @Box_get(ptr %b23)
  %call25 = call i32 @printf(ptr @str.5, i32 %call24)
  %delptr = load ptr, ptr %b, align 8
  call void @Box_delete(ptr %delptr)
  call void @free(ptr %delptr)
  %i26 = load i32, ptr %i17, align 4
  %add27 = add i32 %i26, 1
  store i32 %add27, ptr %i17, align 4
  br label %wcond

wend:                                             ; preds = %wcond
  %call28 = call i32 @printf(ptr @str.6)
  ret i32 0
}
