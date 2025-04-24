; ModuleID = 'test_program.c'
source_filename = "test_program.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@.str = private unnamed_addr constant [23 x i8] c"Stream access sum: %d\0A\00", align 1
@.str.1 = private unnamed_addr constant [23 x i8] c"Random access sum: %d\0A\00", align 1
@.str.2 = private unnamed_addr constant [22 x i8] c"Nested loops sum: %d\0A\00", align 1
@.str.3 = private unnamed_addr constant [32 x i8] c"Vectorizable operation sum: %d\0A\00", align 1
@.str.4 = private unnamed_addr constant [29 x i8] c"Parallel processing sum: %d\0A\00", align 1
@.str.5 = private unnamed_addr constant [24 x i8] c"Cross function sum: %d\0A\00", align 1
@.str.6 = private unnamed_addr constant [26 x i8] c"Memory allocation failed\0A\00", align 1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @stream_access(ptr noundef %0, i32 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  store ptr %0, ptr %3, align 8
  store i32 %1, ptr %4, align 4
  store i32 0, ptr %5, align 4
  br label %8

8:                                                ; preds = %19, %2
  %9 = load i32, ptr %5, align 4
  %10 = load i32, ptr %4, align 4
  %11 = icmp slt i32 %9, %10
  br i1 %11, label %12, label %22

12:                                               ; preds = %8
  %13 = load i32, ptr %5, align 4
  %14 = mul nsw i32 %13, 2
  %15 = load ptr, ptr %3, align 8
  %16 = load i32, ptr %5, align 4
  %17 = sext i32 %16 to i64
  %18 = getelementptr inbounds i32, ptr %15, i64 %17
  store i32 %14, ptr %18, align 4
  br label %19

19:                                               ; preds = %12
  %20 = load i32, ptr %5, align 4
  %21 = add nsw i32 %20, 1
  store i32 %21, ptr %5, align 4
  br label %8, !llvm.loop !6

22:                                               ; preds = %8
  store i32 0, ptr %6, align 4
  store i32 0, ptr %7, align 4
  br label %23

23:                                               ; preds = %35, %22
  %24 = load i32, ptr %7, align 4
  %25 = load i32, ptr %4, align 4
  %26 = icmp slt i32 %24, %25
  br i1 %26, label %27, label %38

27:                                               ; preds = %23
  %28 = load ptr, ptr %3, align 8
  %29 = load i32, ptr %7, align 4
  %30 = sext i32 %29 to i64
  %31 = getelementptr inbounds i32, ptr %28, i64 %30
  %32 = load i32, ptr %31, align 4
  %33 = load i32, ptr %6, align 4
  %34 = add nsw i32 %33, %32
  store i32 %34, ptr %6, align 4
  br label %35

35:                                               ; preds = %27
  %36 = load i32, ptr %7, align 4
  %37 = add nsw i32 %36, 1
  store i32 %37, ptr %7, align 4
  br label %23, !llvm.loop !8

38:                                               ; preds = %23
  %39 = load i32, ptr %6, align 4
  %40 = call i32 (ptr, ...) @printf(ptr noundef @.str, i32 noundef %39)
  ret void
}

declare i32 @printf(ptr noundef, ...) #1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @random_access(ptr noundef %0, i32 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  %8 = alloca i32, align 4
  %9 = alloca i32, align 4
  store ptr %0, ptr %3, align 8
  store i32 %1, ptr %4, align 4
  call void @srand(i32 noundef 42) #4
  store i32 0, ptr %5, align 4
  br label %10

10:                                               ; preds = %24, %2
  %11 = load i32, ptr %5, align 4
  %12 = load i32, ptr %4, align 4
  %13 = sdiv i32 %12, 10
  %14 = icmp slt i32 %11, %13
  br i1 %14, label %15, label %27

15:                                               ; preds = %10
  %16 = call i32 @rand() #4
  %17 = load i32, ptr %4, align 4
  %18 = srem i32 %16, %17
  store i32 %18, ptr %6, align 4
  %19 = load i32, ptr %5, align 4
  %20 = load ptr, ptr %3, align 8
  %21 = load i32, ptr %6, align 4
  %22 = sext i32 %21 to i64
  %23 = getelementptr inbounds i32, ptr %20, i64 %22
  store i32 %19, ptr %23, align 4
  br label %24

24:                                               ; preds = %15
  %25 = load i32, ptr %5, align 4
  %26 = add nsw i32 %25, 1
  store i32 %26, ptr %5, align 4
  br label %10, !llvm.loop !9

27:                                               ; preds = %10
  store i32 0, ptr %7, align 4
  store i32 0, ptr %8, align 4
  br label %28

28:                                               ; preds = %44, %27
  %29 = load i32, ptr %8, align 4
  %30 = load i32, ptr %4, align 4
  %31 = sdiv i32 %30, 10
  %32 = icmp slt i32 %29, %31
  br i1 %32, label %33, label %47

33:                                               ; preds = %28
  %34 = call i32 @rand() #4
  %35 = load i32, ptr %4, align 4
  %36 = srem i32 %34, %35
  store i32 %36, ptr %9, align 4
  %37 = load ptr, ptr %3, align 8
  %38 = load i32, ptr %9, align 4
  %39 = sext i32 %38 to i64
  %40 = getelementptr inbounds i32, ptr %37, i64 %39
  %41 = load i32, ptr %40, align 4
  %42 = load i32, ptr %7, align 4
  %43 = add nsw i32 %42, %41
  store i32 %43, ptr %7, align 4
  br label %44

44:                                               ; preds = %33
  %45 = load i32, ptr %8, align 4
  %46 = add nsw i32 %45, 1
  store i32 %46, ptr %8, align 4
  br label %28, !llvm.loop !10

47:                                               ; preds = %28
  %48 = load i32, ptr %7, align 4
  %49 = call i32 (ptr, ...) @printf(ptr noundef @.str.1, i32 noundef %48)
  ret void
}

; Function Attrs: nounwind
declare void @srand(i32 noundef) #2

; Function Attrs: nounwind
declare i32 @rand() #2

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @nested_loops(ptr noundef %0, i32 noundef %1, i32 noundef %2) #0 {
  %4 = alloca ptr, align 8
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  %8 = alloca i32, align 4
  %9 = alloca i32, align 4
  %10 = alloca i32, align 4
  %11 = alloca i32, align 4
  store ptr %0, ptr %4, align 8
  store i32 %1, ptr %5, align 4
  store i32 %2, ptr %6, align 4
  store i32 0, ptr %7, align 4
  br label %12

12:                                               ; preds = %37, %3
  %13 = load i32, ptr %7, align 4
  %14 = load i32, ptr %5, align 4
  %15 = icmp slt i32 %13, %14
  br i1 %15, label %16, label %40

16:                                               ; preds = %12
  store i32 0, ptr %8, align 4
  br label %17

17:                                               ; preds = %33, %16
  %18 = load i32, ptr %8, align 4
  %19 = load i32, ptr %6, align 4
  %20 = icmp slt i32 %18, %19
  br i1 %20, label %21, label %36

21:                                               ; preds = %17
  %22 = load i32, ptr %7, align 4
  %23 = load i32, ptr %8, align 4
  %24 = mul nsw i32 %22, %23
  %25 = load ptr, ptr %4, align 8
  %26 = load i32, ptr %7, align 4
  %27 = sext i32 %26 to i64
  %28 = getelementptr inbounds ptr, ptr %25, i64 %27
  %29 = load ptr, ptr %28, align 8
  %30 = load i32, ptr %8, align 4
  %31 = sext i32 %30 to i64
  %32 = getelementptr inbounds i32, ptr %29, i64 %31
  store i32 %24, ptr %32, align 4
  br label %33

33:                                               ; preds = %21
  %34 = load i32, ptr %8, align 4
  %35 = add nsw i32 %34, 1
  store i32 %35, ptr %8, align 4
  br label %17, !llvm.loop !11

36:                                               ; preds = %17
  br label %37

37:                                               ; preds = %36
  %38 = load i32, ptr %7, align 4
  %39 = add nsw i32 %38, 1
  store i32 %39, ptr %7, align 4
  br label %12, !llvm.loop !12

40:                                               ; preds = %12
  store i32 0, ptr %9, align 4
  store i32 0, ptr %10, align 4
  br label %41

41:                                               ; preds = %66, %40
  %42 = load i32, ptr %10, align 4
  %43 = load i32, ptr %6, align 4
  %44 = icmp slt i32 %42, %43
  br i1 %44, label %45, label %69

45:                                               ; preds = %41
  store i32 0, ptr %11, align 4
  br label %46

46:                                               ; preds = %62, %45
  %47 = load i32, ptr %11, align 4
  %48 = load i32, ptr %5, align 4
  %49 = icmp slt i32 %47, %48
  br i1 %49, label %50, label %65

50:                                               ; preds = %46
  %51 = load ptr, ptr %4, align 8
  %52 = load i32, ptr %11, align 4
  %53 = sext i32 %52 to i64
  %54 = getelementptr inbounds ptr, ptr %51, i64 %53
  %55 = load ptr, ptr %54, align 8
  %56 = load i32, ptr %10, align 4
  %57 = sext i32 %56 to i64
  %58 = getelementptr inbounds i32, ptr %55, i64 %57
  %59 = load i32, ptr %58, align 4
  %60 = load i32, ptr %9, align 4
  %61 = add nsw i32 %60, %59
  store i32 %61, ptr %9, align 4
  br label %62

62:                                               ; preds = %50
  %63 = load i32, ptr %11, align 4
  %64 = add nsw i32 %63, 1
  store i32 %64, ptr %11, align 4
  br label %46, !llvm.loop !13

65:                                               ; preds = %46
  br label %66

66:                                               ; preds = %65
  %67 = load i32, ptr %10, align 4
  %68 = add nsw i32 %67, 1
  store i32 %68, ptr %10, align 4
  br label %41, !llvm.loop !14

69:                                               ; preds = %41
  %70 = load i32, ptr %9, align 4
  %71 = call i32 (ptr, ...) @printf(ptr noundef @.str.2, i32 noundef %70)
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @vectorizable_operation(ptr noundef %0, i32 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  store ptr %0, ptr %3, align 8
  store i32 %1, ptr %4, align 4
  store i32 0, ptr %5, align 4
  br label %8

8:                                                ; preds = %24, %2
  %9 = load i32, ptr %5, align 4
  %10 = load i32, ptr %4, align 4
  %11 = icmp slt i32 %9, %10
  br i1 %11, label %12, label %27

12:                                               ; preds = %8
  %13 = load ptr, ptr %3, align 8
  %14 = load i32, ptr %5, align 4
  %15 = sext i32 %14 to i64
  %16 = getelementptr inbounds i32, ptr %13, i64 %15
  %17 = load i32, ptr %16, align 4
  %18 = mul nsw i32 %17, 2
  %19 = add nsw i32 %18, 1
  %20 = load ptr, ptr %3, align 8
  %21 = load i32, ptr %5, align 4
  %22 = sext i32 %21 to i64
  %23 = getelementptr inbounds i32, ptr %20, i64 %22
  store i32 %19, ptr %23, align 4
  br label %24

24:                                               ; preds = %12
  %25 = load i32, ptr %5, align 4
  %26 = add nsw i32 %25, 1
  store i32 %26, ptr %5, align 4
  br label %8, !llvm.loop !15

27:                                               ; preds = %8
  store i32 0, ptr %6, align 4
  store i32 0, ptr %7, align 4
  br label %28

28:                                               ; preds = %40, %27
  %29 = load i32, ptr %7, align 4
  %30 = load i32, ptr %4, align 4
  %31 = icmp slt i32 %29, %30
  br i1 %31, label %32, label %43

32:                                               ; preds = %28
  %33 = load ptr, ptr %3, align 8
  %34 = load i32, ptr %7, align 4
  %35 = sext i32 %34 to i64
  %36 = getelementptr inbounds i32, ptr %33, i64 %35
  %37 = load i32, ptr %36, align 4
  %38 = load i32, ptr %6, align 4
  %39 = add nsw i32 %38, %37
  store i32 %39, ptr %6, align 4
  br label %40

40:                                               ; preds = %32
  %41 = load i32, ptr %7, align 4
  %42 = add nsw i32 %41, 1
  store i32 %42, ptr %7, align 4
  br label %28, !llvm.loop !16

43:                                               ; preds = %28
  %44 = load i32, ptr %6, align 4
  %45 = call i32 (ptr, ...) @printf(ptr noundef @.str.3, i32 noundef %44)
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @parallel_processing(ptr noundef %0, i32 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  store ptr %0, ptr %3, align 8
  store i32 %1, ptr %4, align 4
  store i32 0, ptr %5, align 4
  br label %8

8:                                                ; preds = %20, %2
  %9 = load i32, ptr %5, align 4
  %10 = load i32, ptr %4, align 4
  %11 = icmp slt i32 %9, %10
  br i1 %11, label %12, label %23

12:                                               ; preds = %8
  %13 = load i32, ptr %5, align 4
  %14 = load i32, ptr %5, align 4
  %15 = mul nsw i32 %13, %14
  %16 = load ptr, ptr %3, align 8
  %17 = load i32, ptr %5, align 4
  %18 = sext i32 %17 to i64
  %19 = getelementptr inbounds i32, ptr %16, i64 %18
  store i32 %15, ptr %19, align 4
  br label %20

20:                                               ; preds = %12
  %21 = load i32, ptr %5, align 4
  %22 = add nsw i32 %21, 1
  store i32 %22, ptr %5, align 4
  br label %8, !llvm.loop !17

23:                                               ; preds = %8
  store i32 0, ptr %6, align 4
  store i32 0, ptr %7, align 4
  br label %24

24:                                               ; preds = %36, %23
  %25 = load i32, ptr %7, align 4
  %26 = load i32, ptr %4, align 4
  %27 = icmp slt i32 %25, %26
  br i1 %27, label %28, label %39

28:                                               ; preds = %24
  %29 = load ptr, ptr %3, align 8
  %30 = load i32, ptr %7, align 4
  %31 = sext i32 %30 to i64
  %32 = getelementptr inbounds i32, ptr %29, i64 %31
  %33 = load i32, ptr %32, align 4
  %34 = load i32, ptr %6, align 4
  %35 = add nsw i32 %34, %33
  store i32 %35, ptr %6, align 4
  br label %36

36:                                               ; preds = %28
  %37 = load i32, ptr %7, align 4
  %38 = add nsw i32 %37, 1
  store i32 %38, ptr %7, align 4
  br label %24, !llvm.loop !18

39:                                               ; preds = %24
  %40 = load i32, ptr %6, align 4
  %41 = call i32 (ptr, ...) @printf(ptr noundef @.str.4, i32 noundef %40)
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @multidimensional_array() #0 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca ptr, align 8
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  %8 = alloca i32, align 4
  %9 = alloca i32, align 4
  %10 = alloca i32, align 4
  %11 = alloca i32, align 4
  store i32 100, ptr %1, align 4
  store i32 100, ptr %2, align 4
  store i32 100, ptr %3, align 4
  %12 = call noalias ptr @malloc(i64 noundef 800) #5
  store ptr %12, ptr %4, align 8
  store i32 0, ptr %5, align 4
  br label %13

13:                                               ; preds = %39, %0
  %14 = load i32, ptr %5, align 4
  %15 = icmp slt i32 %14, 100
  br i1 %15, label %16, label %42

16:                                               ; preds = %13
  %17 = call noalias ptr @malloc(i64 noundef 800) #5
  %18 = load ptr, ptr %4, align 8
  %19 = load i32, ptr %5, align 4
  %20 = sext i32 %19 to i64
  %21 = getelementptr inbounds ptr, ptr %18, i64 %20
  store ptr %17, ptr %21, align 8
  store i32 0, ptr %6, align 4
  br label %22

22:                                               ; preds = %35, %16
  %23 = load i32, ptr %6, align 4
  %24 = icmp slt i32 %23, 100
  br i1 %24, label %25, label %38

25:                                               ; preds = %22
  %26 = call noalias ptr @malloc(i64 noundef 400) #5
  %27 = load ptr, ptr %4, align 8
  %28 = load i32, ptr %5, align 4
  %29 = sext i32 %28 to i64
  %30 = getelementptr inbounds ptr, ptr %27, i64 %29
  %31 = load ptr, ptr %30, align 8
  %32 = load i32, ptr %6, align 4
  %33 = sext i32 %32 to i64
  %34 = getelementptr inbounds ptr, ptr %31, i64 %33
  store ptr %26, ptr %34, align 8
  br label %35

35:                                               ; preds = %25
  %36 = load i32, ptr %6, align 4
  %37 = add nsw i32 %36, 1
  store i32 %37, ptr %6, align 4
  br label %22, !llvm.loop !19

38:                                               ; preds = %22
  br label %39

39:                                               ; preds = %38
  %40 = load i32, ptr %5, align 4
  %41 = add nsw i32 %40, 1
  store i32 %41, ptr %5, align 4
  br label %13, !llvm.loop !20

42:                                               ; preds = %13
  store i32 0, ptr %7, align 4
  br label %43

43:                                               ; preds = %80, %42
  %44 = load i32, ptr %7, align 4
  %45 = icmp slt i32 %44, 100
  br i1 %45, label %46, label %83

46:                                               ; preds = %43
  store i32 0, ptr %8, align 4
  br label %47

47:                                               ; preds = %76, %46
  %48 = load i32, ptr %8, align 4
  %49 = icmp slt i32 %48, 100
  br i1 %49, label %50, label %79

50:                                               ; preds = %47
  store i32 0, ptr %9, align 4
  br label %51

51:                                               ; preds = %72, %50
  %52 = load i32, ptr %9, align 4
  %53 = icmp slt i32 %52, 100
  br i1 %53, label %54, label %75

54:                                               ; preds = %51
  %55 = load i32, ptr %7, align 4
  %56 = load i32, ptr %8, align 4
  %57 = add nsw i32 %55, %56
  %58 = load i32, ptr %9, align 4
  %59 = add nsw i32 %57, %58
  %60 = load ptr, ptr %4, align 8
  %61 = load i32, ptr %7, align 4
  %62 = sext i32 %61 to i64
  %63 = getelementptr inbounds ptr, ptr %60, i64 %62
  %64 = load ptr, ptr %63, align 8
  %65 = load i32, ptr %8, align 4
  %66 = sext i32 %65 to i64
  %67 = getelementptr inbounds ptr, ptr %64, i64 %66
  %68 = load ptr, ptr %67, align 8
  %69 = load i32, ptr %9, align 4
  %70 = sext i32 %69 to i64
  %71 = getelementptr inbounds i32, ptr %68, i64 %70
  store i32 %59, ptr %71, align 4
  br label %72

72:                                               ; preds = %54
  %73 = load i32, ptr %9, align 4
  %74 = add nsw i32 %73, 1
  store i32 %74, ptr %9, align 4
  br label %51, !llvm.loop !21

75:                                               ; preds = %51
  br label %76

76:                                               ; preds = %75
  %77 = load i32, ptr %8, align 4
  %78 = add nsw i32 %77, 1
  store i32 %78, ptr %8, align 4
  br label %47, !llvm.loop !22

79:                                               ; preds = %47
  br label %80

80:                                               ; preds = %79
  %81 = load i32, ptr %7, align 4
  %82 = add nsw i32 %81, 1
  store i32 %82, ptr %7, align 4
  br label %43, !llvm.loop !23

83:                                               ; preds = %43
  store i32 0, ptr %10, align 4
  br label %84

84:                                               ; preds = %110, %83
  %85 = load i32, ptr %10, align 4
  %86 = icmp slt i32 %85, 100
  br i1 %86, label %87, label %113

87:                                               ; preds = %84
  store i32 0, ptr %11, align 4
  br label %88

88:                                               ; preds = %101, %87
  %89 = load i32, ptr %11, align 4
  %90 = icmp slt i32 %89, 100
  br i1 %90, label %91, label %104

91:                                               ; preds = %88
  %92 = load ptr, ptr %4, align 8
  %93 = load i32, ptr %10, align 4
  %94 = sext i32 %93 to i64
  %95 = getelementptr inbounds ptr, ptr %92, i64 %94
  %96 = load ptr, ptr %95, align 8
  %97 = load i32, ptr %11, align 4
  %98 = sext i32 %97 to i64
  %99 = getelementptr inbounds ptr, ptr %96, i64 %98
  %100 = load ptr, ptr %99, align 8
  call void @free(ptr noundef %100) #4
  br label %101

101:                                              ; preds = %91
  %102 = load i32, ptr %11, align 4
  %103 = add nsw i32 %102, 1
  store i32 %103, ptr %11, align 4
  br label %88, !llvm.loop !24

104:                                              ; preds = %88
  %105 = load ptr, ptr %4, align 8
  %106 = load i32, ptr %10, align 4
  %107 = sext i32 %106 to i64
  %108 = getelementptr inbounds ptr, ptr %105, i64 %107
  %109 = load ptr, ptr %108, align 8
  call void @free(ptr noundef %109) #4
  br label %110

110:                                              ; preds = %104
  %111 = load i32, ptr %10, align 4
  %112 = add nsw i32 %111, 1
  store i32 %112, ptr %10, align 4
  br label %84, !llvm.loop !25

113:                                              ; preds = %84
  %114 = load ptr, ptr %4, align 8
  call void @free(ptr noundef %114) #4
  ret void
}

; Function Attrs: nounwind allocsize(0)
declare noalias ptr @malloc(i64 noundef) #3

; Function Attrs: nounwind
declare void @free(ptr noundef) #2

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @process_data(ptr noundef %0, i32 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  store ptr %0, ptr %3, align 8
  store i32 %1, ptr %4, align 4
  store i32 0, ptr %5, align 4
  br label %6

6:                                                ; preds = %18, %2
  %7 = load i32, ptr %5, align 4
  %8 = load i32, ptr %4, align 4
  %9 = icmp slt i32 %7, %8
  br i1 %9, label %10, label %21

10:                                               ; preds = %6
  %11 = load i32, ptr %5, align 4
  %12 = load ptr, ptr %3, align 8
  %13 = load i32, ptr %5, align 4
  %14 = sext i32 %13 to i64
  %15 = getelementptr inbounds i32, ptr %12, i64 %14
  %16 = load i32, ptr %15, align 4
  %17 = add nsw i32 %16, %11
  store i32 %17, ptr %15, align 4
  br label %18

18:                                               ; preds = %10
  %19 = load i32, ptr %5, align 4
  %20 = add nsw i32 %19, 1
  store i32 %20, ptr %5, align 4
  br label %6, !llvm.loop !26

21:                                               ; preds = %6
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @cross_function_memory() #0 {
  %1 = alloca i32, align 4
  %2 = alloca ptr, align 8
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  store i32 1000000, ptr %1, align 4
  %6 = call noalias ptr @malloc(i64 noundef 4000000) #5
  store ptr %6, ptr %2, align 8
  store i32 0, ptr %3, align 4
  br label %7

7:                                                ; preds = %16, %0
  %8 = load i32, ptr %3, align 4
  %9 = icmp slt i32 %8, 1000000
  br i1 %9, label %10, label %19

10:                                               ; preds = %7
  %11 = load i32, ptr %3, align 4
  %12 = load ptr, ptr %2, align 8
  %13 = load i32, ptr %3, align 4
  %14 = sext i32 %13 to i64
  %15 = getelementptr inbounds i32, ptr %12, i64 %14
  store i32 %11, ptr %15, align 4
  br label %16

16:                                               ; preds = %10
  %17 = load i32, ptr %3, align 4
  %18 = add nsw i32 %17, 1
  store i32 %18, ptr %3, align 4
  br label %7, !llvm.loop !27

19:                                               ; preds = %7
  %20 = load ptr, ptr %2, align 8
  call void @process_data(ptr noundef %20, i32 noundef 1000000)
  store i32 0, ptr %4, align 4
  store i32 0, ptr %5, align 4
  br label %21

21:                                               ; preds = %32, %19
  %22 = load i32, ptr %5, align 4
  %23 = icmp slt i32 %22, 1000000
  br i1 %23, label %24, label %35

24:                                               ; preds = %21
  %25 = load ptr, ptr %2, align 8
  %26 = load i32, ptr %5, align 4
  %27 = sext i32 %26 to i64
  %28 = getelementptr inbounds i32, ptr %25, i64 %27
  %29 = load i32, ptr %28, align 4
  %30 = load i32, ptr %4, align 4
  %31 = add nsw i32 %30, %29
  store i32 %31, ptr %4, align 4
  br label %32

32:                                               ; preds = %24
  %33 = load i32, ptr %5, align 4
  %34 = add nsw i32 %33, 1
  store i32 %34, ptr %5, align 4
  br label %21, !llvm.loop !28

35:                                               ; preds = %21
  %36 = load i32, ptr %4, align 4
  %37 = call i32 (ptr, ...) @printf(ptr noundef @.str.5, i32 noundef %36)
  %38 = load ptr, ptr %2, align 8
  call void @free(ptr noundef %38) #4
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca ptr, align 8
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca ptr, align 8
  %7 = alloca i32, align 4
  %8 = alloca i32, align 4
  %9 = alloca ptr, align 8
  %10 = alloca i32, align 4
  store i32 0, ptr %1, align 4
  store i32 10000000, ptr %2, align 4
  %11 = call noalias ptr @malloc(i64 noundef 40000000) #5
  store ptr %11, ptr %3, align 8
  %12 = load ptr, ptr %3, align 8
  %13 = icmp ne ptr %12, null
  br i1 %13, label %16, label %14

14:                                               ; preds = %0
  %15 = call i32 (ptr, ...) @printf(ptr noundef @.str.6)
  store i32 1, ptr %1, align 4
  br label %65

16:                                               ; preds = %0
  %17 = load ptr, ptr %3, align 8
  call void @stream_access(ptr noundef %17, i32 noundef 10000000)
  %18 = load ptr, ptr %3, align 8
  call void @random_access(ptr noundef %18, i32 noundef 10000000)
  %19 = load ptr, ptr %3, align 8
  call void @vectorizable_operation(ptr noundef %19, i32 noundef 10000000)
  store i32 1000, ptr %4, align 4
  store i32 1000, ptr %5, align 4
  %20 = call noalias ptr @malloc(i64 noundef 8000) #5
  store ptr %20, ptr %6, align 8
  store i32 0, ptr %7, align 4
  br label %21

21:                                               ; preds = %30, %16
  %22 = load i32, ptr %7, align 4
  %23 = icmp slt i32 %22, 1000
  br i1 %23, label %24, label %33

24:                                               ; preds = %21
  %25 = call noalias ptr @malloc(i64 noundef 4000) #5
  %26 = load ptr, ptr %6, align 8
  %27 = load i32, ptr %7, align 4
  %28 = sext i32 %27 to i64
  %29 = getelementptr inbounds ptr, ptr %26, i64 %28
  store ptr %25, ptr %29, align 8
  br label %30

30:                                               ; preds = %24
  %31 = load i32, ptr %7, align 4
  %32 = add nsw i32 %31, 1
  store i32 %32, ptr %7, align 4
  br label %21, !llvm.loop !29

33:                                               ; preds = %21
  %34 = load ptr, ptr %6, align 8
  call void @nested_loops(ptr noundef %34, i32 noundef 1000, i32 noundef 1000)
  store i32 0, ptr %8, align 4
  br label %35

35:                                               ; preds = %44, %33
  %36 = load i32, ptr %8, align 4
  %37 = icmp slt i32 %36, 1000
  br i1 %37, label %38, label %47

38:                                               ; preds = %35
  %39 = load ptr, ptr %6, align 8
  %40 = load i32, ptr %8, align 4
  %41 = sext i32 %40 to i64
  %42 = getelementptr inbounds ptr, ptr %39, i64 %41
  %43 = load ptr, ptr %42, align 8
  call void @free(ptr noundef %43) #4
  br label %44

44:                                               ; preds = %38
  %45 = load i32, ptr %8, align 4
  %46 = add nsw i32 %45, 1
  store i32 %46, ptr %8, align 4
  br label %35, !llvm.loop !30

47:                                               ; preds = %35
  %48 = load ptr, ptr %6, align 8
  call void @free(ptr noundef %48) #4
  call void @multidimensional_array()
  call void @cross_function_memory()
  %49 = load ptr, ptr %3, align 8
  call void @free(ptr noundef %49) #4
  %50 = call noalias ptr @malloc(i64 noundef 40) #5
  store ptr %50, ptr %9, align 8
  store i32 0, ptr %10, align 4
  br label %51

51:                                               ; preds = %60, %47
  %52 = load i32, ptr %10, align 4
  %53 = icmp slt i32 %52, 10
  br i1 %53, label %54, label %63

54:                                               ; preds = %51
  %55 = load i32, ptr %10, align 4
  %56 = load ptr, ptr %9, align 8
  %57 = load i32, ptr %10, align 4
  %58 = sext i32 %57 to i64
  %59 = getelementptr inbounds i32, ptr %56, i64 %58
  store i32 %55, ptr %59, align 4
  br label %60

60:                                               ; preds = %54
  %61 = load i32, ptr %10, align 4
  %62 = add nsw i32 %61, 1
  store i32 %62, ptr %10, align 4
  br label %51, !llvm.loop !31

63:                                               ; preds = %51
  %64 = load ptr, ptr %9, align 8
  call void @free(ptr noundef %64) #4
  store i32 0, ptr %1, align 4
  br label %65

65:                                               ; preds = %63, %14
  %66 = load i32, ptr %1, align 4
  ret i32 %66
}

attributes #0 = { noinline nounwind optnone uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { nounwind "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { nounwind allocsize(0) "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { nounwind }
attributes #5 = { nounwind allocsize(0) }

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
!9 = distinct !{!9, !7}
!10 = distinct !{!10, !7}
!11 = distinct !{!11, !7}
!12 = distinct !{!12, !7}
!13 = distinct !{!13, !7}
!14 = distinct !{!14, !7}
!15 = distinct !{!15, !7}
!16 = distinct !{!16, !7}
!17 = distinct !{!17, !7}
!18 = distinct !{!18, !7}
!19 = distinct !{!19, !7}
!20 = distinct !{!20, !7}
!21 = distinct !{!21, !7}
!22 = distinct !{!22, !7}
!23 = distinct !{!23, !7}
!24 = distinct !{!24, !7}
!25 = distinct !{!25, !7}
!26 = distinct !{!26, !7}
!27 = distinct !{!27, !7}
!28 = distinct !{!28, !7}
!29 = distinct !{!29, !7}
!30 = distinct !{!30, !7}
!31 = distinct !{!31, !7}
