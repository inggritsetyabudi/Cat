; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
%"revise_arena::Node" = type { i32 }

declare ptr @"claw.runtime.arena.alloc"(ptr, i64, i64)
declare ptr @"claw.runtime.arena.new"()
declare void @"claw.runtime.println.i32"(i32)
declare void @"claw.runtime.arena.free"(ptr)

define internal %"revise_arena::Node" @"revise_arena::allocate_node"(ptr %arg.arena, i32 %arg.val_arg) {
entry:
  %t0.addr = alloca %"revise_arena::Node", align 4
  %field.ptr.0 = getelementptr inbounds %"revise_arena::Node", ptr %t0.addr, i32 0, i32 0
  store i32 %arg.val_arg, ptr %field.ptr.0, align 4
  %temp.addr = alloca %"revise_arena::Node", align 4
  %load.1 = load %"revise_arena::Node", ptr %t0.addr, align 4
  store %"revise_arena::Node" %load.1, ptr %temp.addr, align 4
  %arena.alloc.2 = call ptr @"claw.runtime.arena.alloc"(ptr %arg.arena, i64 4, i64 4)
  %load.3 = load %"revise_arena::Node", ptr %temp.addr, align 4
  store %"revise_arena::Node" %load.3, ptr %arena.alloc.2, align 4
  %n.addr = alloca %"revise_arena::Node", align 4
  %load.4 = load %"revise_arena::Node", ptr %arena.alloc.2, align 4
  store %"revise_arena::Node" %load.4, ptr %n.addr, align 4
  ; drop temp : Node
  %load.5 = load %"revise_arena::Node", ptr %n.addr, align 4
  ret %"revise_arena::Node" %load.5
}

define internal i32 @"revise_arena::main"() {
entry:
  %arena.new.0 = call ptr @"claw.runtime.arena.new"()
  %arena.addr = alloca ptr, align 8
  store ptr %arena.new.0, ptr %arena.addr, align 8
  %load.1 = load ptr, ptr %arena.addr, align 8
  %t1 = call %"revise_arena::Node" @"revise_arena::allocate_node"(ptr %load.1, i32 100)
  %n1.addr = alloca %"revise_arena::Node", align 4
  store %"revise_arena::Node" %t1, ptr %n1.addr, align 4
  %field.ptr.2 = getelementptr inbounds %"revise_arena::Node", ptr %n1.addr, i32 0, i32 0
  %t2 = load i32, ptr %field.ptr.2, align 4
  call void @"claw.runtime.println.i32"(i32 %t2)
  %load.3 = load ptr, ptr %arena.addr, align 8
  %t3 = call %"revise_arena::Node" @"revise_arena::allocate_node"(ptr %load.3, i32 200)
  %n2.addr = alloca %"revise_arena::Node", align 4
  store %"revise_arena::Node" %t3, ptr %n2.addr, align 4
  %field.ptr.4 = getelementptr inbounds %"revise_arena::Node", ptr %n2.addr, i32 0, i32 0
  %t4 = load i32, ptr %field.ptr.4, align 4
  call void @"claw.runtime.println.i32"(i32 %t4)
  %t5.addr = alloca %"revise_arena::Node", align 4
  %field.ptr.5 = getelementptr inbounds %"revise_arena::Node", ptr %t5.addr, i32 0, i32 0
  store i32 300, ptr %field.ptr.5, align 4
  %temp3.addr = alloca %"revise_arena::Node", align 4
  %load.6 = load %"revise_arena::Node", ptr %t5.addr, align 4
  store %"revise_arena::Node" %load.6, ptr %temp3.addr, align 4
  %load.7 = load ptr, ptr %arena.addr, align 8
  %arena.alloc.8 = call ptr @"claw.runtime.arena.alloc"(ptr %load.7, i64 4, i64 4)
  %load.9 = load %"revise_arena::Node", ptr %temp3.addr, align 4
  store %"revise_arena::Node" %load.9, ptr %arena.alloc.8, align 4
  %n3.addr = alloca %"revise_arena::Node", align 4
  %load.10 = load %"revise_arena::Node", ptr %arena.alloc.8, align 4
  store %"revise_arena::Node" %load.10, ptr %n3.addr, align 4
  %field.ptr.11 = getelementptr inbounds %"revise_arena::Node", ptr %n3.addr, i32 0, i32 0
  %t7 = load i32, ptr %field.ptr.11, align 4
  call void @"claw.runtime.println.i32"(i32 %t7)
  ; drop temp3 : Node
  %load.12 = load ptr, ptr %arena.addr, align 8
  call void @"claw.runtime.arena.free"(ptr %load.12)
  ret i32 0
}

define i32 @main() {
entry:
  %entry.result.0 = call i32 @"revise_arena::main"()
  ret i32 %entry.result.0
}
