; ModuleID = 'flame'
source_filename = "flame"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%Vertex = type { [2 x i32], [4 x i8] }
%Shape = type { ptr, i32, [2 x i32], [2 x i32], [4 x i8], i32, i32, ptr }
%Screen = type { ptr, i32, [4 x i32] }
%TextLabel = type { ptr, i32, i32, [4 x i8] }

@None = constant i32 0
@Quit = constant i32 1
@Square = constant i32 0
@Circle = constant i32 1
@Triangle = constant i32 2
@Island = constant i32 3
@Rhombus = constant i32 4
@Pentagon = constant i32 5
@currentScreen = global ptr null
@None.1 = constant i32 0
@Quit.2 = constant i32 1
@Square.3 = constant i32 0
@Circle.4 = constant i32 1
@Triangle.5 = constant i32 2
@Island.6 = constant i32 3
@Rhombus.7 = constant i32 4
@Pentagon.8 = constant i32 5
@currentScreen.9 = global ptr null
@str = private unnamed_addr constant [6 x i8] c"First\00", align 1

declare ptr @malloc(i64)

declare void @free(ptr)

declare void @createVertexBuffer(ptr, i32)

declare void @setClearColor(i32, i32, i32, i32)

declare i32 @getFormVertexCount(i32)

declare void @buildFormVertices(i32, i32, i32, i32, i32, i32, i32, i32, i32, ptr)

declare void @destroyVertexBuffer()

declare void @drawFrame()

declare void @initTextSystem(ptr, i32)

declare void @shutdownTextSystem()

declare void @measureText(ptr, ptr, ptr)

declare void @queueText(ptr, i32, i32, i32, i32, i32, i32)

declare i32 @appEvent()

declare ptr @openWindow(ptr, i32, i32)

declare void @closeWindow(ptr)

define void @_Vertex_new_vertexpintintcharcharcharchar(ptr %0, i32 %1, i32 %2, i8 %3, i8 %4, i8 %5, i8 %6) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %x = alloca i32, align 4
  store i32 %1, ptr %x, align 4
  %y = alloca i32, align 4
  store i32 %2, ptr %y, align 4
  %r = alloca i8, align 1
  store i8 %3, ptr %r, align 1
  %g = alloca i8, align 1
  store i8 %4, ptr %g, align 1
  %b = alloca i8, align 1
  store i8 %5, ptr %b, align 1
  %a = alloca i8, align 1
  store i8 %6, ptr %a, align 1
  %ptr = load ptr, ptr %self, align 8
  %field_gep = getelementptr %Vertex, ptr %ptr, i32 0, i32 0
  %elem_gep = getelementptr [2 x i32], ptr %field_gep, i32 0, i32 0
  %x1 = load i32, ptr %x, align 4
  store i32 %x1, ptr %elem_gep, align 4
  %ptr2 = load ptr, ptr %self, align 8
  %field_gep3 = getelementptr %Vertex, ptr %ptr2, i32 0, i32 0
  %elem_gep4 = getelementptr [2 x i32], ptr %field_gep3, i32 0, i32 1
  %y5 = load i32, ptr %y, align 4
  store i32 %y5, ptr %elem_gep4, align 4
  %ptr6 = load ptr, ptr %self, align 8
  %field_gep7 = getelementptr %Vertex, ptr %ptr6, i32 0, i32 1
  %elem_gep8 = getelementptr [4 x i8], ptr %field_gep7, i32 0, i32 0
  %r9 = load i8, ptr %r, align 1
  store i8 %r9, ptr %elem_gep8, align 1
  %ptr10 = load ptr, ptr %self, align 8
  %field_gep11 = getelementptr %Vertex, ptr %ptr10, i32 0, i32 1
  %elem_gep12 = getelementptr [4 x i8], ptr %field_gep11, i32 0, i32 1
  %g13 = load i8, ptr %g, align 1
  store i8 %g13, ptr %elem_gep12, align 1
  %ptr14 = load ptr, ptr %self, align 8
  %field_gep15 = getelementptr %Vertex, ptr %ptr14, i32 0, i32 1
  %elem_gep16 = getelementptr [4 x i8], ptr %field_gep15, i32 0, i32 2
  %b17 = load i8, ptr %b, align 1
  store i8 %b17, ptr %elem_gep16, align 1
  %ptr18 = load ptr, ptr %self, align 8
  %field_gep19 = getelementptr %Vertex, ptr %ptr18, i32 0, i32 1
  %elem_gep20 = getelementptr [4 x i8], ptr %field_gep19, i32 0, i32 3
  %a21 = load i8, ptr %a, align 1
  store i8 %a21, ptr %elem_gep20, align 1
  ret void
}

define void @_Shape_new_shapepintintintintintcharcharcharchar(ptr %0, i32 %1, i32 %2, i32 %3, i32 %4, i32 %5, i8 %6, i8 %7, i8 %8, i8 %9) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %form = alloca i32, align 4
  store i32 %1, ptr %form, align 4
  %x = alloca i32, align 4
  store i32 %2, ptr %x, align 4
  %y = alloca i32, align 4
  store i32 %3, ptr %y, align 4
  %w = alloca i32, align 4
  store i32 %4, ptr %w, align 4
  %h = alloca i32, align 4
  store i32 %5, ptr %h, align 4
  %r = alloca i8, align 1
  store i8 %6, ptr %r, align 1
  %g = alloca i8, align 1
  store i8 %7, ptr %g, align 1
  %b = alloca i8, align 1
  store i8 %8, ptr %b, align 1
  %a = alloca i8, align 1
  store i8 %9, ptr %a, align 1
  %ptr = load ptr, ptr %self, align 8
  %gep = getelementptr %Shape, ptr %ptr, i32 0, i32 6
  store i32 0, ptr %gep, align 4
  %ptr1 = load ptr, ptr %self, align 8
  %gep2 = getelementptr %Shape, ptr %ptr1, i32 0, i32 7
  store ptr null, ptr %gep2, align 8
  %ptr3 = load ptr, ptr %self, align 8
  %gep4 = getelementptr %Shape, ptr %ptr3, i32 0, i32 5
  %form5 = load i32, ptr %form, align 4
  store i32 %form5, ptr %gep4, align 4
  %ptr6 = load ptr, ptr %self, align 8
  %field_gep = getelementptr %Shape, ptr %ptr6, i32 0, i32 3
  %elem_gep = getelementptr [2 x i32], ptr %field_gep, i32 0, i32 0
  %x7 = load i32, ptr %x, align 4
  store i32 %x7, ptr %elem_gep, align 4
  %ptr8 = load ptr, ptr %self, align 8
  %field_gep9 = getelementptr %Shape, ptr %ptr8, i32 0, i32 3
  %elem_gep10 = getelementptr [2 x i32], ptr %field_gep9, i32 0, i32 1
  %y11 = load i32, ptr %y, align 4
  store i32 %y11, ptr %elem_gep10, align 4
  %ptr12 = load ptr, ptr %self, align 8
  %field_gep13 = getelementptr %Shape, ptr %ptr12, i32 0, i32 2
  %elem_gep14 = getelementptr [2 x i32], ptr %field_gep13, i32 0, i32 0
  %w15 = load i32, ptr %w, align 4
  store i32 %w15, ptr %elem_gep14, align 4
  %ptr16 = load ptr, ptr %self, align 8
  %field_gep17 = getelementptr %Shape, ptr %ptr16, i32 0, i32 2
  %elem_gep18 = getelementptr [2 x i32], ptr %field_gep17, i32 0, i32 1
  %h19 = load i32, ptr %h, align 4
  store i32 %h19, ptr %elem_gep18, align 4
  %ptr20 = load ptr, ptr %self, align 8
  %field_gep21 = getelementptr %Shape, ptr %ptr20, i32 0, i32 4
  %elem_gep22 = getelementptr [4 x i8], ptr %field_gep21, i32 0, i32 0
  %r23 = load i8, ptr %r, align 1
  store i8 %r23, ptr %elem_gep22, align 1
  %ptr24 = load ptr, ptr %self, align 8
  %field_gep25 = getelementptr %Shape, ptr %ptr24, i32 0, i32 4
  %elem_gep26 = getelementptr [4 x i8], ptr %field_gep25, i32 0, i32 1
  %g27 = load i8, ptr %g, align 1
  store i8 %g27, ptr %elem_gep26, align 1
  %ptr28 = load ptr, ptr %self, align 8
  %field_gep29 = getelementptr %Shape, ptr %ptr28, i32 0, i32 4
  %elem_gep30 = getelementptr [4 x i8], ptr %field_gep29, i32 0, i32 2
  %b31 = load i8, ptr %b, align 1
  store i8 %b31, ptr %elem_gep30, align 1
  %ptr32 = load ptr, ptr %self, align 8
  %field_gep33 = getelementptr %Shape, ptr %ptr32, i32 0, i32 4
  %elem_gep34 = getelementptr [4 x i8], ptr %field_gep33, i32 0, i32 3
  %a35 = load i8, ptr %a, align 1
  store i8 %a35, ptr %elem_gep34, align 1
  %vc = alloca i32, align 4
  %form36 = load i32, ptr %form, align 4
  %call = call i32 @getFormVertexCount(i32 %form36)
  store i32 %call, ptr %vc, align 4
  %ptr37 = load ptr, ptr %self, align 8
  %gep38 = getelementptr %Shape, ptr %ptr37, i32 0, i32 1
  %vc39 = load i32, ptr %vc, align 4
  store i32 %vc39, ptr %gep38, align 4
  %ptr40 = load ptr, ptr %self, align 8
  %gep41 = getelementptr %Shape, ptr %ptr40, i32 0, i32 0
  %vc42 = load i32, ptr %vc, align 4
  %mul = mul i32 %vc42, 12
  %zext = zext i32 %mul to i64
  %call43 = call ptr @malloc(i64 %zext)
  store ptr %call43, ptr %gep41, align 8
  %form44 = load i32, ptr %form, align 4
  %x45 = load i32, ptr %x, align 4
  %y46 = load i32, ptr %y, align 4
  %w47 = load i32, ptr %w, align 4
  %h48 = load i32, ptr %h, align 4
  %r49 = load i8, ptr %r, align 1
  %zext50 = zext i8 %r49 to i32
  %g51 = load i8, ptr %g, align 1
  %zext52 = zext i8 %g51 to i32
  %b53 = load i8, ptr %b, align 1
  %zext54 = zext i8 %b53 to i32
  %a55 = load i8, ptr %a, align 1
  %zext56 = zext i8 %a55 to i32
  %ptr57 = load ptr, ptr %self, align 8
  %gep58 = getelementptr %Shape, ptr %ptr57, i32 0, i32 0
  %vertices = load ptr, ptr %gep58, align 8
  call void @buildFormVertices(i32 %form44, i32 %x45, i32 %y46, i32 %w47, i32 %h48, i32 %zext50, i32 %zext52, i32 %zext54, i32 %zext56, ptr %vertices)
  ret void
}

define void @_Shape_new_shapepvertexpint(ptr %0, ptr %1, i32 %2) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %vertices = alloca ptr, align 8
  store ptr %1, ptr %vertices, align 8
  %count = alloca i32, align 4
  store i32 %2, ptr %count, align 4
  %ptr = load ptr, ptr %self, align 8
  %gep = getelementptr %Shape, ptr %ptr, i32 0, i32 6
  store i32 0, ptr %gep, align 4
  %ptr1 = load ptr, ptr %self, align 8
  %gep2 = getelementptr %Shape, ptr %ptr1, i32 0, i32 7
  store ptr null, ptr %gep2, align 8
  %ptr3 = load ptr, ptr %self, align 8
  %gep4 = getelementptr %Shape, ptr %ptr3, i32 0, i32 5
  store i32 -1, ptr %gep4, align 4
  %ptr5 = load ptr, ptr %self, align 8
  %gep6 = getelementptr %Shape, ptr %ptr5, i32 0, i32 1
  %count7 = load i32, ptr %count, align 4
  store i32 %count7, ptr %gep6, align 4
  %ptr8 = load ptr, ptr %self, align 8
  %gep9 = getelementptr %Shape, ptr %ptr8, i32 0, i32 0
  %vertices10 = load ptr, ptr %vertices, align 8
  store ptr %vertices10, ptr %gep9, align 8
  %ptr11 = load ptr, ptr %self, align 8
  %field_gep = getelementptr %Shape, ptr %ptr11, i32 0, i32 3
  %elem_gep = getelementptr [2 x i32], ptr %field_gep, i32 0, i32 0
  store i32 0, ptr %elem_gep, align 4
  %ptr12 = load ptr, ptr %self, align 8
  %field_gep13 = getelementptr %Shape, ptr %ptr12, i32 0, i32 3
  %elem_gep14 = getelementptr [2 x i32], ptr %field_gep13, i32 0, i32 1
  store i32 0, ptr %elem_gep14, align 4
  %ptr15 = load ptr, ptr %self, align 8
  %field_gep16 = getelementptr %Shape, ptr %ptr15, i32 0, i32 2
  %elem_gep17 = getelementptr [2 x i32], ptr %field_gep16, i32 0, i32 0
  store i32 0, ptr %elem_gep17, align 4
  %ptr18 = load ptr, ptr %self, align 8
  %field_gep19 = getelementptr %Shape, ptr %ptr18, i32 0, i32 2
  %elem_gep20 = getelementptr [2 x i32], ptr %field_gep19, i32 0, i32 1
  store i32 0, ptr %elem_gep20, align 4
  %ptr21 = load ptr, ptr %self, align 8
  %field_gep22 = getelementptr %Shape, ptr %ptr21, i32 0, i32 4
  %elem_gep23 = getelementptr [4 x i8], ptr %field_gep22, i32 0, i32 0
  store i8 0, ptr %elem_gep23, align 1
  %ptr24 = load ptr, ptr %self, align 8
  %field_gep25 = getelementptr %Shape, ptr %ptr24, i32 0, i32 4
  %elem_gep26 = getelementptr [4 x i8], ptr %field_gep25, i32 0, i32 1
  store i8 0, ptr %elem_gep26, align 1
  %ptr27 = load ptr, ptr %self, align 8
  %field_gep28 = getelementptr %Shape, ptr %ptr27, i32 0, i32 4
  %elem_gep29 = getelementptr [4 x i8], ptr %field_gep28, i32 0, i32 2
  store i8 0, ptr %elem_gep29, align 1
  %ptr30 = load ptr, ptr %self, align 8
  %field_gep31 = getelementptr %Shape, ptr %ptr30, i32 0, i32 4
  %elem_gep32 = getelementptr [4 x i8], ptr %field_gep31, i32 0, i32 3
  store i8 0, ptr %elem_gep32, align 1
  ret void
}

define void @_Shape_new_shapepcharpintintcharcharcharchar(ptr %0, ptr %1, i32 %2, i32 %3, i8 %4, i8 %5, i8 %6, i8 %7) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %text = alloca ptr, align 8
  store ptr %1, ptr %text, align 8
  %x = alloca i32, align 4
  store i32 %2, ptr %x, align 4
  %y = alloca i32, align 4
  store i32 %3, ptr %y, align 4
  %r = alloca i8, align 1
  store i8 %4, ptr %r, align 1
  %g = alloca i8, align 1
  store i8 %5, ptr %g, align 1
  %b = alloca i8, align 1
  store i8 %6, ptr %b, align 1
  %a = alloca i8, align 1
  store i8 %7, ptr %a, align 1
  %ptr = load ptr, ptr %self, align 8
  %gep = getelementptr %Shape, ptr %ptr, i32 0, i32 6
  store i32 1, ptr %gep, align 4
  %ptr1 = load ptr, ptr %self, align 8
  %gep2 = getelementptr %Shape, ptr %ptr1, i32 0, i32 7
  %text3 = load ptr, ptr %text, align 8
  store ptr %text3, ptr %gep2, align 8
  %ptr4 = load ptr, ptr %self, align 8
  %gep5 = getelementptr %Shape, ptr %ptr4, i32 0, i32 5
  store i32 -1, ptr %gep5, align 4
  %ptr6 = load ptr, ptr %self, align 8
  %gep7 = getelementptr %Shape, ptr %ptr6, i32 0, i32 0
  store ptr null, ptr %gep7, align 8
  %ptr8 = load ptr, ptr %self, align 8
  %gep9 = getelementptr %Shape, ptr %ptr8, i32 0, i32 1
  store i32 0, ptr %gep9, align 4
  %ptr10 = load ptr, ptr %self, align 8
  %field_gep = getelementptr %Shape, ptr %ptr10, i32 0, i32 3
  %elem_gep = getelementptr [2 x i32], ptr %field_gep, i32 0, i32 0
  %x11 = load i32, ptr %x, align 4
  store i32 %x11, ptr %elem_gep, align 4
  %ptr12 = load ptr, ptr %self, align 8
  %field_gep13 = getelementptr %Shape, ptr %ptr12, i32 0, i32 3
  %elem_gep14 = getelementptr [2 x i32], ptr %field_gep13, i32 0, i32 1
  %y15 = load i32, ptr %y, align 4
  store i32 %y15, ptr %elem_gep14, align 4
  %ptr16 = load ptr, ptr %self, align 8
  %field_gep17 = getelementptr %Shape, ptr %ptr16, i32 0, i32 2
  %elem_gep18 = getelementptr [2 x i32], ptr %field_gep17, i32 0, i32 0
  store i32 0, ptr %elem_gep18, align 4
  %ptr19 = load ptr, ptr %self, align 8
  %field_gep20 = getelementptr %Shape, ptr %ptr19, i32 0, i32 2
  %elem_gep21 = getelementptr [2 x i32], ptr %field_gep20, i32 0, i32 1
  store i32 0, ptr %elem_gep21, align 4
  %ptr22 = load ptr, ptr %self, align 8
  %field_gep23 = getelementptr %Shape, ptr %ptr22, i32 0, i32 4
  %elem_gep24 = getelementptr [4 x i8], ptr %field_gep23, i32 0, i32 0
  %r25 = load i8, ptr %r, align 1
  store i8 %r25, ptr %elem_gep24, align 1
  %ptr26 = load ptr, ptr %self, align 8
  %field_gep27 = getelementptr %Shape, ptr %ptr26, i32 0, i32 4
  %elem_gep28 = getelementptr [4 x i8], ptr %field_gep27, i32 0, i32 1
  %g29 = load i8, ptr %g, align 1
  store i8 %g29, ptr %elem_gep28, align 1
  %ptr30 = load ptr, ptr %self, align 8
  %field_gep31 = getelementptr %Shape, ptr %ptr30, i32 0, i32 4
  %elem_gep32 = getelementptr [4 x i8], ptr %field_gep31, i32 0, i32 2
  %b33 = load i8, ptr %b, align 1
  store i8 %b33, ptr %elem_gep32, align 1
  %ptr34 = load ptr, ptr %self, align 8
  %field_gep35 = getelementptr %Shape, ptr %ptr34, i32 0, i32 4
  %elem_gep36 = getelementptr [4 x i8], ptr %field_gep35, i32 0, i32 3
  %a37 = load i8, ptr %a, align 1
  store i8 %a37, ptr %elem_gep36, align 1
  ret void
}

define void @_Shape_move_shapepintint(ptr %0, i32 %1, i32 %2) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %x = alloca i32, align 4
  store i32 %1, ptr %x, align 4
  %y = alloca i32, align 4
  store i32 %2, ptr %y, align 4
  %ptr = load ptr, ptr %self, align 8
  %field_gep = getelementptr %Shape, ptr %ptr, i32 0, i32 3
  %elem_gep = getelementptr [2 x i32], ptr %field_gep, i32 0, i32 0
  %x1 = load i32, ptr %x, align 4
  store i32 %x1, ptr %elem_gep, align 4
  %ptr2 = load ptr, ptr %self, align 8
  %field_gep3 = getelementptr %Shape, ptr %ptr2, i32 0, i32 3
  %elem_gep4 = getelementptr [2 x i32], ptr %field_gep3, i32 0, i32 1
  %y5 = load i32, ptr %y, align 4
  store i32 %y5, ptr %elem_gep4, align 4
  %ptr6 = load ptr, ptr %self, align 8
  %gep = getelementptr %Shape, ptr %ptr6, i32 0, i32 5
  %form = load i32, ptr %gep, align 4
  %x7 = load i32, ptr %x, align 4
  %y8 = load i32, ptr %y, align 4
  %ptr9 = load ptr, ptr %self, align 8
  %field_gep10 = getelementptr %Shape, ptr %ptr9, i32 0, i32 2
  %elem_gep11 = getelementptr [2 x i32], ptr %field_gep10, i32 0, i32 0
  %elem = load i32, ptr %elem_gep11, align 4
  %ptr12 = load ptr, ptr %self, align 8
  %field_gep13 = getelementptr %Shape, ptr %ptr12, i32 0, i32 2
  %elem_gep14 = getelementptr [2 x i32], ptr %field_gep13, i32 0, i32 1
  %elem15 = load i32, ptr %elem_gep14, align 4
  %ptr16 = load ptr, ptr %self, align 8
  %field_gep17 = getelementptr %Shape, ptr %ptr16, i32 0, i32 4
  %elem_gep18 = getelementptr [4 x i8], ptr %field_gep17, i32 0, i32 0
  %elem19 = load i8, ptr %elem_gep18, align 1
  %zext = zext i8 %elem19 to i32
  %ptr20 = load ptr, ptr %self, align 8
  %field_gep21 = getelementptr %Shape, ptr %ptr20, i32 0, i32 4
  %elem_gep22 = getelementptr [4 x i8], ptr %field_gep21, i32 0, i32 1
  %elem23 = load i8, ptr %elem_gep22, align 1
  %zext24 = zext i8 %elem23 to i32
  %ptr25 = load ptr, ptr %self, align 8
  %field_gep26 = getelementptr %Shape, ptr %ptr25, i32 0, i32 4
  %elem_gep27 = getelementptr [4 x i8], ptr %field_gep26, i32 0, i32 2
  %elem28 = load i8, ptr %elem_gep27, align 1
  %zext29 = zext i8 %elem28 to i32
  %ptr30 = load ptr, ptr %self, align 8
  %field_gep31 = getelementptr %Shape, ptr %ptr30, i32 0, i32 4
  %elem_gep32 = getelementptr [4 x i8], ptr %field_gep31, i32 0, i32 3
  %elem33 = load i8, ptr %elem_gep32, align 1
  %zext34 = zext i8 %elem33 to i32
  %ptr35 = load ptr, ptr %self, align 8
  %gep36 = getelementptr %Shape, ptr %ptr35, i32 0, i32 0
  %vertices = load ptr, ptr %gep36, align 8
  call void @buildFormVertices(i32 %form, i32 %x7, i32 %y8, i32 %elem, i32 %elem15, i32 %zext, i32 %zext24, i32 %zext29, i32 %zext34, ptr %vertices)
  ret void
}

define void @_Shape_delete_shapep(ptr %0) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %selfptr = load ptr, ptr %self, align 8
  %field_slot = getelementptr %Shape, ptr %selfptr, i32 0, i32 0
  %delptr = load ptr, ptr %field_slot, align 8
  call void @free(ptr %delptr)
  store ptr null, ptr %field_slot, align 8
  %selfptr1 = load ptr, ptr %self, align 8
  %field_slot2 = getelementptr %Shape, ptr %selfptr1, i32 0, i32 7
  %delptr3 = load ptr, ptr %field_slot2, align 8
  call void @free(ptr %delptr3)
  store ptr null, ptr %field_slot2, align 8
  ret void
}

define void @_Screen_new_screenpshapepintintintintint(ptr %0, ptr %1, i32 %2, i32 %3, i32 %4, i32 %5, i32 %6) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %shapes = alloca ptr, align 8
  store ptr %1, ptr %shapes, align 8
  %count = alloca i32, align 4
  store i32 %2, ptr %count, align 4
  %r = alloca i32, align 4
  store i32 %3, ptr %r, align 4
  %g = alloca i32, align 4
  store i32 %4, ptr %g, align 4
  %b = alloca i32, align 4
  store i32 %5, ptr %b, align 4
  %a = alloca i32, align 4
  store i32 %6, ptr %a, align 4
  %ptr = load ptr, ptr %self, align 8
  %gep = getelementptr %Screen, ptr %ptr, i32 0, i32 0
  %shapes1 = load ptr, ptr %shapes, align 8
  store ptr %shapes1, ptr %gep, align 8
  %ptr2 = load ptr, ptr %self, align 8
  %gep3 = getelementptr %Screen, ptr %ptr2, i32 0, i32 1
  %count4 = load i32, ptr %count, align 4
  store i32 %count4, ptr %gep3, align 4
  %ptr5 = load ptr, ptr %self, align 8
  %field_gep = getelementptr %Screen, ptr %ptr5, i32 0, i32 2
  %elem_gep = getelementptr [4 x i32], ptr %field_gep, i32 0, i32 0
  %r6 = load i32, ptr %r, align 4
  store i32 %r6, ptr %elem_gep, align 4
  %ptr7 = load ptr, ptr %self, align 8
  %field_gep8 = getelementptr %Screen, ptr %ptr7, i32 0, i32 2
  %elem_gep9 = getelementptr [4 x i32], ptr %field_gep8, i32 0, i32 1
  %g10 = load i32, ptr %g, align 4
  store i32 %g10, ptr %elem_gep9, align 4
  %ptr11 = load ptr, ptr %self, align 8
  %field_gep12 = getelementptr %Screen, ptr %ptr11, i32 0, i32 2
  %elem_gep13 = getelementptr [4 x i32], ptr %field_gep12, i32 0, i32 2
  %b14 = load i32, ptr %b, align 4
  store i32 %b14, ptr %elem_gep13, align 4
  %ptr15 = load ptr, ptr %self, align 8
  %field_gep16 = getelementptr %Screen, ptr %ptr15, i32 0, i32 2
  %elem_gep17 = getelementptr [4 x i32], ptr %field_gep16, i32 0, i32 3
  %a18 = load i32, ptr %a, align 4
  store i32 %a18, ptr %elem_gep17, align 4
  ret void
}

define void @_Screen_set_screenp(ptr %0) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %self1 = load ptr, ptr %self, align 8
  store ptr %self1, ptr @currentScreen, align 8
  %ptr = load ptr, ptr %self, align 8
  %field_gep = getelementptr %Screen, ptr %ptr, i32 0, i32 2
  %elem_gep = getelementptr [4 x i32], ptr %field_gep, i32 0, i32 0
  %elem = load i32, ptr %elem_gep, align 4
  %ptr2 = load ptr, ptr %self, align 8
  %field_gep3 = getelementptr %Screen, ptr %ptr2, i32 0, i32 2
  %elem_gep4 = getelementptr [4 x i32], ptr %field_gep3, i32 0, i32 1
  %elem5 = load i32, ptr %elem_gep4, align 4
  %ptr6 = load ptr, ptr %self, align 8
  %field_gep7 = getelementptr %Screen, ptr %ptr6, i32 0, i32 2
  %elem_gep8 = getelementptr [4 x i32], ptr %field_gep7, i32 0, i32 2
  %elem9 = load i32, ptr %elem_gep8, align 4
  %ptr10 = load ptr, ptr %self, align 8
  %field_gep11 = getelementptr %Screen, ptr %ptr10, i32 0, i32 2
  %elem_gep12 = getelementptr [4 x i32], ptr %field_gep11, i32 0, i32 3
  %elem13 = load i32, ptr %elem_gep12, align 4
  call void @setClearColor(i32 %elem, i32 %elem5, i32 %elem9, i32 %elem13)
  call void @destroyVertexBuffer()
  %totalVertices = alloca i32, align 4
  store i32 0, ptr %totalVertices, align 4
  %i = alloca i32, align 4
  store i32 0, ptr %i, align 4
  br label %wcond

wcond:                                            ; preds = %ifend, %entry
  %i14 = load i32, ptr %i, align 4
  %ptr15 = load ptr, ptr %self, align 8
  %gep = getelementptr %Screen, ptr %ptr15, i32 0, i32 1
  %shapeCount = load i32, ptr %gep, align 4
  %lt = icmp slt i32 %i14, %shapeCount
  %bool = zext i1 %lt to i32
  %wcond16 = icmp ne i32 %bool, 0
  br i1 %wcond16, label %wbody, label %wend

wbody:                                            ; preds = %wcond
  %sh = alloca ptr, align 8
  %i17 = load i32, ptr %i, align 4
  %ptr18 = load ptr, ptr %self, align 8
  %field_gep19 = getelementptr %Screen, ptr %ptr18, i32 0, i32 0
  %fld_ptr = load ptr, ptr %field_gep19, align 8
  %elemptr = getelementptr %Shape, ptr %fld_ptr, i32 %i17
  store ptr %elemptr, ptr %sh, align 8
  %ptr20 = load ptr, ptr %sh, align 8
  %gep21 = getelementptr %Shape, ptr %ptr20, i32 0, i32 6
  %type = load i32, ptr %gep21, align 4
  %eq = icmp eq i32 %type, 0
  %bool22 = zext i1 %eq to i32
  %ifcond = icmp ne i32 %bool22, 0
  br i1 %ifcond, label %then, label %else

wend:                                             ; preds = %wcond
  %totalSize = alloca i32, align 4
  %totalVertices28 = load i32, ptr %totalVertices, align 4
  %mul = mul i32 %totalVertices28, 12
  store i32 %mul, ptr %totalSize, align 4
  %totalVertices29 = load i32, ptr %totalVertices, align 4
  %zext = zext i32 %totalVertices29 to i64
  %allVertices = alloca %Vertex, i64 %zext, align 8
  store i32 0, ptr %i, align 4
  %vi = alloca i32, align 4
  store i32 0, ptr %vi, align 4
  br label %wcond30

then:                                             ; preds = %wbody
  %totalVertices23 = load i32, ptr %totalVertices, align 4
  %ptr24 = load ptr, ptr %sh, align 8
  %gep25 = getelementptr %Shape, ptr %ptr24, i32 0, i32 1
  %verticesCount = load i32, ptr %gep25, align 4
  %add = add i32 %totalVertices23, %verticesCount
  store i32 %add, ptr %totalVertices, align 4
  br label %ifend

else:                                             ; preds = %wbody
  br label %ifend

ifend:                                            ; preds = %else, %then
  %i26 = load i32, ptr %i, align 4
  %add27 = add i32 %i26, 1
  store i32 %add27, ptr %i, align 4
  br label %wcond

wcond30:                                          ; preds = %ifend54, %wend
  %i33 = load i32, ptr %i, align 4
  %ptr34 = load ptr, ptr %self, align 8
  %gep35 = getelementptr %Screen, ptr %ptr34, i32 0, i32 1
  %shapeCount36 = load i32, ptr %gep35, align 4
  %lt37 = icmp slt i32 %i33, %shapeCount36
  %bool38 = zext i1 %lt37 to i32
  %wcond39 = icmp ne i32 %bool38, 0
  br i1 %wcond39, label %wbody31, label %wend32

wbody31:                                          ; preds = %wcond30
  %sh40 = alloca ptr, align 8
  %i41 = load i32, ptr %i, align 4
  %ptr42 = load ptr, ptr %self, align 8
  %field_gep43 = getelementptr %Screen, ptr %ptr42, i32 0, i32 0
  %fld_ptr44 = load ptr, ptr %field_gep43, align 8
  %elemptr45 = getelementptr %Shape, ptr %fld_ptr44, i32 %i41
  store ptr %elemptr45, ptr %sh40, align 8
  %ptr46 = load ptr, ptr %sh40, align 8
  %gep47 = getelementptr %Shape, ptr %ptr46, i32 0, i32 6
  %type48 = load i32, ptr %gep47, align 4
  %eq49 = icmp eq i32 %type48, 0
  %bool50 = zext i1 %eq49 to i32
  %ifcond51 = icmp ne i32 %bool50, 0
  br i1 %ifcond51, label %then52, label %else53

wend32:                                           ; preds = %wcond30
  %totalVertices79 = load i32, ptr %totalVertices, align 4
  %gt = icmp sgt i32 %totalVertices79, 0
  %bool80 = zext i1 %gt to i32
  %ifcond81 = icmp ne i32 %bool80, 0
  br i1 %ifcond81, label %then82, label %else83

then52:                                           ; preds = %wbody31
  %j = alloca i32, align 4
  store i32 0, ptr %j, align 4
  br label %wcond55

else53:                                           ; preds = %wbody31
  br label %ifend54

ifend54:                                          ; preds = %else53, %wend57
  %i77 = load i32, ptr %i, align 4
  %add78 = add i32 %i77, 1
  store i32 %add78, ptr %i, align 4
  br label %wcond30

wcond55:                                          ; preds = %wbody56, %then52
  %j58 = load i32, ptr %j, align 4
  %ptr59 = load ptr, ptr %sh40, align 8
  %gep60 = getelementptr %Shape, ptr %ptr59, i32 0, i32 1
  %verticesCount61 = load i32, ptr %gep60, align 4
  %lt62 = icmp slt i32 %j58, %verticesCount61
  %bool63 = zext i1 %lt62 to i32
  %wcond64 = icmp ne i32 %bool63, 0
  br i1 %wcond64, label %wbody56, label %wend57

wbody56:                                          ; preds = %wcond55
  %vi65 = load i32, ptr %vi, align 4
  %gep66 = getelementptr %Vertex, ptr %allVertices, i32 %vi65
  %j67 = load i32, ptr %j, align 4
  %ptr68 = load ptr, ptr %sh40, align 8
  %field_gep69 = getelementptr %Shape, ptr %ptr68, i32 0, i32 0
  %fld_ptr70 = load ptr, ptr %field_gep69, align 8
  %elem_gep71 = getelementptr %Vertex, ptr %fld_ptr70, i32 %j67
  %elem72 = load %Vertex, ptr %elem_gep71, align 4
  store %Vertex %elem72, ptr %gep66, align 4
  %vi73 = load i32, ptr %vi, align 4
  %add74 = add i32 %vi73, 1
  store i32 %add74, ptr %vi, align 4
  %j75 = load i32, ptr %j, align 4
  %add76 = add i32 %j75, 1
  store i32 %add76, ptr %j, align 4
  br label %wcond55

wend57:                                           ; preds = %wcond55
  br label %ifend54

then82:                                           ; preds = %wend32
  %totalSize85 = load i32, ptr %totalSize, align 4
  call void @createVertexBuffer(ptr %allVertices, i32 %totalSize85)
  br label %ifend84

else83:                                           ; preds = %wend32
  br label %ifend84

ifend84:                                          ; preds = %else83, %then82
  store i32 0, ptr %i, align 4
  br label %wcond86

wcond86:                                          ; preds = %ifend110, %ifend84
  %i89 = load i32, ptr %i, align 4
  %ptr90 = load ptr, ptr %self, align 8
  %gep91 = getelementptr %Screen, ptr %ptr90, i32 0, i32 1
  %shapeCount92 = load i32, ptr %gep91, align 4
  %lt93 = icmp slt i32 %i89, %shapeCount92
  %bool94 = zext i1 %lt93 to i32
  %wcond95 = icmp ne i32 %bool94, 0
  br i1 %wcond95, label %wbody87, label %wend88

wbody87:                                          ; preds = %wcond86
  %sh96 = alloca ptr, align 8
  %i97 = load i32, ptr %i, align 4
  %ptr98 = load ptr, ptr %self, align 8
  %field_gep99 = getelementptr %Screen, ptr %ptr98, i32 0, i32 0
  %fld_ptr100 = load ptr, ptr %field_gep99, align 8
  %elemptr101 = getelementptr %Shape, ptr %fld_ptr100, i32 %i97
  store ptr %elemptr101, ptr %sh96, align 8
  %ptr102 = load ptr, ptr %sh96, align 8
  %gep103 = getelementptr %Shape, ptr %ptr102, i32 0, i32 6
  %type104 = load i32, ptr %gep103, align 4
  %eq105 = icmp eq i32 %type104, 1
  %bool106 = zext i1 %eq105 to i32
  %ifcond107 = icmp ne i32 %bool106, 0
  br i1 %ifcond107, label %then108, label %else109

wend88:                                           ; preds = %wcond86
  call void @drawFrame()
  ret void

then108:                                          ; preds = %wbody87
  %ptr111 = load ptr, ptr %sh96, align 8
  %gep112 = getelementptr %Shape, ptr %ptr111, i32 0, i32 7
  %text = load ptr, ptr %gep112, align 8
  %ne = icmp ne ptr %text, null
  %bool113 = zext i1 %ne to i32
  %ifcond114 = icmp ne i32 %bool113, 0
  br i1 %ifcond114, label %then115, label %else116

else109:                                          ; preds = %wbody87
  br label %ifend110

ifend110:                                         ; preds = %else109, %ifend117
  %i149 = load i32, ptr %i, align 4
  %add150 = add i32 %i149, 1
  store i32 %add150, ptr %i, align 4
  br label %wcond86

then115:                                          ; preds = %then108
  %ptr118 = load ptr, ptr %sh96, align 8
  %gep119 = getelementptr %Shape, ptr %ptr118, i32 0, i32 7
  %text120 = load ptr, ptr %gep119, align 8
  %ptr121 = load ptr, ptr %sh96, align 8
  %field_gep122 = getelementptr %Shape, ptr %ptr121, i32 0, i32 3
  %elem_gep123 = getelementptr [2 x i32], ptr %field_gep122, i32 0, i32 0
  %elem124 = load i32, ptr %elem_gep123, align 4
  %ptr125 = load ptr, ptr %sh96, align 8
  %field_gep126 = getelementptr %Shape, ptr %ptr125, i32 0, i32 3
  %elem_gep127 = getelementptr [2 x i32], ptr %field_gep126, i32 0, i32 1
  %elem128 = load i32, ptr %elem_gep127, align 4
  %ptr129 = load ptr, ptr %sh96, align 8
  %field_gep130 = getelementptr %Shape, ptr %ptr129, i32 0, i32 4
  %elem_gep131 = getelementptr [4 x i8], ptr %field_gep130, i32 0, i32 0
  %elem132 = load i8, ptr %elem_gep131, align 1
  %zext133 = zext i8 %elem132 to i32
  %ptr134 = load ptr, ptr %sh96, align 8
  %field_gep135 = getelementptr %Shape, ptr %ptr134, i32 0, i32 4
  %elem_gep136 = getelementptr [4 x i8], ptr %field_gep135, i32 0, i32 1
  %elem137 = load i8, ptr %elem_gep136, align 1
  %zext138 = zext i8 %elem137 to i32
  %ptr139 = load ptr, ptr %sh96, align 8
  %field_gep140 = getelementptr %Shape, ptr %ptr139, i32 0, i32 4
  %elem_gep141 = getelementptr [4 x i8], ptr %field_gep140, i32 0, i32 2
  %elem142 = load i8, ptr %elem_gep141, align 1
  %zext143 = zext i8 %elem142 to i32
  %ptr144 = load ptr, ptr %sh96, align 8
  %field_gep145 = getelementptr %Shape, ptr %ptr144, i32 0, i32 4
  %elem_gep146 = getelementptr [4 x i8], ptr %field_gep145, i32 0, i32 3
  %elem147 = load i8, ptr %elem_gep146, align 1
  %zext148 = zext i8 %elem147 to i32
  call void @queueText(ptr %text120, i32 %elem124, i32 %elem128, i32 %zext133, i32 %zext138, i32 %zext143, i32 %zext148)
  br label %ifend117

else116:                                          ; preds = %then108
  br label %ifend117

ifend117:                                         ; preds = %else116, %then115
  br label %ifend110
}

define void @_Screen_refresh_screenp(ptr %0) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %currentScreen = load ptr, ptr @currentScreen, align 8
  %ne = icmp ne ptr %currentScreen, null
  %bool = zext i1 %ne to i32
  %ifcond = icmp ne i32 %bool, 0
  br i1 %ifcond, label %then, label %else

then:                                             ; preds = %entry
  %self1 = load ptr, ptr %self, align 8
  call void @_Screen_set_screenp(ptr %self1)
  br label %ifend

else:                                             ; preds = %entry
  br label %ifend

ifend:                                            ; preds = %else, %then
  ret void
}

define void @_Screen_delete_screenp(ptr %0) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %selfptr = load ptr, ptr %self, align 8
  %field_slot = getelementptr %Screen, ptr %selfptr, i32 0, i32 0
  %delptr = load ptr, ptr %field_slot, align 8
  call void @_Shape_delete_shapep(ptr %delptr)
  call void @free(ptr %delptr)
  store ptr null, ptr %field_slot, align 8
  ret void
}

define void @_TextLabel_new_textlabelpcharpintintcharcharcharchar(ptr %0, ptr %1, i32 %2, i32 %3, i8 %4, i8 %5, i8 %6, i8 %7) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %txt = alloca ptr, align 8
  store ptr %1, ptr %txt, align 8
  %x = alloca i32, align 4
  store i32 %2, ptr %x, align 4
  %y = alloca i32, align 4
  store i32 %3, ptr %y, align 4
  %r = alloca i8, align 1
  store i8 %4, ptr %r, align 1
  %g = alloca i8, align 1
  store i8 %5, ptr %g, align 1
  %b = alloca i8, align 1
  store i8 %6, ptr %b, align 1
  %a = alloca i8, align 1
  store i8 %7, ptr %a, align 1
  %ptr = load ptr, ptr %self, align 8
  %gep = getelementptr %TextLabel, ptr %ptr, i32 0, i32 0
  %txt1 = load ptr, ptr %txt, align 8
  store ptr %txt1, ptr %gep, align 8
  %ptr2 = load ptr, ptr %self, align 8
  %gep3 = getelementptr %TextLabel, ptr %ptr2, i32 0, i32 1
  %x4 = load i32, ptr %x, align 4
  store i32 %x4, ptr %gep3, align 4
  %ptr5 = load ptr, ptr %self, align 8
  %gep6 = getelementptr %TextLabel, ptr %ptr5, i32 0, i32 2
  %y7 = load i32, ptr %y, align 4
  store i32 %y7, ptr %gep6, align 4
  %ptr8 = load ptr, ptr %self, align 8
  %field_gep = getelementptr %TextLabel, ptr %ptr8, i32 0, i32 3
  %elem_gep = getelementptr [4 x i8], ptr %field_gep, i32 0, i32 0
  %r9 = load i8, ptr %r, align 1
  store i8 %r9, ptr %elem_gep, align 1
  %ptr10 = load ptr, ptr %self, align 8
  %field_gep11 = getelementptr %TextLabel, ptr %ptr10, i32 0, i32 3
  %elem_gep12 = getelementptr [4 x i8], ptr %field_gep11, i32 0, i32 1
  %g13 = load i8, ptr %g, align 1
  store i8 %g13, ptr %elem_gep12, align 1
  %ptr14 = load ptr, ptr %self, align 8
  %field_gep15 = getelementptr %TextLabel, ptr %ptr14, i32 0, i32 3
  %elem_gep16 = getelementptr [4 x i8], ptr %field_gep15, i32 0, i32 2
  %b17 = load i8, ptr %b, align 1
  store i8 %b17, ptr %elem_gep16, align 1
  %ptr18 = load ptr, ptr %self, align 8
  %field_gep19 = getelementptr %TextLabel, ptr %ptr18, i32 0, i32 3
  %elem_gep20 = getelementptr [4 x i8], ptr %field_gep19, i32 0, i32 3
  %a21 = load i8, ptr %a, align 1
  store i8 %a21, ptr %elem_gep20, align 1
  ret void
}

define void @_TextLabel_draw_textlabelp(ptr %0) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %ptr = load ptr, ptr %self, align 8
  %gep = getelementptr %TextLabel, ptr %ptr, i32 0, i32 0
  %text = load ptr, ptr %gep, align 8
  %ptr1 = load ptr, ptr %self, align 8
  %gep2 = getelementptr %TextLabel, ptr %ptr1, i32 0, i32 1
  %x = load i32, ptr %gep2, align 4
  %ptr3 = load ptr, ptr %self, align 8
  %gep4 = getelementptr %TextLabel, ptr %ptr3, i32 0, i32 2
  %y = load i32, ptr %gep4, align 4
  %ptr5 = load ptr, ptr %self, align 8
  %field_gep = getelementptr %TextLabel, ptr %ptr5, i32 0, i32 3
  %elem_gep = getelementptr [4 x i8], ptr %field_gep, i32 0, i32 0
  %elem = load i8, ptr %elem_gep, align 1
  %zext = zext i8 %elem to i32
  %ptr6 = load ptr, ptr %self, align 8
  %field_gep7 = getelementptr %TextLabel, ptr %ptr6, i32 0, i32 3
  %elem_gep8 = getelementptr [4 x i8], ptr %field_gep7, i32 0, i32 1
  %elem9 = load i8, ptr %elem_gep8, align 1
  %zext10 = zext i8 %elem9 to i32
  %ptr11 = load ptr, ptr %self, align 8
  %field_gep12 = getelementptr %TextLabel, ptr %ptr11, i32 0, i32 3
  %elem_gep13 = getelementptr [4 x i8], ptr %field_gep12, i32 0, i32 2
  %elem14 = load i8, ptr %elem_gep13, align 1
  %zext15 = zext i8 %elem14 to i32
  %ptr16 = load ptr, ptr %self, align 8
  %field_gep17 = getelementptr %TextLabel, ptr %ptr16, i32 0, i32 3
  %elem_gep18 = getelementptr [4 x i8], ptr %field_gep17, i32 0, i32 3
  %elem19 = load i8, ptr %elem_gep18, align 1
  %zext20 = zext i8 %elem19 to i32
  call void @queueText(ptr %text, i32 %x, i32 %y, i32 %zext, i32 %zext10, i32 %zext15, i32 %zext20)
  ret void
}

define void @_TextLabel_move_textlabelpintint(ptr %0, i32 %1, i32 %2) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %nx = alloca i32, align 4
  store i32 %1, ptr %nx, align 4
  %ny = alloca i32, align 4
  store i32 %2, ptr %ny, align 4
  %ptr = load ptr, ptr %self, align 8
  %gep = getelementptr %TextLabel, ptr %ptr, i32 0, i32 1
  %nx1 = load i32, ptr %nx, align 4
  store i32 %nx1, ptr %gep, align 4
  %ptr2 = load ptr, ptr %self, align 8
  %gep3 = getelementptr %TextLabel, ptr %ptr2, i32 0, i32 2
  %ny4 = load i32, ptr %ny, align 4
  store i32 %ny4, ptr %gep3, align 4
  ret void
}

define void @_TextLabel_setColor_textlabelpcharcharcharchar(ptr %0, i8 %1, i8 %2, i8 %3, i8 %4) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %r = alloca i8, align 1
  store i8 %1, ptr %r, align 1
  %g = alloca i8, align 1
  store i8 %2, ptr %g, align 1
  %b = alloca i8, align 1
  store i8 %3, ptr %b, align 1
  %a = alloca i8, align 1
  store i8 %4, ptr %a, align 1
  %ptr = load ptr, ptr %self, align 8
  %field_gep = getelementptr %TextLabel, ptr %ptr, i32 0, i32 3
  %elem_gep = getelementptr [4 x i8], ptr %field_gep, i32 0, i32 0
  %r1 = load i8, ptr %r, align 1
  store i8 %r1, ptr %elem_gep, align 1
  %ptr2 = load ptr, ptr %self, align 8
  %field_gep3 = getelementptr %TextLabel, ptr %ptr2, i32 0, i32 3
  %elem_gep4 = getelementptr [4 x i8], ptr %field_gep3, i32 0, i32 1
  %g5 = load i8, ptr %g, align 1
  store i8 %g5, ptr %elem_gep4, align 1
  %ptr6 = load ptr, ptr %self, align 8
  %field_gep7 = getelementptr %TextLabel, ptr %ptr6, i32 0, i32 3
  %elem_gep8 = getelementptr [4 x i8], ptr %field_gep7, i32 0, i32 2
  %b9 = load i8, ptr %b, align 1
  store i8 %b9, ptr %elem_gep8, align 1
  %ptr10 = load ptr, ptr %self, align 8
  %field_gep11 = getelementptr %TextLabel, ptr %ptr10, i32 0, i32 3
  %elem_gep12 = getelementptr [4 x i8], ptr %field_gep11, i32 0, i32 3
  %a13 = load i8, ptr %a, align 1
  store i8 %a13, ptr %elem_gep12, align 1
  ret void
}

define void @_TextLabel_setText_textlabelpcharp(ptr %0, ptr %1) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %txt = alloca ptr, align 8
  store ptr %1, ptr %txt, align 8
  %ptr = load ptr, ptr %self, align 8
  %gep = getelementptr %TextLabel, ptr %ptr, i32 0, i32 0
  %txt1 = load ptr, ptr %txt, align 8
  store ptr %txt1, ptr %gep, align 8
  ret void
}

define void @_TextLabel_delete_textlabelp(ptr %0) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %selfptr = load ptr, ptr %self, align 8
  %field_slot = getelementptr %TextLabel, ptr %selfptr, i32 0, i32 0
  %delptr = load ptr, ptr %field_slot, align 8
  call void @free(ptr %delptr)
  store ptr null, ptr %field_slot, align 8
  ret void
}

define void @_drawShape_shapep(ptr %0) {
entry:
  %s = alloca ptr, align 8
  store ptr %0, ptr %s, align 8
  %x = alloca i32, align 4
  %ptr = load ptr, ptr %s, align 8
  %field_gep = getelementptr %Shape, ptr %ptr, i32 0, i32 3
  %elem_gep = getelementptr [2 x i32], ptr %field_gep, i32 0, i32 0
  %elem = load i32, ptr %elem_gep, align 4
  store i32 %elem, ptr %x, align 4
  %y = alloca i32, align 4
  %ptr1 = load ptr, ptr %s, align 8
  %field_gep2 = getelementptr %Shape, ptr %ptr1, i32 0, i32 3
  %elem_gep3 = getelementptr [2 x i32], ptr %field_gep2, i32 0, i32 1
  %elem4 = load i32, ptr %elem_gep3, align 4
  store i32 %elem4, ptr %y, align 4
  %w = alloca i32, align 4
  %ptr5 = load ptr, ptr %s, align 8
  %field_gep6 = getelementptr %Shape, ptr %ptr5, i32 0, i32 2
  %elem_gep7 = getelementptr [2 x i32], ptr %field_gep6, i32 0, i32 0
  %elem8 = load i32, ptr %elem_gep7, align 4
  store i32 %elem8, ptr %w, align 4
  %h = alloca i32, align 4
  %ptr9 = load ptr, ptr %s, align 8
  %field_gep10 = getelementptr %Shape, ptr %ptr9, i32 0, i32 2
  %elem_gep11 = getelementptr [2 x i32], ptr %field_gep10, i32 0, i32 1
  %elem12 = load i32, ptr %elem_gep11, align 4
  store i32 %elem12, ptr %h, align 4
  %r = alloca i8, align 1
  %ptr13 = load ptr, ptr %s, align 8
  %field_gep14 = getelementptr %Shape, ptr %ptr13, i32 0, i32 4
  %elem_gep15 = getelementptr [4 x i8], ptr %field_gep14, i32 0, i32 0
  %elem16 = load i8, ptr %elem_gep15, align 1
  store i8 %elem16, ptr %r, align 1
  %g = alloca i8, align 1
  %ptr17 = load ptr, ptr %s, align 8
  %field_gep18 = getelementptr %Shape, ptr %ptr17, i32 0, i32 4
  %elem_gep19 = getelementptr [4 x i8], ptr %field_gep18, i32 0, i32 1
  %elem20 = load i8, ptr %elem_gep19, align 1
  store i8 %elem20, ptr %g, align 1
  %b = alloca i8, align 1
  %ptr21 = load ptr, ptr %s, align 8
  %field_gep22 = getelementptr %Shape, ptr %ptr21, i32 0, i32 4
  %elem_gep23 = getelementptr [4 x i8], ptr %field_gep22, i32 0, i32 2
  %elem24 = load i8, ptr %elem_gep23, align 1
  store i8 %elem24, ptr %b, align 1
  %a = alloca i8, align 1
  %ptr25 = load ptr, ptr %s, align 8
  %field_gep26 = getelementptr %Shape, ptr %ptr25, i32 0, i32 4
  %elem_gep27 = getelementptr [4 x i8], ptr %field_gep26, i32 0, i32 3
  %elem28 = load i8, ptr %elem_gep27, align 1
  store i8 %elem28, ptr %a, align 1
  %vertices = alloca [6 x %Vertex], align 8
  store [6 x %Vertex] zeroinitializer, ptr %vertices, align 4
  %v0 = alloca ptr, align 8
  %newptr = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Vertex, ptr null, i32 1) to i64))
  store ptr %newptr, ptr %v0, align 8
  %x29 = load i32, ptr %x, align 4
  %y30 = load i32, ptr %y, align 4
  %r31 = load i8, ptr %r, align 1
  %g32 = load i8, ptr %g, align 1
  %b33 = load i8, ptr %b, align 1
  %a34 = load i8, ptr %a, align 1
  call void @_Vertex_new_vertexpintintcharcharcharchar(ptr %newptr, i32 %x29, i32 %y30, i8 %r31, i8 %g32, i8 %b33, i8 %a34)
  %v1 = alloca ptr, align 8
  %newptr35 = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Vertex, ptr null, i32 1) to i64))
  store ptr %newptr35, ptr %v1, align 8
  %x36 = load i32, ptr %x, align 4
  %w37 = load i32, ptr %w, align 4
  %add = add i32 %x36, %w37
  %y38 = load i32, ptr %y, align 4
  %r39 = load i8, ptr %r, align 1
  %g40 = load i8, ptr %g, align 1
  %b41 = load i8, ptr %b, align 1
  %a42 = load i8, ptr %a, align 1
  call void @_Vertex_new_vertexpintintcharcharcharchar(ptr %newptr35, i32 %add, i32 %y38, i8 %r39, i8 %g40, i8 %b41, i8 %a42)
  %v2 = alloca ptr, align 8
  %newptr43 = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Vertex, ptr null, i32 1) to i64))
  store ptr %newptr43, ptr %v2, align 8
  %x44 = load i32, ptr %x, align 4
  %w45 = load i32, ptr %w, align 4
  %add46 = add i32 %x44, %w45
  %y47 = load i32, ptr %y, align 4
  %h48 = load i32, ptr %h, align 4
  %add49 = add i32 %y47, %h48
  %r50 = load i8, ptr %r, align 1
  %g51 = load i8, ptr %g, align 1
  %b52 = load i8, ptr %b, align 1
  %a53 = load i8, ptr %a, align 1
  call void @_Vertex_new_vertexpintintcharcharcharchar(ptr %newptr43, i32 %add46, i32 %add49, i8 %r50, i8 %g51, i8 %b52, i8 %a53)
  %v3 = alloca ptr, align 8
  %newptr54 = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Vertex, ptr null, i32 1) to i64))
  store ptr %newptr54, ptr %v3, align 8
  %x55 = load i32, ptr %x, align 4
  %y56 = load i32, ptr %y, align 4
  %r57 = load i8, ptr %r, align 1
  %g58 = load i8, ptr %g, align 1
  %b59 = load i8, ptr %b, align 1
  %a60 = load i8, ptr %a, align 1
  call void @_Vertex_new_vertexpintintcharcharcharchar(ptr %newptr54, i32 %x55, i32 %y56, i8 %r57, i8 %g58, i8 %b59, i8 %a60)
  %v4 = alloca ptr, align 8
  %newptr61 = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Vertex, ptr null, i32 1) to i64))
  store ptr %newptr61, ptr %v4, align 8
  %x62 = load i32, ptr %x, align 4
  %w63 = load i32, ptr %w, align 4
  %add64 = add i32 %x62, %w63
  %y65 = load i32, ptr %y, align 4
  %h66 = load i32, ptr %h, align 4
  %add67 = add i32 %y65, %h66
  %r68 = load i8, ptr %r, align 1
  %g69 = load i8, ptr %g, align 1
  %b70 = load i8, ptr %b, align 1
  %a71 = load i8, ptr %a, align 1
  call void @_Vertex_new_vertexpintintcharcharcharchar(ptr %newptr61, i32 %add64, i32 %add67, i8 %r68, i8 %g69, i8 %b70, i8 %a71)
  %v5 = alloca ptr, align 8
  %newptr72 = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Vertex, ptr null, i32 1) to i64))
  store ptr %newptr72, ptr %v5, align 8
  %x73 = load i32, ptr %x, align 4
  %y74 = load i32, ptr %y, align 4
  %h75 = load i32, ptr %h, align 4
  %add76 = add i32 %y74, %h75
  %r77 = load i8, ptr %r, align 1
  %g78 = load i8, ptr %g, align 1
  %b79 = load i8, ptr %b, align 1
  %a80 = load i8, ptr %a, align 1
  call void @_Vertex_new_vertexpintintcharcharcharchar(ptr %newptr72, i32 %x73, i32 %add76, i8 %r77, i8 %g78, i8 %b79, i8 %a80)
  %gep = getelementptr [6 x %Vertex], ptr %vertices, i32 0, i32 0
  %deref_ptr = load ptr, ptr %v0, align 8
  %deref = load %Vertex, ptr %deref_ptr, align 4
  store %Vertex %deref, ptr %gep, align 4
  %gep81 = getelementptr [6 x %Vertex], ptr %vertices, i32 0, i32 1
  %deref_ptr82 = load ptr, ptr %v1, align 8
  %deref83 = load %Vertex, ptr %deref_ptr82, align 4
  store %Vertex %deref83, ptr %gep81, align 4
  %gep84 = getelementptr [6 x %Vertex], ptr %vertices, i32 0, i32 2
  %deref_ptr85 = load ptr, ptr %v2, align 8
  %deref86 = load %Vertex, ptr %deref_ptr85, align 4
  store %Vertex %deref86, ptr %gep84, align 4
  %gep87 = getelementptr [6 x %Vertex], ptr %vertices, i32 0, i32 3
  %deref_ptr88 = load ptr, ptr %v3, align 8
  %deref89 = load %Vertex, ptr %deref_ptr88, align 4
  store %Vertex %deref89, ptr %gep87, align 4
  %gep90 = getelementptr [6 x %Vertex], ptr %vertices, i32 0, i32 4
  %deref_ptr91 = load ptr, ptr %v4, align 8
  %deref92 = load %Vertex, ptr %deref_ptr91, align 4
  store %Vertex %deref92, ptr %gep90, align 4
  %gep93 = getelementptr [6 x %Vertex], ptr %vertices, i32 0, i32 5
  %deref_ptr94 = load ptr, ptr %v5, align 8
  %deref95 = load %Vertex, ptr %deref_ptr94, align 4
  store %Vertex %deref95, ptr %gep93, align 4
  call void @destroyVertexBuffer()
  %arrptr = getelementptr [6 x %Vertex], ptr %vertices, i32 0
  call void @createVertexBuffer(ptr %arrptr, i32 72)
  ret void
}

define void @_drawVertices_vertexpint(ptr %0, i32 %1) {
entry:
  %vertices = alloca ptr, align 8
  store ptr %0, ptr %vertices, align 8
  %count = alloca i32, align 4
  store i32 %1, ptr %count, align 4
  %size = alloca i32, align 4
  %count1 = load i32, ptr %count, align 4
  %mul = mul i32 %count1, 12
  store i32 %mul, ptr %size, align 4
  call void @destroyVertexBuffer()
  %vertices2 = load ptr, ptr %vertices, align 8
  %size3 = load i32, ptr %size, align 4
  call void @createVertexBuffer(ptr %vertices2, i32 %size3)
  ret void
}

define i32 @main() {
entry:
  %win = alloca ptr, align 8
  %call = call ptr @openWindow(ptr @str, i32 600, i32 800)
  store ptr %call, ptr %win, align 8
  %s = alloca ptr, align 8
  %newptr = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Shape, ptr null, i32 1) to i64))
  store ptr %newptr, ptr %s, align 8
  %Circle = load i32, ptr @Circle.4, align 4
  call void @_Shape_new_shapepintintintintintcharcharcharchar(ptr %newptr, i32 %Circle, i32 0, i32 0, i32 200, i32 200, i8 -1, i8 -1, i8 0, i8 -1)
  %shapes = alloca [1 x %Shape], align 8
  store [1 x %Shape] zeroinitializer, ptr %shapes, align 8
  %gep = getelementptr [1 x %Shape], ptr %shapes, i32 0, i32 0
  %deref_ptr = load ptr, ptr %s, align 8
  %deref = load %Shape, ptr %deref_ptr, align 8
  store %Shape %deref, ptr %gep, align 8
  %screen = alloca ptr, align 8
  %newptr1 = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Screen, ptr null, i32 1) to i64))
  store ptr %newptr1, ptr %screen, align 8
  call void @_Screen_new_screenpshapepintintintintint(ptr %newptr1, ptr %shapes, i32 1, i32 255, i32 0, i32 0, i32 255)
  %screen2 = load ptr, ptr %screen, align 8
  call void @_Screen_set_screenp(ptr %screen2)
  %screen3 = load ptr, ptr %screen, align 8
  call void @_Screen_refresh_screenp(ptr %screen3)
  br label %wcond

wcond:                                            ; preds = %ifend, %entry
  br i1 true, label %wbody, label %wend

wbody:                                            ; preds = %wcond
  %call4 = call i32 @appEvent()
  %Quit = load i32, ptr @Quit.2, align 4
  %eq = icmp eq i32 %call4, %Quit
  %bool = zext i1 %eq to i32
  %ifcond = icmp ne i32 %bool, 0
  br i1 %ifcond, label %then, label %else

wend:                                             ; preds = %wcond
  ret i32 0

then:                                             ; preds = %wbody
  %win5 = load ptr, ptr %win, align 8
  call void @closeWindow(ptr %win5)
  ret i32 0

else:                                             ; preds = %wbody
  br label %ifend

ifend:                                            ; preds = %else
  br label %wcond
}
