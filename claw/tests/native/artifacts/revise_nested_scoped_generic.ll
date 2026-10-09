; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
%"revise_nested_scoped_generic::Name" = type { %claw.slice }

@.str.1 = private unnamed_addr constant [5 x i8] c"beta\00"

@.str.0 = private unnamed_addr constant [6 x i8] c"alpha\00"

declare void @"claw.runtime.println.slice"(ptr)

define internal void @"revise_nested_scoped_generic::identity_box"(ptr %ret.slot, ptr %arg.value.addr) {
entry:
  %load.0 = load { %"revise_nested_scoped_generic::Name", i128 }, ptr %arg.value.addr, align 16
  store { %"revise_nested_scoped_generic::Name", i128 } %load.0, ptr %ret.slot, align 16
  ret void
}

define internal void @"revise_nested_scoped_generic::choose_box"(ptr %ret.slot, ptr %arg.left.addr, ptr %arg.right.addr) {
entry:
  ; drop right : Box[Name[borrow]]
  %load.0 = load { %"revise_nested_scoped_generic::Name", i128 }, ptr %arg.left.addr, align 16
  store { %"revise_nested_scoped_generic::Name", i128 } %load.0, ptr %ret.slot, align 16
  ret void
}

define internal %claw.slice @"revise_nested_scoped_generic::first_text"(ptr %arg.value.addr) {
entry:
  %field.ptr.0 = getelementptr inbounds { %"revise_nested_scoped_generic::Name", i128 }, ptr %arg.value.addr, i32 0, i32 0
  %t0 = load %"revise_nested_scoped_generic::Name", ptr %field.ptr.0, align 8
  %addr.1 = alloca %"revise_nested_scoped_generic::Name", align 8
  store %"revise_nested_scoped_generic::Name" %t0, ptr %addr.1, align 8
  %field.ptr.2 = getelementptr inbounds %"revise_nested_scoped_generic::Name", ptr %addr.1, i32 0, i32 0
  %t1 = load %claw.slice, ptr %field.ptr.2, align 8
  ; drop value : Box[Name[borrow]]
  ret %claw.slice %t1
}

define internal void @"revise_nested_scoped_generic::main"() {
entry:
  %source.addr = alloca %claw.slice, align 8
  %str.ptr.0 = getelementptr inbounds [6 x i8], ptr @.str.0, i64 0, i64 0
  %str.slice.1 = insertvalue %claw.slice poison, ptr %str.ptr.0, 0
  %str.slice.2 = insertvalue %claw.slice %str.slice.1, i64 5, 1
  store %claw.slice %str.slice.2, ptr %source.addr, align 8
  %second.addr = alloca %claw.slice, align 8
  %str.ptr.3 = getelementptr inbounds [5 x i8], ptr @.str.1, i64 0, i64 0
  %str.slice.4 = insertvalue %claw.slice poison, ptr %str.ptr.3, 0
  %str.slice.5 = insertvalue %claw.slice %str.slice.4, i64 4, 1
  store %claw.slice %str.slice.5, ptr %second.addr, align 8
  br label %scope_s_0
scope_s_0:
  %t0.addr = alloca %"revise_nested_scoped_generic::Name", align 8
  %field.ptr.6 = getelementptr inbounds %"revise_nested_scoped_generic::Name", ptr %t0.addr, i32 0, i32 0
  %load.7 = load %claw.slice, ptr %source.addr, align 8
  store %claw.slice %load.7, ptr %field.ptr.6, align 8
  %name.addr = alloca %"revise_nested_scoped_generic::Name", align 8
  %load.8 = load %"revise_nested_scoped_generic::Name", ptr %t0.addr, align 8
  store %"revise_nested_scoped_generic::Name" %load.8, ptr %name.addr, align 8
  %t1.addr = alloca %"revise_nested_scoped_generic::Name", align 8
  %field.ptr.9 = getelementptr inbounds %"revise_nested_scoped_generic::Name", ptr %t1.addr, i32 0, i32 0
  %load.10 = load %claw.slice, ptr %second.addr, align 8
  store %claw.slice %load.10, ptr %field.ptr.9, align 8
  %other_name.addr = alloca %"revise_nested_scoped_generic::Name", align 8
  %load.11 = load %"revise_nested_scoped_generic::Name", ptr %t1.addr, align 8
  store %"revise_nested_scoped_generic::Name" %load.11, ptr %other_name.addr, align 8
  %t2.addr = alloca { %"revise_nested_scoped_generic::Name", i128 }, align 16
  %field.ptr.12 = getelementptr inbounds { %"revise_nested_scoped_generic::Name", i128 }, ptr %t2.addr, i32 0, i32 0
  %load.13 = load %"revise_nested_scoped_generic::Name", ptr %name.addr, align 8
  store %"revise_nested_scoped_generic::Name" %load.13, ptr %field.ptr.12, align 8
  %field.ptr.14 = getelementptr inbounds { %"revise_nested_scoped_generic::Name", i128 }, ptr %t2.addr, i32 0, i32 1
  store i128 7, ptr %field.ptr.14, align 16
  %box.addr = alloca { %"revise_nested_scoped_generic::Name", i128 }, align 16
  %load.15 = load { %"revise_nested_scoped_generic::Name", i128 }, ptr %t2.addr, align 16
  store { %"revise_nested_scoped_generic::Name", i128 } %load.15, ptr %box.addr, align 16
  %t3.addr = alloca { %"revise_nested_scoped_generic::Name", i128 }, align 16
  %field.ptr.16 = getelementptr inbounds { %"revise_nested_scoped_generic::Name", i128 }, ptr %t3.addr, i32 0, i32 0
  %load.17 = load %"revise_nested_scoped_generic::Name", ptr %other_name.addr, align 8
  store %"revise_nested_scoped_generic::Name" %load.17, ptr %field.ptr.16, align 8
  %field.ptr.18 = getelementptr inbounds { %"revise_nested_scoped_generic::Name", i128 }, ptr %t3.addr, i32 0, i32 1
  store i128 9, ptr %field.ptr.18, align 16
  %other_box.addr = alloca { %"revise_nested_scoped_generic::Name", i128 }, align 16
  %load.19 = load { %"revise_nested_scoped_generic::Name", i128 }, ptr %t3.addr, align 16
  store { %"revise_nested_scoped_generic::Name", i128 } %load.19, ptr %other_box.addr, align 16
  %call.ret.addr.20 = alloca { %"revise_nested_scoped_generic::Name", i128 }, align 16
  call void @"revise_nested_scoped_generic::identity_box"(ptr %call.ret.addr.20, ptr %box.addr)
  %same.addr = alloca { %"revise_nested_scoped_generic::Name", i128 }, align 16
  %load.21 = load { %"revise_nested_scoped_generic::Name", i128 }, ptr %call.ret.addr.20, align 16
  store { %"revise_nested_scoped_generic::Name", i128 } %load.21, ptr %same.addr, align 16
  %call.ret.addr.22 = alloca { %"revise_nested_scoped_generic::Name", i128 }, align 16
  call void @"revise_nested_scoped_generic::choose_box"(ptr %call.ret.addr.22, ptr %same.addr, ptr %other_box.addr)
  %selected.addr = alloca { %"revise_nested_scoped_generic::Name", i128 }, align 16
  %load.23 = load { %"revise_nested_scoped_generic::Name", i128 }, ptr %call.ret.addr.22, align 16
  store { %"revise_nested_scoped_generic::Name", i128 } %load.23, ptr %selected.addr, align 16
  %t6 = call %claw.slice @"revise_nested_scoped_generic::first_text"(ptr %selected.addr)
  %text.addr = alloca %claw.slice, align 8
  store %claw.slice %t6, ptr %text.addr, align 8
  call void @"claw.runtime.println.slice"(ptr %text.addr)
  br label %scope_cont_1
scope_cont_1:
  ; drop second : Str
  ; drop source : Str
  ret void
}

define i32 @main() {
entry:
  call void @"revise_nested_scoped_generic::main"()
  ret i32 0
}
