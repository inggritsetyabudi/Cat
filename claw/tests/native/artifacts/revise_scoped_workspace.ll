; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
%"src.views::PairView" = type { %claw.slice, %claw.slice }

@.str.1 = private unnamed_addr constant [6 x i8] c"scope\00"

@.str.0 = private unnamed_addr constant [10 x i8] c"workspace\00"

declare void @"claw.runtime.println.slice"(ptr)

define internal void @"main::main"() {
entry:
  %first.addr = alloca %claw.slice, align 8
  %str.ptr.0 = getelementptr inbounds [10 x i8], ptr @.str.0, i64 0, i64 0
  %str.slice.1 = insertvalue %claw.slice poison, ptr %str.ptr.0, 0
  %str.slice.2 = insertvalue %claw.slice %str.slice.1, i64 9, 1
  store %claw.slice %str.slice.2, ptr %first.addr, align 8
  %second.addr = alloca %claw.slice, align 8
  %str.ptr.3 = getelementptr inbounds [6 x i8], ptr @.str.1, i64 0, i64 0
  %str.slice.4 = insertvalue %claw.slice poison, ptr %str.ptr.3, 0
  %str.slice.5 = insertvalue %claw.slice %str.slice.4, i64 5, 1
  store %claw.slice %str.slice.5, ptr %second.addr, align 8
  br label %scope_actual_0
scope_actual_0:
  %view.addr = alloca %claw.slice, align 8
  %load.6 = load %claw.slice, ptr %first.addr, align 8
  store %claw.slice %load.6, ptr %view.addr, align 8
  %load.7 = load %claw.slice, ptr %view.addr, align 8
  %t0 = call %claw.slice @"src.views::identity_ref"(%claw.slice %load.7)
  %same_view.addr = alloca %claw.slice, align 8
  store %claw.slice %t0, ptr %same_view.addr, align 8
  %t1.addr = alloca %"src.views::PairView", align 8
  %field.ptr.8 = getelementptr inbounds %"src.views::PairView", ptr %t1.addr, i32 0, i32 0
  %load.9 = load %claw.slice, ptr %same_view.addr, align 8
  store %claw.slice %load.9, ptr %field.ptr.8, align 8
  %field.ptr.10 = getelementptr inbounds %"src.views::PairView", ptr %t1.addr, i32 0, i32 1
  %load.11 = load %claw.slice, ptr %second.addr, align 8
  store %claw.slice %load.11, ptr %field.ptr.10, align 8
  %pair.addr = alloca %"src.views::PairView", align 8
  %load.12 = load %"src.views::PairView", ptr %t1.addr, align 8
  store %"src.views::PairView" %load.12, ptr %pair.addr, align 8
  %load.13 = load %"src.views::PairView", ptr %pair.addr, align 8
  %t2 = call %"src.views::PairView" @"src.views::identity_pair"(%"src.views::PairView" %load.13)
  %same_pair.addr = alloca %"src.views::PairView", align 8
  store %"src.views::PairView" %t2, ptr %same_pair.addr, align 8
  %load.14 = load %"src.views::PairView", ptr %same_pair.addr, align 8
  %t3 = call %claw.slice @"src.views::first_text"(%"src.views::PairView" %load.14)
  %first_view.addr = alloca %claw.slice, align 8
  store %claw.slice %t3, ptr %first_view.addr, align 8
  call void @"claw.runtime.println.slice"(ptr %first_view.addr)
  br label %scope_cont_1
scope_cont_1:
  ; drop second : Str
  ; drop first : Str
  ret void
}

define %"src.views::PairView" @"src.views::identity_pair"(%"src.views::PairView" %arg.value) {
entry:
  ret %"src.views::PairView" %arg.value
}

define %claw.slice @"src.views::identity_ref"(%claw.slice %arg.value) {
entry:
  ret %claw.slice %arg.value
}

define %claw.slice @"src.views::first_text"(%"src.views::PairView" %arg.value) {
entry:
  %addr.0 = alloca %"src.views::PairView", align 8
  store %"src.views::PairView" %arg.value, ptr %addr.0, align 8
  %field.ptr.1 = getelementptr inbounds %"src.views::PairView", ptr %addr.0, i32 0, i32 0
  %t0 = load %claw.slice, ptr %field.ptr.1, align 8
  ; drop value : PairView[borrow]
  ret %claw.slice %t0
}

define i32 @main() {
entry:
  call void @"main::main"()
  ret i32 0
}
