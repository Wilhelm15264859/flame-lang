; ModuleID = 'flame'
source_filename = "flame"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%Vertex = type { [2 x i32], [4 x i8] }
%Shape = type { ptr, i32, [2 x i32], [2 x i32], [4 x i8] }

@str = private unnamed_addr constant [6 x i8] c"First\00", align 1

declare ptr @malloc(i64)

declare void @free(ptr)

define void @Vertex_new(ptr %0, i32 %1, i32 %2, i8 %3, i8 %4, i8 %5, i8 %6) {
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

define void @Shape_new(ptr %0, i32 %1, i32 %2, i32 %3, i32 %4, i8 %5, i8 %6, i8 %7, i8 %8) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  %x = alloca i32, align 4
  store i32 %1, ptr %x, align 4
  %y = alloca i32, align 4
  store i32 %2, ptr %y, align 4
  %w = alloca i32, align 4
  store i32 %3, ptr %w, align 4
  %h = alloca i32, align 4
  store i32 %4, ptr %h, align 4
  %r = alloca i8, align 1
  store i8 %5, ptr %r, align 1
  %g = alloca i8, align 1
  store i8 %6, ptr %g, align 1
  %b = alloca i8, align 1
  store i8 %7, ptr %b, align 1
  %a = alloca i8, align 1
  store i8 %8, ptr %a, align 1
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
  %field_gep7 = getelementptr %Shape, ptr %ptr6, i32 0, i32 2
  %elem_gep8 = getelementptr [2 x i32], ptr %field_gep7, i32 0, i32 0
  %w9 = load i32, ptr %w, align 4
  store i32 %w9, ptr %elem_gep8, align 4
  %ptr10 = load ptr, ptr %self, align 8
  %field_gep11 = getelementptr %Shape, ptr %ptr10, i32 0, i32 2
  %elem_gep12 = getelementptr [2 x i32], ptr %field_gep11, i32 0, i32 1
  %h13 = load i32, ptr %h, align 4
  store i32 %h13, ptr %elem_gep12, align 4
  %ptr14 = load ptr, ptr %self, align 8
  %field_gep15 = getelementptr %Shape, ptr %ptr14, i32 0, i32 4
  %elem_gep16 = getelementptr [4 x i8], ptr %field_gep15, i32 0, i32 0
  %r17 = load i8, ptr %r, align 1
  store i8 %r17, ptr %elem_gep16, align 1
  %ptr18 = load ptr, ptr %self, align 8
  %field_gep19 = getelementptr %Shape, ptr %ptr18, i32 0, i32 4
  %elem_gep20 = getelementptr [4 x i8], ptr %field_gep19, i32 0, i32 1
  %g21 = load i8, ptr %g, align 1
  store i8 %g21, ptr %elem_gep20, align 1
  %ptr22 = load ptr, ptr %self, align 8
  %field_gep23 = getelementptr %Shape, ptr %ptr22, i32 0, i32 4
  %elem_gep24 = getelementptr [4 x i8], ptr %field_gep23, i32 0, i32 2
  %b25 = load i8, ptr %b, align 1
  store i8 %b25, ptr %elem_gep24, align 1
  %ptr26 = load ptr, ptr %self, align 8
  %field_gep27 = getelementptr %Shape, ptr %ptr26, i32 0, i32 4
  %elem_gep28 = getelementptr [4 x i8], ptr %field_gep27, i32 0, i32 3
  %a29 = load i8, ptr %a, align 1
  store i8 %a29, ptr %elem_gep28, align 1
  ret void
}

define void @_Shape_delete_shapep(ptr %0) {
entry:
  %self = alloca ptr, align 8
  store ptr %0, ptr %self, align 8
  ret void
}

declare void @createVertexBuffer(ptr, i32)

declare void @graphicInit(ptr)

declare void @drawFrame()

declare i32 @appEvent()

declare ptr @openWindow(ptr, i32, i32)

declare void @closeWindow(ptr)

declare void @destroyVertexBuffer()

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
  call void @Vertex_new(ptr %newptr, i32 %x29, i32 %y30, i8 %r31, i8 %g32, i8 %b33, i8 %a34)
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
  call void @Vertex_new(ptr %newptr35, i32 %add, i32 %y38, i8 %r39, i8 %g40, i8 %b41, i8 %a42)
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
  call void @Vertex_new(ptr %newptr43, i32 %add46, i32 %add49, i8 %r50, i8 %g51, i8 %b52, i8 %a53)
  %v3 = alloca ptr, align 8
  %newptr54 = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Vertex, ptr null, i32 1) to i64))
  store ptr %newptr54, ptr %v3, align 8
  %x55 = load i32, ptr %x, align 4
  %y56 = load i32, ptr %y, align 4
  %r57 = load i8, ptr %r, align 1
  %g58 = load i8, ptr %g, align 1
  %b59 = load i8, ptr %b, align 1
  %a60 = load i8, ptr %a, align 1
  call void @Vertex_new(ptr %newptr54, i32 %x55, i32 %y56, i8 %r57, i8 %g58, i8 %b59, i8 %a60)
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
  call void @Vertex_new(ptr %newptr61, i32 %add64, i32 %add67, i8 %r68, i8 %g69, i8 %b70, i8 %a71)
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
  call void @Vertex_new(ptr %newptr72, i32 %x73, i32 %add76, i8 %r77, i8 %g78, i8 %b79, i8 %a80)
  %gep = getelementptr [6 x %Vertex], ptr %vertices, i32 0, i32 0
  %deref_ptr = load ptr, ptr %v0, align 8
  %deref = load %Vertex, ptr %deref_ptr, align 4
  store %Vertex %deref, ptr %gep, align 4
  %delptr = load ptr, ptr %v0, align 8
  call void @free(ptr %delptr)
  %gep81 = getelementptr [6 x %Vertex], ptr %vertices, i32 0, i32 1
  %deref_ptr82 = load ptr, ptr %v1, align 8
  %deref83 = load %Vertex, ptr %deref_ptr82, align 4
  store %Vertex %deref83, ptr %gep81, align 4
  %delptr84 = load ptr, ptr %v1, align 8
  call void @free(ptr %delptr84)
  %gep85 = getelementptr [6 x %Vertex], ptr %vertices, i32 0, i32 2
  %deref_ptr86 = load ptr, ptr %v2, align 8
  %deref87 = load %Vertex, ptr %deref_ptr86, align 4
  store %Vertex %deref87, ptr %gep85, align 4
  %delptr88 = load ptr, ptr %v2, align 8
  call void @free(ptr %delptr88)
  %gep89 = getelementptr [6 x %Vertex], ptr %vertices, i32 0, i32 3
  %deref_ptr90 = load ptr, ptr %v3, align 8
  %deref91 = load %Vertex, ptr %deref_ptr90, align 4
  store %Vertex %deref91, ptr %gep89, align 4
  %delptr92 = load ptr, ptr %v3, align 8
  call void @free(ptr %delptr92)
  %gep93 = getelementptr [6 x %Vertex], ptr %vertices, i32 0, i32 4
  %deref_ptr94 = load ptr, ptr %v4, align 8
  %deref95 = load %Vertex, ptr %deref_ptr94, align 4
  store %Vertex %deref95, ptr %gep93, align 4
  %delptr96 = load ptr, ptr %v4, align 8
  call void @free(ptr %delptr96)
  %gep97 = getelementptr [6 x %Vertex], ptr %vertices, i32 0, i32 5
  %deref_ptr98 = load ptr, ptr %v5, align 8
  %deref99 = load %Vertex, ptr %deref_ptr98, align 4
  store %Vertex %deref99, ptr %gep97, align 4
  %delptr100 = load ptr, ptr %v5, align 8
  call void @free(ptr %delptr100)
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
  %win1 = load ptr, ptr %win, align 8
  call void @graphicInit(ptr %win1)
  %vertices = alloca [3 x %Vertex], align 8
  store [3 x %Vertex] zeroinitializer, ptr %vertices, align 4
  %gep = getelementptr [3 x %Vertex], ptr %vertices, i32 0, i32 0
  %newptr = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Vertex, ptr null, i32 1) to i64))
  call void @Vertex_new(ptr %newptr, i32 0, i32 100, i8 -1, i8 0, i8 0, i8 -1)
  %newval = load %Vertex, ptr %newptr, align 4
  store %Vertex %newval, ptr %gep, align 4
  call void @free(ptr %newptr)
  %gep2 = getelementptr [3 x %Vertex], ptr %vertices, i32 0, i32 1
  %newptr3 = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Vertex, ptr null, i32 1) to i64))
  call void @Vertex_new(ptr %newptr3, i32 100, i32 0, i8 0, i8 -1, i8 0, i8 -1)
  %newval4 = load %Vertex, ptr %newptr3, align 4
  store %Vertex %newval4, ptr %gep2, align 4
  call void @free(ptr %newptr3)
  %gep5 = getelementptr [3 x %Vertex], ptr %vertices, i32 0, i32 2
  %newptr6 = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Vertex, ptr null, i32 1) to i64))
  call void @Vertex_new(ptr %newptr6, i32 -100, i32 0, i8 0, i8 0, i8 -1, i8 -1)
  %newval7 = load %Vertex, ptr %newptr6, align 4
  store %Vertex %newval7, ptr %gep5, align 4
  call void @free(ptr %newptr6)
  br label %wcond

wcond:                                            ; preds = %ifend, %entry
  br i1 true, label %wbody, label %wend

wbody:                                            ; preds = %wcond
  %e = alloca i32, align 4
  %call8 = call i32 @appEvent()
  store i32 %call8, ptr %e, align 4
  %e9 = load i32, ptr %e, align 4
  %eq = icmp eq i32 %e9, 1
  %bool = zext i1 %eq to i32
  %ifcond = icmp ne i32 %bool, 0
  br i1 %ifcond, label %then, label %else

wend:                                             ; preds = %wcond
  %delptr = load ptr, ptr %win, align 8
  call void @free(ptr %delptr)
  ret i32 0

then:                                             ; preds = %wbody
  %win10 = load ptr, ptr %win, align 8
  call void @closeWindow(ptr %win10)
  ret i32 0

else:                                             ; preds = %wbody
  br label %ifend

ifend:                                            ; preds = %else
  %arrptr = getelementptr [3 x %Vertex], ptr %vertices, i32 0
  call void @_drawVertices_vertexpint(ptr %arrptr, i32 3)
  call void @drawFrame()
  br label %wcond
}
