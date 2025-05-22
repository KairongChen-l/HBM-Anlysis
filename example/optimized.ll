; ModuleID = 'source.ll'
source_filename = "test_hbm.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@.str = private unnamed_addr constant [7 x i8] c"malloc\00", align 1
@.str.1 = private unnamed_addr constant [17 x i8] c"sum(arr) = %lld\0A\00", align 1
@.str.2 = private unnamed_addr constant [11 x i8] c"malloc vec\00", align 1
@.str.3 = private unnamed_addr constant [15 x i8] c"vec[100] = %d\0A\00", align 1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 {
  %1 = alloca i32, align 4
  %2 = alloca i64, align 8
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  %6 = alloca ptr, align 8
  store i32 0, ptr %1, align 4
  store i64 52428800, ptr %2, align 8
  %7 = load i64, ptr %2, align 8
  %8 = mul i64 %7, 4
  %9 = call noalias ptr @malloc(i64 noundef %8) #4
  store ptr %9, ptr %3, align 8
  %10 = load ptr, ptr %3, align 8
  %11 = icmp ne ptr %10, null
  br i1 %11, label %13, label %12

12:                                               ; preds = %0
  call void @perror(ptr noundef @.str)
  store i32 1, ptr %1, align 4
  br label %49

13:                                               ; preds = %0
  store i64 0, ptr %4, align 8
  store i64 0, ptr %5, align 8
  br label %14

14:                                               ; preds = %31, %13
  %15 = load i64, ptr %5, align 8
  %16 = load i64, ptr %2, align 8
  %17 = icmp ult i64 %15, %16
  br i1 %17, label %18, label %34

18:                                               ; preds = %14
  %19 = load i64, ptr %5, align 8
  %20 = trunc i64 %19 to i32
  %21 = load ptr, ptr %3, align 8
  %22 = load i64, ptr %5, align 8
  %23 = getelementptr inbounds i32, ptr %21, i64 %22
  store i32 %20, ptr %23, align 4
  %24 = load ptr, ptr %3, align 8
  %25 = load i64, ptr %5, align 8
  %26 = getelementptr inbounds i32, ptr %24, i64 %25
  %27 = load i32, ptr %26, align 4
  %28 = sext i32 %27 to i64
  %29 = load i64, ptr %4, align 8
  %30 = add nsw i64 %29, %28
  store i64 %30, ptr %4, align 8
  br label %31

31:                                               ; preds = %18
  %32 = load i64, ptr %5, align 8
  %33 = add i64 %32, 1
  store i64 %33, ptr %5, align 8
  br label %14, !llvm.loop !6

34:                                               ; preds = %14
  %35 = load i64, ptr %4, align 8
  %36 = call i32 (ptr, ...) @printf(ptr noundef @.str.1, i64 noundef %35)
  %37 = call ptr @allocate_vector(i64 noundef 1024)
  store ptr %37, ptr %6, align 8
  %38 = load ptr, ptr %6, align 8
  %39 = icmp ne ptr %38, null
  br i1 %39, label %42, label %40

40:                                               ; preds = %34
  call void @perror(ptr noundef @.str.2)
  %41 = load ptr, ptr %3, align 8
  call void @free(ptr noundef %41) #5
  store i32 1, ptr %1, align 4
  br label %49

42:                                               ; preds = %34
  %43 = load ptr, ptr %6, align 8
  %44 = getelementptr inbounds i32, ptr %43, i64 100
  %45 = load i32, ptr %44, align 4
  %46 = call i32 (ptr, ...) @printf(ptr noundef @.str.3, i32 noundef %45)
  %47 = load ptr, ptr %6, align 8
  call void @free(ptr noundef %47) #5
  %48 = load ptr, ptr %3, align 8
  call void @free(ptr noundef %48) #5
  store i32 0, ptr %1, align 4
  br label %49

49:                                               ; preds = %42, %40, %12
  %50 = load i32, ptr %1, align 4
  ret i32 %50
}

; Function Attrs: nounwind allocsize(0)
declare noalias ptr @malloc(i64 noundef) #1

declare void @perror(ptr noundef) #2

declare i32 @printf(ptr noundef, ...) #2

; Function Attrs: noinline nounwind optnone uwtable
define internal ptr @allocate_vector(i64 noundef %0) #0 {
  %2 = alloca ptr, align 8
  %3 = alloca i64, align 8
  %4 = alloca ptr, align 8
  %5 = alloca i64, align 8
  store i64 %0, ptr %3, align 8
  %6 = load i64, ptr %3, align 8
  %7 = mul i64 %6, 4
  %8 = call noalias ptr @malloc(i64 noundef %7) #4
  store ptr %8, ptr %4, align 8
  %9 = load ptr, ptr %4, align 8
  %10 = icmp ne ptr %9, null
  br i1 %10, label %12, label %11

11:                                               ; preds = %1
  store ptr null, ptr %2, align 8
  br label %28

12:                                               ; preds = %1
  store i64 0, ptr %5, align 8
  br label %13

13:                                               ; preds = %23, %12
  %14 = load i64, ptr %5, align 8
  %15 = load i64, ptr %3, align 8
  %16 = icmp ult i64 %14, %15
  br i1 %16, label %17, label %26

17:                                               ; preds = %13
  %18 = load i64, ptr %5, align 8
  %19 = trunc i64 %18 to i32
  %20 = load ptr, ptr %4, align 8
  %21 = load i64, ptr %5, align 8
  %22 = getelementptr inbounds i32, ptr %20, i64 %21
  store i32 %19, ptr %22, align 4
  br label %23

23:                                               ; preds = %17
  %24 = load i64, ptr %5, align 8
  %25 = add i64 %24, 1
  store i64 %25, ptr %5, align 8
  br label %13, !llvm.loop !8

26:                                               ; preds = %13
  %27 = load ptr, ptr %4, align 8
  store ptr %27, ptr %2, align 8
  br label %28

28:                                               ; preds = %26, %11
  %29 = load ptr, ptr %2, align 8
  ret ptr %29
}

; Function Attrs: nounwind
declare void @free(ptr noundef) #3

declare ptr @hbm_malloc(i64)

attributes #0 = { noinline nounwind optnone uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { nounwind allocsize(0) "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { nounwind "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { nounwind allocsize(0) }
attributes #5 = { nounwind }

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
!8 = distinct !{!8, !7}
