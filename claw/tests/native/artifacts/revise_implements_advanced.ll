; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
%"revise_implements_advanced::Counter" = type { i32 }

%"revise_implements_advanced::Point" = type { i32, i32 }

declare void @"claw.runtime.println.i32"(i32)

define internal %"revise_implements_advanced::Counter" @"revise_implements_advanced::Counter.new"(i32 %arg.start) {
entry:
  %t0.addr = alloca %"revise_implements_advanced::Counter", align 4
  %field.ptr.0 = getelementptr inbounds %"revise_implements_advanced::Counter", ptr %t0.addr, i32 0, i32 0
  store i32 %arg.start, ptr %field.ptr.0, align 4
  %load.1 = load %"revise_implements_advanced::Counter", ptr %t0.addr, align 4
  ret %"revise_implements_advanced::Counter" %load.1
}

define internal i32 @"revise_implements_advanced::Counter.get"(ptr %arg.self.addr) {
entry:
  %field.ptr.0 = getelementptr inbounds %"revise_implements_advanced::Counter", ptr %arg.self.addr, i32 0, i32 0
  %t0 = load i32, ptr %field.ptr.0, align 4
  ret i32 %t0
}

define internal void @"revise_implements_advanced::Counter.inc"(ptr %arg.self.addr) {
entry:
  %field.ptr.0 = getelementptr inbounds %"revise_implements_advanced::Counter", ptr %arg.self.addr, i32 0, i32 0
  %t0 = load i32, ptr %field.ptr.0, align 4
  %t1 = add i32 %t0, 1
  %field.ptr.1 = getelementptr inbounds %"revise_implements_advanced::Counter", ptr %arg.self.addr, i32 0, i32 0
  store i32 %t1, ptr %field.ptr.1, align 4
  ret void
}

define internal i32 @"revise_implements_advanced::Counter.consume_val"(%"revise_implements_advanced::Counter" %arg.self) {
entry:
  %addr.0 = alloca %"revise_implements_advanced::Counter", align 4
  store %"revise_implements_advanced::Counter" %arg.self, ptr %addr.0, align 4
  %field.ptr.1 = getelementptr inbounds %"revise_implements_advanced::Counter", ptr %addr.0, i32 0, i32 0
  %t0 = load i32, ptr %field.ptr.1, align 4
  ret i32 %t0
}

define internal %"revise_implements_advanced::Point" @"revise_implements_advanced::Point.origin"() {
entry:
  %t0.addr = alloca %"revise_implements_advanced::Point", align 4
  %field.ptr.0 = getelementptr inbounds %"revise_implements_advanced::Point", ptr %t0.addr, i32 0, i32 0
  store i32 0, ptr %field.ptr.0, align 4
  %field.ptr.1 = getelementptr inbounds %"revise_implements_advanced::Point", ptr %t0.addr, i32 0, i32 1
  store i32 0, ptr %field.ptr.1, align 4
  %load.2 = load %"revise_implements_advanced::Point", ptr %t0.addr, align 4
  ret %"revise_implements_advanced::Point" %load.2
}

define internal i32 @"revise_implements_advanced::Point.sum"(ptr %arg.self.addr) {
entry:
  %field.ptr.0 = getelementptr inbounds %"revise_implements_advanced::Point", ptr %arg.self.addr, i32 0, i32 0
  %t0 = load i32, ptr %field.ptr.0, align 4
  %field.ptr.1 = getelementptr inbounds %"revise_implements_advanced::Point", ptr %arg.self.addr, i32 0, i32 1
  %t1 = load i32, ptr %field.ptr.1, align 4
  %t2 = add i32 %t0, %t1
  ret i32 %t2
}

define internal i32 @"revise_implements_advanced::main"() {
entry:
  %t0 = call %"revise_implements_advanced::Counter" @"revise_implements_advanced::Counter.new"(i32 10)
  %c.addr = alloca %"revise_implements_advanced::Counter", align 4
  store %"revise_implements_advanced::Counter" %t0, ptr %c.addr, align 4
  call void @"revise_implements_advanced::Counter.inc"(ptr %c.addr)
  call void @"revise_implements_advanced::Counter.inc"(ptr %c.addr)
  %t1 = call i32 @"revise_implements_advanced::Counter.get"(ptr %c.addr)
  %val_after_inc.addr = alloca i32, align 4
  store i32 %t1, ptr %val_after_inc.addr, align 4
  %t2 = call %"revise_implements_advanced::Point" @"revise_implements_advanced::Point.origin"()
  %p.addr = alloca %"revise_implements_advanced::Point", align 4
  store %"revise_implements_advanced::Point" %t2, ptr %p.addr, align 4
  %t3 = call i32 @"revise_implements_advanced::Point.sum"(ptr %p.addr)
  %p_sum.addr = alloca i32, align 4
  store i32 %t3, ptr %p_sum.addr, align 4
  %load.0 = load %"revise_implements_advanced::Counter", ptr %c.addr, align 4
  %t4 = call i32 @"revise_implements_advanced::Counter.consume_val"(%"revise_implements_advanced::Counter" %load.0)
  %final_count.addr = alloca i32, align 4
  store i32 %t4, ptr %final_count.addr, align 4
  %load.1 = load i32, ptr %val_after_inc.addr, align 4
  %load.2 = load i32, ptr %p_sum.addr, align 4
  %t5 = add i32 %load.1, %load.2
  %load.3 = load i32, ptr %final_count.addr, align 4
  %t6 = add i32 %t5, %load.3
  call void @"claw.runtime.println.i32"(i32 %t6)
  ; drop p : Point
  ; drop c : Counter
  ret i32 0
}

define i32 @main() {
entry:
  %entry.result.0 = call i32 @"revise_implements_advanced::main"()
  ret i32 %entry.result.0
}
