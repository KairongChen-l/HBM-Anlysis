; ModuleID = 'verify_free.c'
source_filename = "verify_free.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@.str = private unnamed_addr constant [39 x i8] c"Loop %zu iterations (32 KiB each) \E2\80\A6\0A\00", align 1
@.str.2 = private unnamed_addr constant [11 x i8] c"hbm_malloc\00", align 1
@.str.3 = private unnamed_addr constant [41 x i8] c"All allocations freed \E2\80\94 test finished.\00", align 1

; Function Attrs: nounwind uwtable
define dso_local noundef i32 @main(i32 noundef %0, ptr nocapture noundef readonly %1) local_unnamed_addr #0 {
  %3 = icmp sgt i32 %0, 1
  br i1 %3, label %6, label %4

4:                                                ; preds = %2
  %5 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str, i64 noundef 100000)
  br label %12

6:                                                ; preds = %2
  %7 = getelementptr inbounds ptr, ptr %1, i64 1
  %8 = load ptr, ptr %7, align 8, !tbaa !5
  %9 = tail call i64 @strtoull(ptr nocapture noundef %8, ptr noundef null, i32 noundef 10) #5
  %10 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str, i64 noundef %9)
  %11 = icmp eq i64 %9, 0
  br i1 %11, label %38, label %12

12:                                               ; preds = %4, %6
  %13 = phi i64 [ 100000, %4 ], [ %9, %6 ]
  br label %14

14:                                               ; preds = %12, %34
  %15 = phi i64 [ %35, %34 ], [ 0, %12 ]
  %16 = tail call ptr @hbm_malloc(i64 noundef 1073741824) #5
  %17 = icmp eq ptr %16, null
  br i1 %17, label %37, label %18

18:                                               ; preds = %14, %18
  %19 = phi i64 [ %31, %18 ], [ 0, %14 ]
  %20 = phi <16 x i8> [ %32, %18 ], [ <i8 0, i8 1, i8 2, i8 3, i8 4, i8 5, i8 6, i8 7, i8 8, i8 9, i8 10, i8 11, i8 12, i8 13, i8 14, i8 15>, %14 ]
  %21 = getelementptr inbounds i8, ptr %16, i64 %19
  store <16 x i8> %20, ptr %21, align 1, !tbaa !9
  %22 = or disjoint i64 %19, 16
  %23 = add <16 x i8> %20, <i8 16, i8 16, i8 16, i8 16, i8 16, i8 16, i8 16, i8 16, i8 16, i8 16, i8 16, i8 16, i8 16, i8 16, i8 16, i8 16>
  %24 = getelementptr inbounds i8, ptr %16, i64 %22
  store <16 x i8> %23, ptr %24, align 1, !tbaa !9
  %25 = or disjoint i64 %19, 32
  %26 = add <16 x i8> %20, <i8 32, i8 32, i8 32, i8 32, i8 32, i8 32, i8 32, i8 32, i8 32, i8 32, i8 32, i8 32, i8 32, i8 32, i8 32, i8 32>
  %27 = getelementptr inbounds i8, ptr %16, i64 %25
  store <16 x i8> %26, ptr %27, align 1, !tbaa !9
  %28 = or disjoint i64 %19, 48
  %29 = add <16 x i8> %20, <i8 48, i8 48, i8 48, i8 48, i8 48, i8 48, i8 48, i8 48, i8 48, i8 48, i8 48, i8 48, i8 48, i8 48, i8 48, i8 48>
  %30 = getelementptr inbounds i8, ptr %16, i64 %28
  store <16 x i8> %29, ptr %30, align 1, !tbaa !9
  %31 = add nuw nsw i64 %19, 64
  %32 = add <16 x i8> %20, <i8 64, i8 64, i8 64, i8 64, i8 64, i8 64, i8 64, i8 64, i8 64, i8 64, i8 64, i8 64, i8 64, i8 64, i8 64, i8 64>
  %33 = icmp eq i64 %31, 1073741824
  br i1 %33, label %34, label %18, !llvm.loop !10

34:                                               ; preds = %18
  tail call void @free(ptr noundef nonnull %16) #5
  %35 = add nuw i64 %15, 1
  %36 = icmp eq i64 %35, %13
  br i1 %36, label %38, label %14, !llvm.loop !14

37:                                               ; preds = %14
  tail call void @perror(ptr noundef nonnull @.str.2) #6
  br label %40

38:                                               ; preds = %34, %6
  %39 = tail call i32 @puts(ptr noundef nonnull dereferenceable(1) @.str.3)
  br label %40

40:                                               ; preds = %37, %38
  %41 = phi i32 [ 0, %38 ], [ 1, %37 ]
  ret i32 %41
}

; Function Attrs: mustprogress nofree nounwind willreturn
declare i64 @strtoull(ptr noundef readonly, ptr nocapture noundef, i32 noundef) local_unnamed_addr #1

; Function Attrs: nofree nounwind
declare noundef i32 @printf(ptr nocapture noundef readonly, ...) local_unnamed_addr #2

; Function Attrs: nofree nounwind
declare void @perror(ptr nocapture noundef readonly) local_unnamed_addr #2

; Function Attrs: mustprogress nounwind willreturn allockind("free") memory(argmem: readwrite, inaccessiblemem: readwrite)
declare void @free(ptr allocptr nocapture noundef) local_unnamed_addr #3

declare ptr @hbm_malloc(i64 noundef) local_unnamed_addr #4

; Function Attrs: nofree nounwind
declare noundef i32 @puts(ptr nocapture noundef readonly) local_unnamed_addr #2

attributes #0 = { nounwind uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { mustprogress nofree nounwind willreturn "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { nofree nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { mustprogress nounwind willreturn allockind("free") memory(argmem: readwrite, inaccessiblemem: readwrite) "alloc-family"="malloc" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #5 = { nounwind }
attributes #6 = { cold }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{!"Ubuntu clang version 18.1.8 (11~20.04.2)"}
!5 = !{!6, !6, i64 0}
!6 = !{!"any pointer", !7, i64 0}
!7 = !{!"omnipotent char", !8, i64 0}
!8 = !{!"Simple C/C++ TBAA"}
!9 = !{!7, !7, i64 0}
!10 = distinct !{!10, !11, !12, !13}
!11 = !{!"llvm.loop.mustprogress"}
!12 = !{!"llvm.loop.isvectorized", i32 1}
!13 = !{!"llvm.loop.unroll.runtime.disable"}
!14 = distinct !{!14, !11}
