; ModuleID = 'flame'
source_filename = "flame"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%Counter = type { i32 }

@str = private unnamed_addr constant [36 x i8] c"Counter destroyed, final value: %d\0A\00", align 1
@str.1 = private unnamed_addr constant [21 x i8] c"=== class tests ===\0A\00", align 1
@str.2 = private unnamed_addr constant [14 x i8] c"counter = %d\0A\00", align 1
@str.3 = private unnamed_addr constant [24 x i8] c"--- ownership test ---\0A\00", align 1
@str.4 = private unnamed_addr constant [9 x i8] c"c2 = %d\0A\00", align 1

declare ptr @malloc(i64)

declare void @free(ptr)

declare i32 @printf(ptr)

define void @Counter_new(ptr %0) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %ptr = load ptr, ptr %self, align 8
  %gep = getelementptr %Counter, ptr %ptr, i32 0, i32 0
  store i32 0, ptr %gep, align 4
  ret void
}

define void @Counter_delete(ptr %0) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %ptr = load ptr, ptr %self, align 8
  %gep = getelementptr %Counter, ptr %ptr, i32 0, i32 0
  %value = load i32, ptr %gep, align 4
  %call = call i32 @printf(ptr @str, i32 %value)
  ret void
}

define void @Counter_increment(ptr %0) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %ptr = load ptr, ptr %self, align 8
  %gep = getelementptr %Counter, ptr %ptr, i32 0, i32 0
  %ptr1 = load ptr, ptr %self, align 8
  %gep2 = getelementptr %Counter, ptr %ptr1, i32 0, i32 0
  %value = load i32, ptr %gep2, align 4
  %add = add i32 %value, 1
  store i32 %add, ptr %gep, align 4
  ret void
}

define void @Counter_add(ptr %0, i32 %1) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %n = alloca i32, align 4
  store i32 %1, ptr %n, align 4
  %ptr = load ptr, ptr %self, align 8
  %gep = getelementptr %Counter, ptr %ptr, i32 0, i32 0
  %ptr1 = load ptr, ptr %self, align 8
  %gep2 = getelementptr %Counter, ptr %ptr1, i32 0, i32 0
  %value = load i32, ptr %gep2, align 4
  %n3 = load i32, ptr %n, align 4
  %add = add i32 %value, %n3
  store i32 %add, ptr %gep, align 4
  ret void
}

define i32 @Counter_get(ptr %0) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %ptr = load ptr, ptr %self, align 8
  %gep = getelementptr %Counter, ptr %ptr, i32 0, i32 0
  %value = load i32, ptr %gep, align 4
  ret i32 %value
}

define ptr @make_counter(i32 %0) {
entry:
  %start = alloca i32, align 4
  store i32 %0, ptr %start, align 4
  %c = alloca ptr, align 8
  %newptr = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Counter, ptr null, i32 1) to i64))
  store ptr %newptr, ptr %c, align 8
  call void @Counter_new(ptr %newptr)
  %c1 = load ptr, ptr %c, align 8
  %start2 = load i32, ptr %start, align 4
  call void @Counter_add(ptr %c1, i32 %start2)
  %c3 = load ptr, ptr %c, align 8
  ret ptr %c3
}

define i32 @main() {
entry:
  %call = call i32 @printf(ptr @str.1)
  %c = alloca ptr, align 8
  %newptr = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Counter, ptr null, i32 1) to i64))
  store ptr %newptr, ptr %c, align 8
  call void @Counter_new(ptr %newptr)
  %c1 = load ptr, ptr %c, align 8
  call void @Counter_increment(ptr %c1)
  %c2 = load ptr, ptr %c, align 8
  call void @Counter_increment(ptr %c2)
  %c3 = load ptr, ptr %c, align 8
  call void @Counter_add(ptr %c3, i32 8)
  %c4 = load ptr, ptr %c, align 8
  %call5 = call i32 @Counter_get(ptr %c4)
  %call6 = call i32 @printf(ptr @str.2, i32 %call5)
  %delptr = load ptr, ptr %c, align 8
  call void @Counter_delete(ptr %delptr)
  call void @free(ptr %delptr)
  %call7 = call i32 @printf(ptr @str.3)
  %c28 = alloca ptr, align 8
  %call9 = call ptr @make_counter(i32 10)
  store ptr %call9, ptr %c28, align 8
  %c210 = load ptr, ptr %c28, align 8
  call void @Counter_increment(ptr %c210)
  %c211 = load ptr, ptr %c28, align 8
  %call12 = call i32 @Counter_get(ptr %c211)
  %call13 = call i32 @printf(ptr @str.4, i32 %call12)
  %delptr14 = load ptr, ptr %c28, align 8
  call void @Counter_delete(ptr %delptr14)
  call void @free(ptr %delptr14)
  ret i32 0
}
