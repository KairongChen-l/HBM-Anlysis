#ifndef MYHBM_PARALLELISM_ANALYZER_H
#define MYHBM_PARALLELISM_ANALYZER_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "AnalysisTypes.h"
#include "llvm/Analysis/LoopInfo.h"

namespace MyHBM
{

  // 并行访问分析器
  class ParallelismAnalyzer
  {
  public:
    ParallelismAnalyzer() = default;

    // 分析指针的线程访问模式
    ThreadAccessPattern analyzeThreadAccess(llvm::Value *Ptr, llvm::Instruction *I);

    // 检测是否为OpenMP并行执行
    bool isOpenMPParallel(llvm::Function &F);

    // 检测是否为CUDA并行执行
    bool isCUDAParallel(llvm::Function &F);

    // 检测是否为TBB并行执行
    bool isTBBParallel(llvm::Function &F);

    // 估计并行执行的线程数
    unsigned estimateParallelThreads(llvm::Function &F);

    // 检查是否为原子访问
    bool isAtomicAccess(llvm::Instruction *I);

    // 检查是否有并行循环元数据
    bool hasParallelLoopMetadata(llvm::Loop *L);

    // 检测是否可能存在伪共享
    bool detectFalseSharing(llvm::Value *Ptr, const llvm::DataLayout &DL);

    // 检测函数是否使用并行运行时，这个就是简单地检查是否使用了并行库的函数
    bool detectParallelRuntime(llvm::Function &F);

    // 检查是否为线程依赖的访问（通过线程ID索引）
    bool isThreadDependentAccess(llvm::Value *Ptr);

    // 检查ID是否与线程ID相关
    bool isThreadIDRelated(llvm::Value *V);

    bool isPtrDerivedFrom(llvm::Value *Derived, llvm::Value *Base);
  };

} // namespace MyHBM

#endif // MYHBM_PARALLELISM_ANALYZER_H