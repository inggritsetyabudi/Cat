; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
%"revise_implements::Config" = type { %claw.slice, i32 }

@.str.0 = private unnamed_addr constant [10 x i8] c"localhost\00"

declare void @"claw.runtime.println.slice"(ptr)

define internal void @"revise_implements::Config.new"(ptr %ret.slot, %claw.slice %arg.host, i32 %arg.port) {
entry:
  %t0.addr = alloca %"revise_implements::Config", align 8
  %field.ptr.0 = getelementptr inbounds %"revise_implements::Config", ptr %t0.addr, i32 0, i32 0
  store %claw.slice %arg.host, ptr %field.ptr.0, align 8
  %field.ptr.1 = getelementptr inbounds %"revise_implements::Config", ptr %t0.addr, i32 0, i32 1
  store i32 %arg.port, ptr %field.ptr.1, align 4
  %load.2 = load %"revise_implements::Config", ptr %t0.addr, align 8
  store %"revise_implements::Config" %load.2, ptr %ret.slot, align 8
  ret void
}

define internal void @"revise_implements::Config.show_host"(ptr %arg.self.addr) {
entry:
  %field.ptr.0 = getelementptr inbounds %"revise_implements::Config", ptr %arg.self.addr, i32 0, i32 0
  %t0 = load %claw.slice, ptr %field.ptr.0, align 8
  %addr.1 = alloca %claw.slice, align 8
  store %claw.slice %t0, ptr %addr.1, align 8
  call void @"claw.runtime.println.slice"(ptr %addr.1)
  ret void
}

define internal i32 @"revise_implements::main"() {
entry:
  %call.ret.addr.0 = alloca %"revise_implements::Config", align 8
  %str.ptr.1 = getelementptr inbounds [10 x i8], ptr @.str.0, i64 0, i64 0
  %str.slice.2 = insertvalue %claw.slice poison, ptr %str.ptr.1, 0
  %str.slice.3 = insertvalue %claw.slice %str.slice.2, i64 9, 1
  call void @"revise_implements::Config.new"(ptr %call.ret.addr.0, %claw.slice %str.slice.3, i32 8080)
  %cfg.addr = alloca %"revise_implements::Config", align 8
  %load.4 = load %"revise_implements::Config", ptr %call.ret.addr.0, align 8
  store %"revise_implements::Config" %load.4, ptr %cfg.addr, align 8
  call void @"revise_implements::Config.show_host"(ptr %cfg.addr)
  ; drop cfg : Config
  ret i32 0
}

define i32 @main() {
entry:
  %entry.result.0 = call i32 @"revise_implements::main"()
  ret i32 %entry.result.0
}
