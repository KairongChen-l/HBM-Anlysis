#ifndef MYHBM_DATA_FLOW_ANALYZER_H
#define MYHBM_DATA_FLOW_ANALYZER_H

#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "AnalysisTypes.h"
#include <set>
using namespace llvm;
namespace MyHBM
{

    // 数据流和生命周期分析器
    class DataFlowAnalyzer
    {
    public:
        DataFlowAnalyzer() = default;

        // 分析数据流
        DataFlowInfo analyzeDataFlow(llvm::Value *AllocPtr, llvm::Function &F);

        // 找出可能的阶段转换点
        std::set<llvm::BasicBlock *> findPhaseTransitionPoints(llvm::Value *Ptr, llvm::Function &F);

        // 判断一个条件是否依赖于指针
        bool isPtrValueDependent(llvm::Value *Cond, llvm::Value *Ptr);

        // 判断两个指令是否相近
        bool isInstructionNear(llvm::Instruction *I1, llvm::Value *I2, unsigned threshold);

        // 计算两个基本块的近似距离
        unsigned getApproximateBlockDistance(llvm::BasicBlock *BB1, llvm::BasicBlock *BB2);

        bool dominates(BasicBlock *A, BasicBlock *B);

        bool isPotentiallyReachableFromTo(BasicBlock *From, BasicBlock *To, void *domTree, void *postDomTree, bool exact);

    private:
        // 内部辅助方法
    };

} // namespace MyHBM

#endif // MYHBM_DATA_FLOW_ANALYZER_H