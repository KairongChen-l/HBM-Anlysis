#ifndef MYHBM_BANDWIDTH_ANALYZER_H
#define MYHBM_BANDWIDTH_ANALYZER_H

// 测试这三个头文件
#include "TemporalLocalityAnalyzer.h"
#include "BankConflictAnalyzer.h"
#include "DependencyChainAnalyzer.h"

#include "llvm/IR/Instruction.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
// #include "llvm/IR/Type.h"
#include "MallocRecord.h"
#include <unordered_set>

using namespace llvm;
namespace MyHBM
{

    // 内存带宽分析器
    class BandwidthAnalyzer
    {
    public:
        BandwidthAnalyzer(
            llvm::ScalarEvolution &SE,
            llvm::LoopInfo &LI,
            llvm::AAResults &AA,
            llvm::MemorySSA &MSSA)
            : SE(SE), LI(LI), AA(AA), MSSA(MSSA), TLA(SE, LI), BCA(SE, LI) {}

        // 计算访问指令的带宽得分
        double computeAccessScore(
            llvm::Instruction *I,
            bool isWrite,
            MallocRecord &MR);

        // 计算内存混乱度评分
        double computeAccessChaosScore(llvm::Value *BasePtr);

        // 计算MemorySSA结构复杂度分析
        double computeMemorySSAStructureScore(const llvm::Instruction *I);

        // 计算带宽得分
        double computeBandwidthScore(uint64_t approximateBytes, double approximateTime);

        // 递归分析指针用户以获取带宽使用信息
        void explorePointerUsers(
            llvm::Value *RootPtr,
            llvm::Value *V,
            double &Score,
            MallocRecord &MR,
            std::unordered_set<llvm::Value *> &Visited);

    private:
        llvm::ScalarEvolution &SE;
        llvm::LoopInfo &LI;
        llvm::AAResults &AA;
        llvm::MemorySSA &MSSA;

        // 以下为新添加的分析
        TemporalLocalityAnalyzer TLA;
        // Add a temporal locality score field to track this aspect
        double computeTemporalLocalityScore(Value *Ptr, Function *F);

        // Add this with other analyzers
        BankConflictAnalyzer BCA;

        // Add bank conflict analysis method
        double analyzeBankConflicts(Value *Ptr, MallocRecord &MR);
        
        double analyzeDependencyChains(Value *Ptr, MallocRecord &MR);
    };

} // namespace MyHBM

#endif // MYHBM_BANDWIDTH_ANALYZER_H