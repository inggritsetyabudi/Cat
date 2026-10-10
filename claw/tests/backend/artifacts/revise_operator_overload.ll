; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
%"revise_operator_overload::Vector2" = type { i32, i32 }

declare void @"claw.runtime.println.i32"(i32)
declare void @"claw.runtime.println.i1"(i1)

define internal %"revise_operator_overload::Vector2" @"revise_operator_overload::Vector2.add"(ptr %arg.self.addr, ptr %arg.other.addr) {
entry:
  %t0.addr = alloca %"revise_operator_overload::Vector2", align 4
  %field.ptr.0 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %arg.self.addr, i32 0, i32 0
  %t1 = load i32, ptr %field.ptr.0, align 4
  %field.ptr.1 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %arg.other.addr, i32 0, i32 0
  %t2 = load i32, ptr %field.ptr.1, align 4
  %t3 = add i32 %t1, %t2
  %field.ptr.2 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %t0.addr, i32 0, i32 0
  store i32 %t3, ptr %field.ptr.2, align 4
  %field.ptr.3 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %arg.self.addr, i32 0, i32 1
  %t4 = load i32, ptr %field.ptr.3, align 4
  %field.ptr.4 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %arg.other.addr, i32 0, i32 1
  %t5 = load i32, ptr %field.ptr.4, align 4
  %t6 = add i32 %t4, %t5
  %field.ptr.5 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %t0.addr, i32 0, i32 1
  store i32 %t6, ptr %field.ptr.5, align 4
  %load.6 = load %"revise_operator_overload::Vector2", ptr %t0.addr, align 4
  ret %"revise_operator_overload::Vector2" %load.6
}

define internal i1 @"revise_operator_overload::Vector2.equal"(ptr %arg.self.addr, ptr %arg.other.addr) {
entry:
  %field.ptr.0 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %arg.self.addr, i32 0, i32 0
  %t0 = load i32, ptr %field.ptr.0, align 4
  %field.ptr.1 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %arg.other.addr, i32 0, i32 0
  %t1 = load i32, ptr %field.ptr.1, align 4
  %t2 = icmp eq i32 %t0, %t1
  br i1 %t2, label %when_then_0, label %when_join_1
when_then_0:
  %field.ptr.2 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %arg.self.addr, i32 0, i32 1
  %t3 = load i32, ptr %field.ptr.2, align 4
  %field.ptr.3 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %arg.other.addr, i32 0, i32 1
  %t4 = load i32, ptr %field.ptr.3, align 4
  %t5 = icmp eq i32 %t3, %t4
  br i1 %t5, label %when_then_2, label %when_join_3
when_then_2:
  ret i1 true
when_join_3:
  br label %when_join_1
when_join_1:
  ; phi %phi0 omitted in initial LLVM lowering
  ret i1 false
}

define internal i32 @"revise_operator_overload::main"() {
entry:
  %t0.addr = alloca %"revise_operator_overload::Vector2", align 4
  %field.ptr.0 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %t0.addr, i32 0, i32 0
  store i32 10, ptr %field.ptr.0, align 4
  %field.ptr.1 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %t0.addr, i32 0, i32 1
  store i32 20, ptr %field.ptr.1, align 4
  %v1.addr = alloca %"revise_operator_overload::Vector2", align 4
  %load.2 = load %"revise_operator_overload::Vector2", ptr %t0.addr, align 4
  store %"revise_operator_overload::Vector2" %load.2, ptr %v1.addr, align 4
  %t1.addr = alloca %"revise_operator_overload::Vector2", align 4
  %field.ptr.3 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %t1.addr, i32 0, i32 0
  store i32 5, ptr %field.ptr.3, align 4
  %field.ptr.4 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %t1.addr, i32 0, i32 1
  store i32 15, ptr %field.ptr.4, align 4
  %v2.addr = alloca %"revise_operator_overload::Vector2", align 4
  %load.5 = load %"revise_operator_overload::Vector2", ptr %t1.addr, align 4
  store %"revise_operator_overload::Vector2" %load.5, ptr %v2.addr, align 4
  %t2 = call %"revise_operator_overload::Vector2" @"revise_operator_overload::Vector2.add"(ptr %v1.addr, ptr %v2.addr)
  %v3.addr = alloca %"revise_operator_overload::Vector2", align 4
  store %"revise_operator_overload::Vector2" %t2, ptr %v3.addr, align 4
  %field.ptr.6 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %v3.addr, i32 0, i32 0
  %t3 = load i32, ptr %field.ptr.6, align 4
  call void @"claw.runtime.println.i32"(i32 %t3)
  %field.ptr.7 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %v3.addr, i32 0, i32 1
  %t4 = load i32, ptr %field.ptr.7, align 4
  call void @"claw.runtime.println.i32"(i32 %t4)
  %t5 = call i1 @"revise_operator_overload::Vector2.equal"(ptr %v1.addr, ptr %v2.addr)
  %is_eq.addr = alloca i1, align 1
  store i1 %t5, ptr %is_eq.addr, align 1
  %load.8 = load i1, ptr %is_eq.addr, align 1
  call void @"claw.runtime.println.i1"(i1 %load.8)
  %t6.addr = alloca %"revise_operator_overload::Vector2", align 4
  %field.ptr.9 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %t6.addr, i32 0, i32 0
  store i32 10, ptr %field.ptr.9, align 4
  %field.ptr.10 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %t6.addr, i32 0, i32 1
  store i32 20, ptr %field.ptr.10, align 4
  %v4.addr = alloca %"revise_operator_overload::Vector2", align 4
  %load.11 = load %"revise_operator_overload::Vector2", ptr %t6.addr, align 4
  store %"revise_operator_overload::Vector2" %load.11, ptr %v4.addr, align 4
  %t7 = call i1 @"revise_operator_overload::Vector2.equal"(ptr %v1.addr, ptr %v4.addr)
  %is_same.addr = alloca i1, align 1
  store i1 %t7, ptr %is_same.addr, align 1
  %load.12 = load i1, ptr %is_same.addr, align 1
  call void @"claw.runtime.println.i1"(i1 %load.12)
  %t8.addr = alloca %"revise_operator_overload::Vector2", align 4
  %field.ptr.13 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %t8.addr, i32 0, i32 0
  store i32 2, ptr %field.ptr.13, align 4
  %field.ptr.14 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %t8.addr, i32 0, i32 1
  store i32 3, ptr %field.ptr.14, align 4
  %v5.addr = alloca %"revise_operator_overload::Vector2", align 4
  %load.15 = load %"revise_operator_overload::Vector2", ptr %t8.addr, align 4
  store %"revise_operator_overload::Vector2" %load.15, ptr %v5.addr, align 4
  %t9 = call %"revise_operator_overload::Vector2" @"revise_operator_overload::Vector2.add"(ptr %v1.addr, ptr %v2.addr)
  %addr.16 = alloca %"revise_operator_overload::Vector2", align 4
  store %"revise_operator_overload::Vector2" %t9, ptr %addr.16, align 4
  %t10 = call %"revise_operator_overload::Vector2" @"revise_operator_overload::Vector2.add"(ptr %addr.16, ptr %v5.addr)
  %v6.addr = alloca %"revise_operator_overload::Vector2", align 4
  store %"revise_operator_overload::Vector2" %t10, ptr %v6.addr, align 4
  %field.ptr.17 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %v6.addr, i32 0, i32 0
  %t11 = load i32, ptr %field.ptr.17, align 4
  call void @"claw.runtime.println.i32"(i32 %t11)
  %field.ptr.18 = getelementptr inbounds %"revise_operator_overload::Vector2", ptr %v6.addr, i32 0, i32 1
  %t12 = load i32, ptr %field.ptr.18, align 4
  call void @"claw.runtime.println.i32"(i32 %t12)
  ; drop v6 : Vector2
  ; drop v5 : Vector2
  ; drop v4 : Vector2
  ; drop v3 : Vector2
  ; drop v2 : Vector2
  ; drop v1 : Vector2
  ret i32 0
}
