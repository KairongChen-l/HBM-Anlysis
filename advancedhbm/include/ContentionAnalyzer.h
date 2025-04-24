#ifndef MYHBM_CONTENTION_ANALYZER_H
#define MYHBM_CONTENTION_ANALYZER_H

#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/Analysis/LoopInfo.h"
#include "AnalysisTypes.h"

namespace MyHBM
{

    // 多线程竞争分析器
    class ContentionAnalyzer
    {
    public:
        ContentionAnalyzer(llvm::LoopInfo &LI) : LI(LI) {}

        // 分析竞争
        ContentionInfo analyzeContention(llvm::Value *AllocPtr, llvm::Function &F);

        // 检测伪共享
        bool detectFalseSharing(llvm::Value *Ptr, unsigned elemSize, unsigned threadCount);

        // 检测带宽竞争
        bool detectBandwidthContention(llvm::Value *Ptr, llvm::Loop *L, unsigned threadCount);


    private:
        llvm::LoopInfo &LI;
    };

} // namespace MyHBM

#endif // MYHBM_CONTENTION_ANALYZER_H