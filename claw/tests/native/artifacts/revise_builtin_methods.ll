; ModuleID = 'claw'
target triple = "aarch64-apple-darwin"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
@.str.2 = private unnamed_addr constant [6 x i8] c"hello\00"

@.str.1 = private unnamed_addr constant [1 x i8] c"\00"

@.str.0 = private unnamed_addr constant [3 x i8] c"C@\00"

define internal i32 @"revise_builtin_methods::main"() {
entry:
  %text.addr = alloca %claw.slice, align 8
  %str.ptr.0 = getelementptr inbounds [3 x i8], ptr @.str.0, i64 0, i64 0
  %str.slice.1 = insertvalue %claw.slice poison, ptr %str.ptr.0, 0
  %str.slice.2 = insertvalue %claw.slice %str.slice.1, i64 2, 1
  store %claw.slice %str.slice.2, ptr %text.addr, align 8
  %load.3 = load %claw.slice, ptr %text.addr, align 8
  %str.len.4 = extractvalue %claw.slice %load.3, 1
  %t0 = add i64 0, %str.len.4
  %l.addr = alloca i64, align 8
  store i64 %t0, ptr %l.addr, align 8
  %load.5 = load i64, ptr %l.addr, align 8
  %t1 = icmp ne i64 %load.5, 2
  br i1 %t1, label %when_then_0, label %when_join_1
when_then_0:
  ; drop text : Str
  ret i32 1
when_join_1:
  %empty_str.addr = alloca %claw.slice, align 8
  %str.ptr.6 = getelementptr inbounds [1 x i8], ptr @.str.1, i64 0, i64 0
  %str.slice.7 = insertvalue %claw.slice poison, ptr %str.ptr.6, 0
  %str.slice.8 = insertvalue %claw.slice %str.slice.7, i64 0, 1
  store %claw.slice %str.slice.8, ptr %empty_str.addr, align 8
  %load.9 = load %claw.slice, ptr %empty_str.addr, align 8
  %str.len.10 = extractvalue %claw.slice %load.9, 1
  %t2 = icmp eq i64 %str.len.10, 0
  %t3 = icmp eq i1 %t2, false
  br i1 %t3, label %when_then_2, label %when_join_3
when_then_2:
  ; drop empty_str : Str
  ; drop text : Str
  ret i32 2
when_join_3:
  %non_empty.addr = alloca %claw.slice, align 8
  %str.ptr.11 = getelementptr inbounds [6 x i8], ptr @.str.2, i64 0, i64 0
  %str.slice.12 = insertvalue %claw.slice poison, ptr %str.ptr.11, 0
  %str.slice.13 = insertvalue %claw.slice %str.slice.12, i64 5, 1
  store %claw.slice %str.slice.13, ptr %non_empty.addr, align 8
  %load.14 = load %claw.slice, ptr %non_empty.addr, align 8
  %str.len.15 = extractvalue %claw.slice %load.14, 1
  %t4 = icmp eq i64 %str.len.15, 0
  %t5 = icmp eq i1 %t4, true
  br i1 %t5, label %when_then_4, label %when_join_5
when_then_4:
  ; drop non_empty : Str
  ; drop empty_str : Str
  ; drop text : Str
  ret i32 3
when_join_5:
  ; drop non_empty : Str
  ; drop empty_str : Str
  ; drop text : Str
  ret i32 0
}

define i32 @main() {
entry:
  %entry.result.0 = call i32 @"revise_builtin_methods::main"()
  ret i32 %entry.result.0
}
