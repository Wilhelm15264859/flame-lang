; ModuleID = 'flame'
source_filename = "flame"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%Point = type { i32, i32 }

@str = private unnamed_addr constant [22 x i8] c"=== struct tests ===\0A\00", align 1
@str.1 = private unnamed_addr constant [15 x i8] c"p.x=%d p.y=%d\0A\00", align 1
@str.2 = private unnamed_addr constant [16 x i8] c"point_sum = %d\0A\00", align 1

declare ptr @malloc(i64)

declare void @free(ptr)

declare i32 @printf(ptr)

define i32 @point_sum(%Point %0) {
entry:
  %p = alloca %Point, align 8
  store %Point %0, ptr %p, align 4
  %gep = getelementptr %Point, ptr %p, i32 0, i32 0
  %x = load i32, ptr %gep, align 4
  %gep1 = getelementptr %Point, ptr %p, i32 0, i32 1
  %y = load i32, ptr %gep1, align 4
  %add = add i32 %x, %y
  ret i32 %add
}

define i32 @main() {
entry:
  %call = call i32 @printf(ptr @str)
  %p = alloca %Point, align 8
  %gep = getelementptr %Point, ptr %p, i32 0, i32 0
  store i32 3, ptr %gep, align 4
  %gep1 = getelementptr %Point, ptr %p, i32 0, i32 1
  store i32 4, ptr %gep1, align 4
  %gep2 = getelementptr %Point, ptr %p, i32 0, i32 0
  %x = load i32, ptr %gep2, align 4
  %gep3 = getelementptr %Point, ptr %p, i32 0, i32 1
  %y = load i32, ptr %gep3, align 4
  %call4 = call i32 @printf(ptr @str.1, i32 %x, i32 %y)
  %s = alloca i32, align 4
  %p5 = load %Point, ptr %p, align 4
  %call6 = call i32 @point_sum(%Point %p5)
  store i32 %call6, ptr %s, align 4
  %s7 = load i32, ptr %s, align 4
  %call8 = call i32 @printf(ptr @str.2, i32 %s7)
  ret i32 0
}
