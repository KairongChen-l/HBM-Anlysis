#include "LoopUtils.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/CFG.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
using namespace llvm;
namespace MyHBM
{

    namespace LoopUtils
    {

        // 获取循环的估计迭代次数
        uint64_t getLoopTripCount(Loop *L,llvm::ScalarEvolution &SE)
        {
            // errs() << "===== Function:getLoopTripCount-L-SE =====\n";
            if (!L)
                return 1;

            // 首先尝试使用LLVM的精确计算方法
            std::optional<unsigned> TripCount = SE.getSmallConstantTripCount(L);
            if (TripCount.has_value())
                return TripCount.value();

            // 尝试使用最大的常量计算方法
            std::optional<unsigned> MaxTripCount = SE.getSmallConstantMaxTripCount(L);
            if (MaxTripCount.has_value())
                return MaxTripCount.value();

            // 使用BackedgeTakenCount进行估计
            const SCEV *BEC = SE.getBackedgeTakenCount(L);
            if (auto *SC = dyn_cast<SCEVConstant>(BEC))
            {
                const APInt &val = SC->getAPInt();
                if (!val.isMaxValue())
                    return val.getZExtValue() + 1;
            }

            // 如果无法精确计算，尝试估计
            BasicBlock *Header = L->getHeader();
            BasicBlock *Latch = L->getLoopLatch();

            if (Header && Latch)
            {
                for (BasicBlock *Pred : predecessors(Header))
                {
                    if (Pred != Latch && L->contains(Pred))
                    {
                        // 尝试找到循环条件
                        if (auto *BI = dyn_cast<BranchInst>(Pred->getTerminator()))
                        {
                            if (BI->isConditional())
                            {
                                Value *Cond = BI->getCondition();
                                if (auto *ICmp = dyn_cast<ICmpInst>(Cond))
                                {
                                    // 分析循环条件，尝试估计迭代次数
                                    Value *Op0 = ICmp->getOperand(0);
                                    Value *Op1 = ICmp->getOperand(1);

                                    // 简单情况：for(i=0; i<N; i++)
                                    if (isa<ConstantInt>(Op1) && SE.isSCEVable(Op0->getType()))
                                    {
                                        const SCEV *OpSCEV = SE.getSCEV(Op0);
                                        if (isa<SCEVAddRecExpr>(OpSCEV))
                                        {
                                            uint64_t ConstVal = cast<ConstantInt>(Op1)->getZExtValue();
                                            // 根据比较条件类型估计
                                            switch (ICmp->getPredicate())
                                            {
                                            case CmpInst::ICMP_SLT:
                                            case CmpInst::ICMP_ULT:
                                                return ConstVal; // i < N -> N次
                                            case CmpInst::ICMP_SLE:
                                            case CmpInst::ICMP_ULE:
                                                return ConstVal + 1; // i <= N -> N+1次
                                            case CmpInst::ICMP_SGT:
                                            case CmpInst::ICMP_UGT:
                                                // 处理递减循环
                                                if (ConstVal == 0)
                                                {
                                                    // 尝试确定起始值
                                                    auto *AddRec = cast<SCEVAddRecExpr>(OpSCEV);
                                                    if (auto *Start = dyn_cast<SCEVConstant>(AddRec->getStart()))
                                                    {
                                                        return Start->getValue()->getZExtValue();
                                                    }
                                                }
                                                break;
                                            case CmpInst::ICMP_SGE:
                                            case CmpInst::ICMP_UGE:
                                                // 处理递减循环: i >= 0 -> N+1次
                                                if (ConstVal == 0)
                                                {
                                                    auto *AddRec = cast<SCEVAddRecExpr>(OpSCEV);
                                                    if (auto *Start = dyn_cast<SCEVConstant>(AddRec->getStart()))
                                                    {
                                                        return Start->getValue()->getZExtValue() + 1;
                                                    }
                                                }
                                                break;
                                            default:
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 如果上述分析都分析不出来的话，就直接返回估计值：100
            return 100;
        }

        // 判断循环是否被标记为可向量化
        bool isLoopMarkedVectorizable(const Loop *L)
        {
            // errs() << "===== Function:isLoopMarkedVectorizable =====\n";
            if (!L || !L->getLoopID())
                return false;

            MDNode *LoopID = L->getLoopID();
            // 检查是否有向量化元数据
            for (unsigned i = 1, e = LoopID->getNumOperands(); i < e; ++i)
            {
                MDNode *MD = dyn_cast<MDNode>(LoopID->getOperand(i));
                if (!MD)
                    continue;

                MDString *S = dyn_cast<MDString>(MD->getOperand(0));
                if (!S)
                    continue;

                // 检查向量化标记
                if (S->getString().equals("llvm.loop.vectorize.enable") ||
                    S->getString().equals("llvm.loop.parallel_accesses"))
                    return true;
            }

            return false;
        }
    } // namespace LoopUtils
} // namespace MyHBM
