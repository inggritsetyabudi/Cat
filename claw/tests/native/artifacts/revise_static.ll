; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
%"revise_static::Config" = type { %claw.slice, i32 }

@"revise_static::VERSION" = internal constant %claw.slice { ptr @.str.0, i64 5 }
@"revise_static::MAX_CONN" = internal constant i32 1024
@"revise_static::ENABLED" = internal constant i1 1
@.str.0 = private unnamed_addr constant [6 x i8] c"1.0.0\00"

declare void @"claw.runtime.println.slice"(ptr)
declare void @"claw.runtime.println.i1"(i1)

define internal void @"revise_static::Config.new"(ptr %ret.slot, %claw.slice %arg.host, i32 %arg.port) {
entry:
  %t0.addr = alloca %"revise_static::Config", align 8
  %field.ptr.0 = getelementptr inbounds %"revise_static::Config", ptr %t0.addr, i32 0, i32 0
  store %claw.slice %arg.host, ptr %field.ptr.0, align 8
  %field.ptr.1 = getelementptr inbounds %"revise_static::Config", ptr %t0.addr, i32 0, i32 1
  store i32 %arg.port, ptr %field.ptr.1, align 4
  %load.2 = load %"revise_static::Config", ptr %t0.addr, align 8
  store %"revise_static::Config" %load.2, ptr %ret.slot, align 8
  ret void
}

define internal %claw.slice @"revise_static::Config.describe"(ptr %arg.self.addr) {
entry:
  %field.ptr.0 = getelementptr inbounds %"revise_static::Config", ptr %arg.self.addr, i32 0, i32 0
  %t0 = load %claw.slice, ptr %field.ptr.0, align 8
  ret %claw.slice %t0
}

define internal %claw.slice @"revise_static::get_version"() {
entry:
  %load.0 = load %claw.slice, ptr @"revise_static::VERSION", align 8
  ret %claw.slice %load.0
}

define internal i32 @"revise_static::main"() {
entry:
  %call.ret.addr.0 = alloca %"revise_static::Config", align 8
  %load.1 = load %claw.slice, ptr @"revise_static::VERSION", align 8
  %load.2 = load i32, ptr @"revise_static::MAX_CONN", align 4
  call void @"revise_static::Config.new"(ptr %call.ret.addr.0, %claw.slice %load.1, i32 %load.2)
  %cfg.addr = alloca %"revise_static::Config", align 8
  %load.3 = load %"revise_static::Config", ptr %call.ret.addr.0, align 8
  store %"revise_static::Config" %load.3, ptr %cfg.addr, align 8
  %t1 = call %claw.slice @"revise_static::Config.describe"(ptr %cfg.addr)
  %addr.4 = alloca %claw.slice, align 8
  store %claw.slice %t1, ptr %addr.4, align 8
  call void @"claw.runtime.println.slice"(ptr %addr.4)
  %load.5 = load i1, ptr @"revise_static::ENABLED", align 1
  call void @"claw.runtime.println.i1"(i1 %load.5)
  ; drop cfg : Config
  ret i32 0
}

define i32 @main() {
entry:
  %entry.result.0 = call i32 @"revise_static::main"()
  ret i32 %entry.result.0
}
