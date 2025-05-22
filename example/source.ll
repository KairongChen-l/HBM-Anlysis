; ModuleID = 'test_hbm.c'
source_filename = "test_hbm.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@stderr = external global ptr, align 8
@.str = private unnamed_addr constant [47 x i8] c"Error: NUMA API not available on this system.\0A\00", align 1
@.str.1 = private unnamed_addr constant [24 x i8] c"Allocating %zu MiB \E2\80\A6\0A\00", align 1
@.str.2 = private unnamed_addr constant [7 x i8] c"malloc\00", align 1
@.str.3 = private unnamed_addr constant [56 x i8] c"Allocation done \E2\80\94 start monitoring (Ctrl-C to quit)\0A\0A\00", align 1
@.str.4 = private unnamed_addr constant [51 x i8] c"-------------------------------------------------\0A\00", align 1
@.str.5 = private unnamed_addr constant [38 x i8] c"Node %-2d : used %6.2f GB / %6.2f GB\0A\00", align 1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main(i32 noundef %0, ptr noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca ptr, align 8
  %6 = alloca i64, align 8
  %7 = alloca i64, align 8
  %8 = alloca ptr, align 8
  store i32 0, ptr %3, align 4
  store i32 %0, ptr %4, align 4
  store ptr %1, ptr %5, align 8
  %9 = call i32 @numa_available()
  %10 = icmp slt i32 %9, 0
  br i1 %10, label %11, label %14

11:                                               ; preds = %2
  %12 = load ptr, ptr @stderr, align 8
  %13 = call i32 (ptr, ptr, ...) @fprintf(ptr noundef %12, ptr noundef @.str)
  store i32 1, ptr %3, align 4
  br label %42

14:                                               ; preds = %2
  %15 = load i32, ptr %4, align 4
  %16 = icmp sgt i32 %15, 1
  br i1 %16, label %17, label %22

17:                                               ; preds = %14
  %18 = load ptr, ptr %5, align 8
  %19 = getelementptr inbounds ptr, ptr %18, i64 1
  %20 = load ptr, ptr %19, align 8
  %21 = call i64 @strtoull(ptr noundef %20, ptr noundef null, i32 noundef 10) #5
  br label %23

22:                                               ; preds = %14
  br label %23

23:                                               ; preds = %22, %17
  %24 = phi i64 [ %21, %17 ], [ 1024, %22 ]
  store i64 %24, ptr %6, align 8
  %25 = load i64, ptr %6, align 8
  %26 = mul i64 %25, 1024
  %27 = mul i64 %26, 1024
  store i64 %27, ptr %7, align 8
  %28 = load i64, ptr %6, align 8
  %29 = call i32 (ptr, ...) @printf(ptr noundef @.str.1, i64 noundef %28)
  %30 = load i64, ptr %7, align 8
  %31 = call noalias ptr @hbm_malloc(i64 noundef %30) #6
  store ptr %31, ptr %8, align 8
  %32 = load ptr, ptr %8, align 8
  %33 = icmp ne ptr %32, null
  br i1 %33, label %35, label %34

34:                                               ; preds = %23
  call void @perror(ptr noundef @.str.2)
  store i32 1, ptr %3, align 4
  br label %42

35:                                               ; preds = %23
  %36 = load ptr, ptr %8, align 8
  %37 = load i64, ptr %7, align 8
  call void @llvm.memset.p0.i64(ptr align 1 %36, i8 -91, i64 %37, i1 false)
  %38 = call i32 (ptr, ...) @printf(ptr noundef @.str.3)
  br label %39

39:                                               ; preds = %35, %39
  call void @print_node_usage()
  %40 = call i32 @puts(ptr noundef @.str.4)
  %41 = call i32 @sleep(i32 noundef 2)
  br label %39

42:                                               ; preds = %34, %11
  %43 = load i32, ptr %3, align 4
  ret i32 %43
}

declare i32 @numa_available() #1

declare i32 @fprintf(ptr noundef, ptr noundef, ...) #1

; Function Attrs: nounwind
declare i64 @strtoull(ptr noundef, ptr noundef, i32 noundef) #2

declare i32 @printf(ptr noundef, ...) #1

; Function Attrs: nounwind allocsize(0)
declare noalias ptr @hbm_malloc(i64 noundef) #3

declare void @perror(ptr noundef) #1

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #4

; Function Attrs: noinline nounwind optnone uwtable
define internal void @print_node_usage() #0 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca i64, align 8
  %4 = alloca i64, align 8
  %5 = alloca double, align 8
  %6 = alloca double, align 8
  %7 = call i32 @numa_max_node()
  store i32 %7, ptr %1, align 4
  store i32 0, ptr %2, align 4
  br label %8

8:                                                ; preds = %27, %0
  %9 = load i32, ptr %2, align 4
  %10 = load i32, ptr %1, align 4
  %11 = icmp sle i32 %9, %10
  br i1 %11, label %12, label %30

12:                                               ; preds = %8
  store i64 0, ptr %3, align 8
  %13 = load i32, ptr %2, align 4
  %14 = call i64 @numa_node_size64(i32 noundef %13, ptr noundef %3)
  store i64 %14, ptr %4, align 8
  %15 = load i64, ptr %4, align 8
  %16 = load i64, ptr %3, align 8
  %17 = sub nsw i64 %15, %16
  %18 = sitofp i64 %17 to double
  %19 = fdiv double %18, 0x41D0000000000000
  store double %19, ptr %5, align 8
  %20 = load i64, ptr %4, align 8
  %21 = sitofp i64 %20 to double
  %22 = fdiv double %21, 0x41D0000000000000
  store double %22, ptr %6, align 8
  %23 = load i32, ptr %2, align 4
  %24 = load double, ptr %5, align 8
  %25 = load double, ptr %6, align 8
  %26 = call i32 (ptr, ...) @printf(ptr noundef @.str.5, i32 noundef %23, double noundef %24, double noundef %25)
  br label %27

27:                                               ; preds = %12
  %28 = load i32, ptr %2, align 4
  %29 = add nsw i32 %28, 1
  store i32 %29, ptr %2, align 4
  br label %8, !llvm.loop !6

30:                                               ; preds = %8
  ret void
}

declare i32 @puts(ptr noundef) #1

declare i32 @sleep(i32 noundef) #1

declare i32 @numa_max_node() #1

declare i64 @numa_node_size64(i32 noundef, ptr noundef) #1

attributes #0 = { noinline nounwind optnone uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { nounwind "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { nounwind allocsize(0) "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { nocallback nofree nounwind willreturn memory(argmem: write) }
attributes #5 = { nounwind }
attributes #6 = { nounwind allocsize(0) }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"Ubuntu clang version 18.1.8 (11~20.04.2)"}
!6 = distinct !{!6, !7}
!7 = !{!"llvm.loop.mustprogress"}
