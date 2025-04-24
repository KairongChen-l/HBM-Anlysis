// StrideAnalyzer.h
#ifndef MYHBM_STRIDE_ANALYZER_H
#define MYHBM_STRIDE_ANALYZER_H

#include "llvm/IR/Value.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "AnalysisTypes.h"
#include "llvm/Analysis/AliasAnalysis.h"

namespace MyHBM
{

    // 内存访问步长分析器,在创建类的时候需要把SE传递进来
    class StrideAnalyzer
    {
    public:
        // 构造函数接收ScalarEvolution引用，用于步长计算
        StrideAnalyzer(llvm::ScalarEvolution &SE) : SE(SE) {}

        // 分析GEP指令的步长类型
        StrideType analyzeGEPStride(llvm::GetElementPtrInst *GEP);

        // 分析是否为流式访问模式
        bool isStreamingAccess(llvm::Value *Ptr, llvm::Loop *L, llvm::AAResults &AA);

    private:
        llvm::ScalarEvolution &SE;
    };

} // namespace MyHBM

#endif // MYHBM_STRIDE_ANALYZER_H