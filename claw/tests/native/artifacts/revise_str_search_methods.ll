; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
@.str.7 = private unnamed_addr constant [1 x i8] c"\00"

@.str.10 = private unnamed_addr constant [14 x i8] c"str-search-ok\00"

@.str.6 = private unnamed_addr constant [8 x i8] c"missing\00"

@.str.5 = private unnamed_addr constant [3 x i8] c"\C3\A9\00"

@.str.4 = private unnamed_addr constant [9 x i8] c"language\00"

@.str.3 = private unnamed_addr constant [7 x i8] c"!caf\C3\A9\00"

@.str.9 = private unnamed_addr constant [18 x i8] c"C@ language caf\C3\A9\00"

@.str.2 = private unnamed_addr constant [6 x i8] c"caf\C3\A9\00"

@.str.1 = private unnamed_addr constant [24 x i8] c"C@ language caf\C3\A9 extra\00"

@.str.8 = private unnamed_addr constant [2 x i8] c"x\00"

@.str.0 = private unnamed_addr constant [3 x i8] c"C@\00"

declare i1 @"claw.runtime.str.starts_with"(ptr, ptr)
declare i1 @"claw.runtime.str.ends_with"(ptr, ptr)
declare i1 @"claw.runtime.str.contains"(ptr, ptr)
declare void @"claw.runtime.println.slice"(ptr)

define internal i32 @"revise_str_search_methods::check_str_search"(%claw.slice %arg.text, %claw.slice %arg.empty) {
entry:
  %str.len.0 = extractvalue %claw.slice %arg.text, 1
  %t0 = add i64 0, %str.len.0
  %t1 = icmp ne i64 %t0, 17
  br i1 %t1, label %when_then_0, label %when_join_1
when_then_0:
  ret i32 13
when_join_1:
  %str.len.1 = extractvalue %claw.slice %arg.text, 1
  %t2 = icmp eq i64 %str.len.1, 0
  %t3 = icmp eq i1 %t2, true
  br i1 %t3, label %when_then_2, label %when_join_3
when_then_2:
  ret i32 14
when_join_3:
  %str.len.2 = extractvalue %claw.slice %arg.empty, 1
  %t4 = add i64 0, %str.len.2
  %t5 = icmp ne i64 %t4, 0
  br i1 %t5, label %when_then_4, label %when_join_5
when_then_4:
  ret i32 15
when_join_5:
  %str.len.3 = extractvalue %claw.slice %arg.empty, 1
  %t6 = icmp eq i64 %str.len.3, 0
  %t7 = icmp eq i1 %t6, false
  br i1 %t7, label %when_then_6, label %when_join_7
when_then_6:
  ret i32 16
when_join_7:
  %addr.4 = alloca %claw.slice, align 8
  store %claw.slice %arg.text, ptr %addr.4, align 8
  %addr.5 = alloca %claw.slice, align 8
  %str.ptr.6 = getelementptr inbounds [3 x i8], ptr @.str.0, i64 0, i64 0
  %str.slice.7 = insertvalue %claw.slice poison, ptr %str.ptr.6, 0
  %str.slice.8 = insertvalue %claw.slice %str.slice.7, i64 2, 1
  store %claw.slice %str.slice.8, ptr %addr.5, align 8
  %t8 = call i1 @"claw.runtime.str.starts_with"(ptr %addr.4, ptr %addr.5)
  %t9 = icmp eq i1 %t8, false
  br i1 %t9, label %when_then_8, label %when_join_9
when_then_8:
  ret i32 1
when_join_9:
  %load.9 = load %claw.slice, ptr %addr.4, align 8
  %addr.10 = alloca %claw.slice, align 8
  %str.ptr.11 = getelementptr inbounds [24 x i8], ptr @.str.1, i64 0, i64 0
  %str.slice.12 = insertvalue %claw.slice poison, ptr %str.ptr.11, 0
  %str.slice.13 = insertvalue %claw.slice %str.slice.12, i64 23, 1
  store %claw.slice %str.slice.13, ptr %addr.10, align 8
  %t10 = call i1 @"claw.runtime.str.starts_with"(ptr %addr.4, ptr %addr.10)
  %t11 = icmp eq i1 %t10, true
  br i1 %t11, label %when_then_10, label %when_join_11
when_then_10:
  ret i32 2
when_join_11:
  %load.14 = load %claw.slice, ptr %addr.4, align 8
  %addr.15 = alloca %claw.slice, align 8
  %str.ptr.16 = getelementptr inbounds [6 x i8], ptr @.str.2, i64 0, i64 0
  %str.slice.17 = insertvalue %claw.slice poison, ptr %str.ptr.16, 0
  %str.slice.18 = insertvalue %claw.slice %str.slice.17, i64 5, 1
  store %claw.slice %str.slice.18, ptr %addr.15, align 8
  %t12 = call i1 @"claw.runtime.str.ends_with"(ptr %addr.4, ptr %addr.15)
  %t13 = icmp eq i1 %t12, false
  br i1 %t13, label %when_then_12, label %when_join_13
when_then_12:
  ret i32 3
when_join_13:
  %load.19 = load %claw.slice, ptr %addr.4, align 8
  %addr.20 = alloca %claw.slice, align 8
  %str.ptr.21 = getelementptr inbounds [7 x i8], ptr @.str.3, i64 0, i64 0
  %str.slice.22 = insertvalue %claw.slice poison, ptr %str.ptr.21, 0
  %str.slice.23 = insertvalue %claw.slice %str.slice.22, i64 6, 1
  store %claw.slice %str.slice.23, ptr %addr.20, align 8
  %t14 = call i1 @"claw.runtime.str.ends_with"(ptr %addr.4, ptr %addr.20)
  %t15 = icmp eq i1 %t14, true
  br i1 %t15, label %when_then_14, label %when_join_15
when_then_14:
  ret i32 4
when_join_15:
  %load.24 = load %claw.slice, ptr %addr.4, align 8
  %addr.25 = alloca %claw.slice, align 8
  %str.ptr.26 = getelementptr inbounds [24 x i8], ptr @.str.1, i64 0, i64 0
  %str.slice.27 = insertvalue %claw.slice poison, ptr %str.ptr.26, 0
  %str.slice.28 = insertvalue %claw.slice %str.slice.27, i64 23, 1
  store %claw.slice %str.slice.28, ptr %addr.25, align 8
  %t16 = call i1 @"claw.runtime.str.ends_with"(ptr %addr.4, ptr %addr.25)
  %t17 = icmp eq i1 %t16, true
  br i1 %t17, label %when_then_16, label %when_join_17
when_then_16:
  ret i32 17
when_join_17:
  %load.29 = load %claw.slice, ptr %addr.4, align 8
  %addr.30 = alloca %claw.slice, align 8
  %str.ptr.31 = getelementptr inbounds [9 x i8], ptr @.str.4, i64 0, i64 0
  %str.slice.32 = insertvalue %claw.slice poison, ptr %str.ptr.31, 0
  %str.slice.33 = insertvalue %claw.slice %str.slice.32, i64 8, 1
  store %claw.slice %str.slice.33, ptr %addr.30, align 8
  %t18 = call i1 @"claw.runtime.str.contains"(ptr %addr.4, ptr %addr.30)
  %t19 = icmp eq i1 %t18, false
  br i1 %t19, label %when_then_18, label %when_join_19
when_then_18:
  ret i32 5
when_join_19:
  %load.34 = load %claw.slice, ptr %addr.4, align 8
  %addr.35 = alloca %claw.slice, align 8
  %str.ptr.36 = getelementptr inbounds [3 x i8], ptr @.str.5, i64 0, i64 0
  %str.slice.37 = insertvalue %claw.slice poison, ptr %str.ptr.36, 0
  %str.slice.38 = insertvalue %claw.slice %str.slice.37, i64 2, 1
  store %claw.slice %str.slice.38, ptr %addr.35, align 8
  %t20 = call i1 @"claw.runtime.str.contains"(ptr %addr.4, ptr %addr.35)
  %t21 = icmp eq i1 %t20, false
  br i1 %t21, label %when_then_20, label %when_join_21
when_then_20:
  ret i32 6
when_join_21:
  %load.39 = load %claw.slice, ptr %addr.4, align 8
  %addr.40 = alloca %claw.slice, align 8
  %str.ptr.41 = getelementptr inbounds [8 x i8], ptr @.str.6, i64 0, i64 0
  %str.slice.42 = insertvalue %claw.slice poison, ptr %str.ptr.41, 0
  %str.slice.43 = insertvalue %claw.slice %str.slice.42, i64 7, 1
  store %claw.slice %str.slice.43, ptr %addr.40, align 8
  %t22 = call i1 @"claw.runtime.str.contains"(ptr %addr.4, ptr %addr.40)
  %t23 = icmp eq i1 %t22, true
  br i1 %t23, label %when_then_22, label %when_join_23
when_then_22:
  ret i32 7
when_join_23:
  %load.44 = load %claw.slice, ptr %addr.4, align 8
  %addr.45 = alloca %claw.slice, align 8
  %str.ptr.46 = getelementptr inbounds [1 x i8], ptr @.str.7, i64 0, i64 0
  %str.slice.47 = insertvalue %claw.slice poison, ptr %str.ptr.46, 0
  %str.slice.48 = insertvalue %claw.slice %str.slice.47, i64 0, 1
  store %claw.slice %str.slice.48, ptr %addr.45, align 8
  %t24 = call i1 @"claw.runtime.str.contains"(ptr %addr.4, ptr %addr.45)
  %t25 = icmp eq i1 %t24, false
  br i1 %t25, label %when_then_24, label %when_join_25
when_then_24:
  ret i32 8
when_join_25:
  %addr.49 = alloca %claw.slice, align 8
  store %claw.slice %arg.empty, ptr %addr.49, align 8
  %addr.50 = alloca %claw.slice, align 8
  %str.ptr.51 = getelementptr inbounds [1 x i8], ptr @.str.7, i64 0, i64 0
  %str.slice.52 = insertvalue %claw.slice poison, ptr %str.ptr.51, 0
  %str.slice.53 = insertvalue %claw.slice %str.slice.52, i64 0, 1
  store %claw.slice %str.slice.53, ptr %addr.50, align 8
  %t26 = call i1 @"claw.runtime.str.starts_with"(ptr %addr.49, ptr %addr.50)
  %t27 = icmp eq i1 %t26, false
  br i1 %t27, label %when_then_26, label %when_join_27
when_then_26:
  ret i32 9
when_join_27:
  %load.54 = load %claw.slice, ptr %addr.49, align 8
  %addr.55 = alloca %claw.slice, align 8
  %str.ptr.56 = getelementptr inbounds [1 x i8], ptr @.str.7, i64 0, i64 0
  %str.slice.57 = insertvalue %claw.slice poison, ptr %str.ptr.56, 0
  %str.slice.58 = insertvalue %claw.slice %str.slice.57, i64 0, 1
  store %claw.slice %str.slice.58, ptr %addr.55, align 8
  %t28 = call i1 @"claw.runtime.str.ends_with"(ptr %addr.49, ptr %addr.55)
  %t29 = icmp eq i1 %t28, false
  br i1 %t29, label %when_then_28, label %when_join_29
when_then_28:
  ret i32 10
when_join_29:
  %load.59 = load %claw.slice, ptr %addr.49, align 8
  %addr.60 = alloca %claw.slice, align 8
  %str.ptr.61 = getelementptr inbounds [1 x i8], ptr @.str.7, i64 0, i64 0
  %str.slice.62 = insertvalue %claw.slice poison, ptr %str.ptr.61, 0
  %str.slice.63 = insertvalue %claw.slice %str.slice.62, i64 0, 1
  store %claw.slice %str.slice.63, ptr %addr.60, align 8
  %t30 = call i1 @"claw.runtime.str.contains"(ptr %addr.49, ptr %addr.60)
  %t31 = icmp eq i1 %t30, false
  br i1 %t31, label %when_then_30, label %when_join_31
when_then_30:
  ret i32 11
when_join_31:
  %load.64 = load %claw.slice, ptr %addr.49, align 8
  %addr.65 = alloca %claw.slice, align 8
  %str.ptr.66 = getelementptr inbounds [2 x i8], ptr @.str.8, i64 0, i64 0
  %str.slice.67 = insertvalue %claw.slice poison, ptr %str.ptr.66, 0
  %str.slice.68 = insertvalue %claw.slice %str.slice.67, i64 1, 1
  store %claw.slice %str.slice.68, ptr %addr.65, align 8
  %t32 = call i1 @"claw.runtime.str.contains"(ptr %addr.49, ptr %addr.65)
  %t33 = icmp eq i1 %t32, true
  br i1 %t33, label %when_then_32, label %when_join_33
when_then_32:
  ret i32 12
when_join_33:
  ret i32 0
}

define internal i32 @"revise_str_search_methods::main"() {
entry:
  %text.addr = alloca %claw.slice, align 8
  %str.ptr.0 = getelementptr inbounds [18 x i8], ptr @.str.9, i64 0, i64 0
  %str.slice.1 = insertvalue %claw.slice poison, ptr %str.ptr.0, 0
  %str.slice.2 = insertvalue %claw.slice %str.slice.1, i64 17, 1
  store %claw.slice %str.slice.2, ptr %text.addr, align 8
  %empty.addr = alloca %claw.slice, align 8
  %str.ptr.3 = getelementptr inbounds [1 x i8], ptr @.str.7, i64 0, i64 0
  %str.slice.4 = insertvalue %claw.slice poison, ptr %str.ptr.3, 0
  %str.slice.5 = insertvalue %claw.slice %str.slice.4, i64 0, 1
  store %claw.slice %str.slice.5, ptr %empty.addr, align 8
  %load.6 = load %claw.slice, ptr %text.addr, align 8
  %addr.7 = alloca %claw.slice, align 8
  %str.ptr.8 = getelementptr inbounds [3 x i8], ptr @.str.0, i64 0, i64 0
  %str.slice.9 = insertvalue %claw.slice poison, ptr %str.ptr.8, 0
  %str.slice.10 = insertvalue %claw.slice %str.slice.9, i64 2, 1
  store %claw.slice %str.slice.10, ptr %addr.7, align 8
  %t0 = call i1 @"claw.runtime.str.starts_with"(ptr %text.addr, ptr %addr.7)
  %t1 = icmp eq i1 %t0, false
  br i1 %t1, label %when_then_0, label %when_join_1
when_then_0:
  ; drop empty : Str
  ; drop text : Str
  ret i32 18
when_join_1:
  %load.11 = load %claw.slice, ptr %text.addr, align 8
  %addr.12 = alloca %claw.slice, align 8
  %str.ptr.13 = getelementptr inbounds [6 x i8], ptr @.str.2, i64 0, i64 0
  %str.slice.14 = insertvalue %claw.slice poison, ptr %str.ptr.13, 0
  %str.slice.15 = insertvalue %claw.slice %str.slice.14, i64 5, 1
  store %claw.slice %str.slice.15, ptr %addr.12, align 8
  %t2 = call i1 @"claw.runtime.str.ends_with"(ptr %text.addr, ptr %addr.12)
  %t3 = icmp eq i1 %t2, false
  br i1 %t3, label %when_then_2, label %when_join_3
when_then_2:
  ; drop empty : Str
  ; drop text : Str
  ret i32 19
when_join_3:
  %load.16 = load %claw.slice, ptr %text.addr, align 8
  %addr.17 = alloca %claw.slice, align 8
  %str.ptr.18 = getelementptr inbounds [9 x i8], ptr @.str.4, i64 0, i64 0
  %str.slice.19 = insertvalue %claw.slice poison, ptr %str.ptr.18, 0
  %str.slice.20 = insertvalue %claw.slice %str.slice.19, i64 8, 1
  store %claw.slice %str.slice.20, ptr %addr.17, align 8
  %t4 = call i1 @"claw.runtime.str.contains"(ptr %text.addr, ptr %addr.17)
  %t5 = icmp eq i1 %t4, false
  br i1 %t5, label %when_then_4, label %when_join_5
when_then_4:
  ; drop empty : Str
  ; drop text : Str
  ret i32 20
when_join_5:
  %load.21 = load %claw.slice, ptr %text.addr, align 8
  %load.22 = load %claw.slice, ptr %empty.addr, align 8
  %t6 = call i32 @"revise_str_search_methods::check_str_search"(%claw.slice %load.21, %claw.slice %load.22)
  %result.addr = alloca i32, align 4
  store i32 %t6, ptr %result.addr, align 4
  %load.23 = load i32, ptr %result.addr, align 4
  %t7 = icmp ne i32 %load.23, 0
  br i1 %t7, label %when_then_6, label %when_join_7
when_then_6:
  ; drop empty : Str
  ; drop text : Str
  %load.24 = load i32, ptr %result.addr, align 4
  ret i32 %load.24
when_join_7:
  %addr.25 = alloca %claw.slice, align 8
  %str.ptr.26 = getelementptr inbounds [14 x i8], ptr @.str.10, i64 0, i64 0
  %str.slice.27 = insertvalue %claw.slice poison, ptr %str.ptr.26, 0
  %str.slice.28 = insertvalue %claw.slice %str.slice.27, i64 13, 1
  store %claw.slice %str.slice.28, ptr %addr.25, align 8
  call void @"claw.runtime.println.slice"(ptr %addr.25)
  ; drop empty : Str
  ; drop text : Str
  ret i32 0
}

define i32 @main() {
entry:
  %entry.result.0 = call i32 @"revise_str_search_methods::main"()
  ret i32 %entry.result.0
}
