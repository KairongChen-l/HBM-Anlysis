#ifndef MYHBM_FUNCTION_ANALYSIS_PASS_H
#define MYHBM_FUNCTION_ANALYSIS_PASS_H

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "MallocRecord.h"

namespace MyHBM
{

    class FunctionAnalysisPass : public llvm::AnalysisInfoMixin<FunctionAnalysisPass>
    {
    public:
        using Result = FunctionMallocInfo;

        // 主要分析入口点
        Result run(llvm::Function &F, llvm::FunctionAnalysisManager &FAM);

        // 静态方法 - 分析malloc调用
        double analyzeMallocStatic(
            llvm::CallInst *CI,
            llvm::Function &F,
            llvm::LoopInfo &LI,
            llvm::ScalarEvolution &SE,
            llvm::AAResults &AA,
            llvm::MemorySSA &MSSA,
            const llvm::LoopAccessInfo *LAI,
            MallocRecord &MR);

        // 匹配malloc对应的free调用
        //void matchFreeCalls(FunctionMallocInfo &FMI, std::vector<llvm::CallInst *> &freeCalls);
        // bool isAllocationFunction(StringRef FuncName);
        // void analyzePotentialAllocation(CallInst *CI, Function &F, FunctionMallocInfo &FMI,
                                        // const DataLayout &DL, uint64_t minAllocationSize,
                                        // ScalarEvolution &SE, LoopInfo &LI, AAResults &AA, MemorySSA &MSSA);
        void setSourceLocation(llvm::CallInst *CI, llvm::Function &F, MyHBM::MallocRecord &MR);
        // 用于PassBuilder的注册
        static llvm::AnalysisKey Key;
        friend llvm::AnalysisInfoMixin<FunctionAnalysisPass>;
 
    };

} // namespace MyHBM

#endif // MYHBM_FUNCTION_ANALYSIS_PASS_H