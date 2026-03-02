; ModuleID = 'flame'
source_filename = "flame"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@str = private unnamed_addr constant [24 x i8] c"=== operator tests ===\0A\00", align 1
@str.1 = private unnamed_addr constant [8 x i8] c"& = %d\0A\00", align 1
@str.2 = private unnamed_addr constant [8 x i8] c"| = %d\0A\00", align 1
@str.3 = private unnamed_addr constant [8 x i8] c"^ = %d\0A\00", align 1
@str.4 = private unnamed_addr constant [9 x i8] c"<< = %d\0A\00", align 1
@str.5 = private unnamed_addr constant [9 x i8] c">> = %d\0A\00", align 1
@str.6 = private unnamed_addr constant [8 x i8] c"~ = %d\0A\00", align 1
@str.7 = private unnamed_addr constant [9 x i8] c"+=5: %d\0A\00", align 1
@str.8 = private unnamed_addr constant [9 x i8] c"-=3: %d\0A\00", align 1
@str.9 = private unnamed_addr constant [9 x i8] c"*=2: %d\0A\00", align 1
@str.10 = private unnamed_addr constant [9 x i8] c"/=4: %d\0A\00", align 1
@str.11 = private unnamed_addr constant [10 x i8] c"%%=3: %d\0A\00", align 1
@str.12 = private unnamed_addr constant [14 x i8] c"float + : ok\0A\00", align 1
@str.13 = private unnamed_addr constant [9 x i8] c"&& = %d\0A\00", align 1
@str.14 = private unnamed_addr constant [9 x i8] c"|| = %d\0A\00", align 1
@str.15 = private unnamed_addr constant [8 x i8] c"! = %d\0A\00", align 1

declare ptr @malloc(i64)

declare void @free(ptr)

declare i32 @printf(ptr)

define i32 @main() {
entry:
  %call = call i32 @printf(ptr @str)
  %a = alloca i32, align 4
  store i32 255, ptr %a, align 4
  %b = alloca i32, align 4
  store i32 15, ptr %b, align 4
  %a1 = load i32, ptr %a, align 4
  %b2 = load i32, ptr %b, align 4
  %band = and i32 %a1, %b2
  %call3 = call i32 @printf(ptr @str.1, i32 %band)
  %a4 = load i32, ptr %a, align 4
  %b5 = load i32, ptr %b, align 4
  %bor = or i32 %a4, %b5
  %call6 = call i32 @printf(ptr @str.2, i32 %bor)
  %a7 = load i32, ptr %a, align 4
  %b8 = load i32, ptr %b, align 4
  %bxor = xor i32 %a7, %b8
  %call9 = call i32 @printf(ptr @str.3, i32 %bxor)
  %call10 = call i32 @printf(ptr @str.4, i32 16)
  %call11 = call i32 @printf(ptr @str.5, i32 32)
  %call12 = call i32 @printf(ptr @str.6, i32 -1)
  %x = alloca i32, align 4
  store i32 10, ptr %x, align 4
  %x13 = load i32, ptr %x, align 4
  %add = add i32 %x13, 5
  store i32 %add, ptr %x, align 4
  %x14 = load i32, ptr %x, align 4
  %call15 = call i32 @printf(ptr @str.7, i32 %x14)
  %x16 = load i32, ptr %x, align 4
  %sub = sub i32 %x16, 3
  store i32 %sub, ptr %x, align 4
  %x17 = load i32, ptr %x, align 4
  %call18 = call i32 @printf(ptr @str.8, i32 %x17)
  %x19 = load i32, ptr %x, align 4
  %mul = mul i32 %x19, 2
  store i32 %mul, ptr %x, align 4
  %x20 = load i32, ptr %x, align 4
  %call21 = call i32 @printf(ptr @str.9, i32 %x20)
  %x22 = load i32, ptr %x, align 4
  %div = sdiv i32 %x22, 4
  store i32 %div, ptr %x, align 4
  %x23 = load i32, ptr %x, align 4
  %call24 = call i32 @printf(ptr @str.10, i32 %x23)
  %x25 = load i32, ptr %x, align 4
  %rem = srem i32 %x25, 3
  store i32 %rem, ptr %x, align 4
  %x26 = load i32, ptr %x, align 4
  %call27 = call i32 @printf(ptr @str.11, i32 %x26)
  %f = alloca float, align 4
  store float 0x40091EB860000000, ptr %f, align 4
  %g = alloca float, align 4
  store float 2.000000e+00, ptr %g, align 4
  %call28 = call i32 @printf(ptr @str.12)
  %t = alloca i32, align 4
  store i32 1, ptr %t, align 4
  %t29 = load i32, ptr %t, align 4
  %call30 = call i32 @printf(ptr @str.13, i32 %t29)
  %u = alloca i32, align 4
  store i32 1, ptr %u, align 4
  %u31 = load i32, ptr %u, align 4
  %call32 = call i32 @printf(ptr @str.14, i32 %u31)
  %v = alloca i32, align 4
  store i32 1, ptr %v, align 4
  %v33 = load i32, ptr %v, align 4
  %call34 = call i32 @printf(ptr @str.15, i32 %v33)
  ret i32 0
}
