; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
%"revise_contract::Point" = type { i32, i32 }

declare void @"claw.runtime.println.i32"(i32)

define internal %"revise_contract::Point" @"revise_contract::Point.new"(i32 %arg.x, i32 %arg.y) {
entry:
  %t0.addr = alloca %"revise_contract::Point", align 4
  %field.ptr.0 = getelementptr inbounds %"revise_contract::Point", ptr %t0.addr, i32 0, i32 0
  store i32 %arg.x, ptr %field.ptr.0, align 4
  %field.ptr.1 = getelementptr inbounds %"revise_contract::Point", ptr %t0.addr, i32 0, i32 1
  store i32 %arg.y, ptr %field.ptr.1, align 4
  %load.2 = load %"revise_contract::Point", ptr %t0.addr, align 4
  ret %"revise_contract::Point" %load.2
}

define internal void @"revise_contract::Point.move_by"(ptr %arg.self.addr, i32 %arg.dx, i32 %arg.dy) {
entry:
  %field.ptr.0 = getelementptr inbounds %"revise_contract::Point", ptr %arg.self.addr, i32 0, i32 0
  %t0 = load i32, ptr %field.ptr.0, align 4
  %t1 = add i32 %t0, %arg.dx
  %field.ptr.1 = getelementptr inbounds %"revise_contract::Point", ptr %arg.self.addr, i32 0, i32 0
  store i32 %t1, ptr %field.ptr.1, align 4
  %field.ptr.2 = getelementptr inbounds %"revise_contract::Point", ptr %arg.self.addr, i32 0, i32 1
  %t2 = load i32, ptr %field.ptr.2, align 4
  %t3 = add i32 %t2, %arg.dy
  %field.ptr.3 = getelementptr inbounds %"revise_contract::Point", ptr %arg.self.addr, i32 0, i32 1
  store i32 %t3, ptr %field.ptr.3, align 4
  ret void
}

define internal i32 @"revise_contract::Point.describe"(ptr %arg.self.addr) {
entry:
  %field.ptr.0 = getelementptr inbounds %"revise_contract::Point", ptr %arg.self.addr, i32 0, i32 0
  %t0 = load i32, ptr %field.ptr.0, align 4
  %field.ptr.1 = getelementptr inbounds %"revise_contract::Point", ptr %arg.self.addr, i32 0, i32 1
  %t1 = load i32, ptr %field.ptr.1, align 4
  %t2 = add i32 %t0, %t1
  ret i32 %t2
}

define internal void @"revise_contract::Point.draw"(ptr %arg.self.addr) {
entry:
  %field.ptr.0 = getelementptr inbounds %"revise_contract::Point", ptr %arg.self.addr, i32 0, i32 0
  %t0 = load i32, ptr %field.ptr.0, align 4
  %field.ptr.1 = getelementptr inbounds %"revise_contract::Point", ptr %arg.self.addr, i32 0, i32 1
  %t1 = load i32, ptr %field.ptr.1, align 4
  %t2 = add i32 %t0, %t1
  call void @"claw.runtime.println.i32"(i32 %t2)
  ret void
}

define internal i32 @"revise_contract::main"() {
entry:
  %t0 = call %"revise_contract::Point" @"revise_contract::Point.new"(i32 10, i32 20)
  %p.addr = alloca %"revise_contract::Point", align 4
  store %"revise_contract::Point" %t0, ptr %p.addr, align 4
  %t1 = call i32 @"revise_contract::Point.describe"(ptr %p.addr)
  call void @"claw.runtime.println.i32"(i32 %t1)
  call void @"revise_contract::Point.move_by"(ptr %p.addr, i32 5, i32 5)
  call void @"revise_contract::Point.draw"(ptr %p.addr)
  ; drop p : Point
  ret i32 0
}
