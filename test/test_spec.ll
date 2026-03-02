; ModuleID = 'flame'
source_filename = "flame"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%Box = type { i32 }

@str = private unnamed_addr constant [17 x i8] c"Box(%d) created\0A\00", align 1
@str.1 = private unnamed_addr constant [15 x i8] c"Box(%d) freed\0A\00", align 1
@str.2 = private unnamed_addr constant [24 x i8] c"=== safe_div_check ===\0A\00", align 1
@str.3 = private unnamed_addr constant [29 x i8] c"div_check: division by zero\0A\00", align 1
@str.4 = private unnamed_addr constant [11 x i8] c"10/2 = %d\0A\00", align 1
@str.5 = private unnamed_addr constant [29 x i8] c"div_check: division by zero\0A\00", align 1
@str.6 = private unnamed_addr constant [26 x i8] c"check triggered for zero\0A\00", align 1
@str.7 = private unnamed_addr constant [17 x i8] c"=== log_div ===\0A\00", align 1
@str.8 = private unnamed_addr constant [18 x i8] c"log_div: %d / %d\0A\00", align 1
@str.9 = private unnamed_addr constant [11 x i8] c"20/4 = %d\0A\00", align 1
@str.10 = private unnamed_addr constant [17 x i8] c"=== log_add ===\0A\00", align 1
@str.11 = private unnamed_addr constant [18 x i8] c"log_add: %d + %d\0A\00", align 1
@str.12 = private unnamed_addr constant [10 x i8] c"sum = %d\0A\00", align 1
@str.13 = private unnamed_addr constant [26 x i8] c"=== autodel explicit ===\0A\00", align 1
@str.14 = private unnamed_addr constant [9 x i8] c"b1 = %d\0A\00", align 1
@str.15 = private unnamed_addr constant [27 x i8] c"=== autodel ownership ===\0A\00", align 1
@str.16 = private unnamed_addr constant [9 x i8] c"b2 = %d\0A\00", align 1
@str.17 = private unnamed_addr constant [14 x i8] c"=== done ===\0A\00", align 1

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
  %ptr2 = load ptr, ptr %self, align 8
  %gep3 = getelementptr %Box, ptr %ptr2, i32 0, i32 0
  %val = load i32, ptr %gep3, align 4
  %call = call i32 @printf(ptr @str, i32 %val)
  ret void
}

define void @Box_delete(ptr %0) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %ptr = load ptr, ptr %self, align 8
  %gep = getelementptr %Box, ptr %ptr, i32 0, i32 0
  %val = load i32, ptr %gep, align 4
  %call = call i32 @printf(ptr @str.1, i32 %val)
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

define ptr @make_box(i32 %0) {
entry:
  %v = alloca i32, align 4
  store i32 %0, ptr %v, align 4
  %b = alloca ptr, align 8
  %newptr = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Box, ptr null, i32 1) to i64))
  store ptr %newptr, ptr %b, align 8
  %v1 = load i32, ptr %v, align 4
  call void @Box_new(ptr %newptr, i32 %v1)
  %b2 = load ptr, ptr %b, align 8
  ret ptr %b2
}

define i32 @main() {
entry:
  %call = call i32 @printf(ptr @str.2)
  %d_ok = alloca i32, align 4
  store i32 2, ptr %d_ok, align 4
  %d_zero = alloca i32, align 4
  store i32 0, ptr %d_zero, align 4
  %_ex_op_1 = alloca i32, align 4
  %d_ok1 = load i32, ptr %d_ok, align 4
  store i32 %d_ok1, ptr %_ex_op_1, align 4
  %_ex_op_12 = load i32, ptr %_ex_op_1, align 4
  %eq = icmp eq i32 %_ex_op_12, 0
  %bool = zext i1 %eq to i32
  %ifcond = icmp ne i32 %bool, 0
  br i1 %ifcond, label %then, label %else

then:                                             ; preds = %entry
  %msg = alloca ptr, align 8
  store ptr @str.3, ptr %msg, align 8
  %msg3 = load ptr, ptr %msg, align 8
  %call4 = call i32 @printf(ptr %msg3)
  br label %ifend

else:                                             ; preds = %entry
  br label %ifend

ifend:                                            ; preds = %else, %then
  %r1 = alloca i32, align 4
  %d_ok5 = load i32, ptr %d_ok, align 4
  %div = sdiv i32 10, %d_ok5
  store i32 %div, ptr %r1, align 4
  %r16 = load i32, ptr %r1, align 4
  %call7 = call i32 @printf(ptr @str.4, i32 %r16)
  %d_zero8 = load i32, ptr %d_zero, align 4
  %eq9 = icmp eq i32 %d_zero8, 0
  %bool10 = zext i1 %eq9 to i32
  %ifcond11 = icmp ne i32 %bool10, 0
  br i1 %ifcond11, label %then12, label %else13

then12:                                           ; preds = %ifend
  %_ex_op_2 = alloca i32, align 4
  %d_ok15 = load i32, ptr %d_ok, align 4
  store i32 %d_ok15, ptr %_ex_op_2, align 4
  %_ex_op_216 = load i32, ptr %_ex_op_2, align 4
  %eq17 = icmp eq i32 %_ex_op_216, 0
  %bool18 = zext i1 %eq17 to i32
  %ifcond19 = icmp ne i32 %bool18, 0
  br i1 %ifcond19, label %then20, label %else21

else13:                                           ; preds = %ifend
  br label %ifend14

ifend14:                                          ; preds = %else13, %ifend22
  %call29 = call i32 @printf(ptr @str.7)
  %num = alloca i32, align 4
  store i32 20, ptr %num, align 4
  %den = alloca i32, align 4
  store i32 4, ptr %den, align 4
  %_ex_a_3 = alloca i32, align 4
  %num30 = load i32, ptr %num, align 4
  store i32 %num30, ptr %_ex_a_3, align 4
  %_ex_b_3 = alloca i32, align 4
  %den31 = load i32, ptr %den, align 4
  store i32 %den31, ptr %_ex_b_3, align 4
  %_ex_a_332 = load i32, ptr %_ex_a_3, align 4
  %_ex_b_333 = load i32, ptr %_ex_b_3, align 4
  %call34 = call i32 @printf(ptr @str.8, i32 %_ex_a_332, i32 %_ex_b_333)
  %r3 = alloca i32, align 4
  %num35 = load i32, ptr %num, align 4
  %den36 = load i32, ptr %den, align 4
  %div37 = sdiv i32 %num35, %den36
  store i32 %div37, ptr %r3, align 4
  %r338 = load i32, ptr %r3, align 4
  %call39 = call i32 @printf(ptr @str.9, i32 %r338)
  %call40 = call i32 @printf(ptr @str.10)
  %aa = alloca i32, align 4
  store i32 3, ptr %aa, align 4
  %bb = alloca i32, align 4
  store i32 4, ptr %bb, align 4
  %_ex_x_4 = alloca i32, align 4
  %aa41 = load i32, ptr %aa, align 4
  store i32 %aa41, ptr %_ex_x_4, align 4
  %_ex_y_4 = alloca i32, align 4
  %bb42 = load i32, ptr %bb, align 4
  store i32 %bb42, ptr %_ex_y_4, align 4
  %_ex_x_443 = load i32, ptr %_ex_x_4, align 4
  %_ex_y_444 = load i32, ptr %_ex_y_4, align 4
  %call45 = call i32 @printf(ptr @str.11, i32 %_ex_x_443, i32 %_ex_y_444)
  %s = alloca i32, align 4
  %aa46 = load i32, ptr %aa, align 4
  %bb47 = load i32, ptr %bb, align 4
  %add = add i32 %aa46, %bb47
  store i32 %add, ptr %s, align 4
  %s48 = load i32, ptr %s, align 4
  %call49 = call i32 @printf(ptr @str.12, i32 %s48)
  %call50 = call i32 @printf(ptr @str.13)
  %b1 = alloca ptr, align 8
  %newptr = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Box, ptr null, i32 1) to i64))
  store ptr %newptr, ptr %b1, align 8
  call void @Box_new(ptr %newptr, i32 10)
  %b151 = load ptr, ptr %b1, align 8
  %call52 = call i32 @Box_get(ptr %b151)
  %call53 = call i32 @printf(ptr @str.14, i32 %call52)
  %delptr = load ptr, ptr %b1, align 8
  call void @Box_delete(ptr %delptr)
  call void @free(ptr %delptr)
  %call54 = call i32 @printf(ptr @str.15)
  %b2 = alloca ptr, align 8
  %call55 = call ptr @make_box(i32 20)
  store ptr %call55, ptr %b2, align 8
  %b256 = load ptr, ptr %b2, align 8
  %call57 = call i32 @Box_get(ptr %b256)
  %call58 = call i32 @printf(ptr @str.16, i32 %call57)
  %delptr59 = load ptr, ptr %b2, align 8
  call void @Box_delete(ptr %delptr59)
  call void @free(ptr %delptr59)
  %call60 = call i32 @printf(ptr @str.17)
  ret i32 0

then20:                                           ; preds = %then12
  %msg23 = alloca ptr, align 8
  store ptr @str.5, ptr %msg23, align 8
  %msg24 = load ptr, ptr %msg23, align 8
  %call25 = call i32 @printf(ptr %msg24)
  br label %ifend22

else21:                                           ; preds = %then12
  br label %ifend22

ifend22:                                          ; preds = %else21, %then20
  %dummy = alloca i32, align 4
  %d_ok26 = load i32, ptr %d_ok, align 4
  %div27 = sdiv i32 10, %d_ok26
  store i32 %div27, ptr %dummy, align 4
  %call28 = call i32 @printf(ptr @str.6)
  br label %ifend14
}
