#include "LoopAnalyzer.h"
#include "LoopUtils.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/ADT/SetVector.h"
#include "clang/AST/Expr.h"
#include <cmath>
#include <optional>

using namespace llvm;
using namespace MyHBM;

// 获取循环的估计迭代次数
uint64_t LoopAnalyzer::getLoopTripCount(Loop *L)
{
    // errs() << "===== Function:getLoopTripCount =====\n";
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

bool isArithmeticOrBitwise(const Instruction &I)
{
    // errs() << "===== Function:isArithmeticOrBitwise =====\n";
    switch (I.getOpcode())
    {
    // 算术操作
    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::FDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
    // 按位操作
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
        return true;
    default:
        return false;
    }
}

// 分析嵌套循环的访存特性
double LoopAnalyzer::analyzeNestedLoops(Loop *L, Value *Ptr)
{
    // errs() << "===== Function:analyzeNestedLoops =====\n";
    if (!L || !Ptr)
        return 0.0;

    // 计算循环嵌套深度
    unsigned nestDepth = 1;
    Loop *Parent = L->getParentLoop();
    while (Parent)
    {
        nestDepth++;
        Parent = Parent->getParentLoop();
    }

    // 收集所有嵌套循环，从外到内
    SmallVector<Loop *, 4> LoopHierarchy;
    Loop *CurLoop = L;
    while (CurLoop)
    {
        LoopHierarchy.push_back(CurLoop);
        CurLoop = CurLoop->getParentLoop();
    }

    // 分析每层循环的迭代范围和步长
    double totalScore = 0.0;
    uint64_t estimatedAccesses = 1;
    bool hasRowMajorAccess = false;
    bool hasColumnMajorAccess = false;
    bool hasStrideAccess = false;
    bool hasDiagonalAccess = false;
    int strideSize = 0;

    // 从外层到内层分析
    std::reverse(LoopHierarchy.begin(), LoopHierarchy.end());
    for (unsigned i = 0; i < LoopHierarchy.size(); ++i)
    {
        Loop *CurL = LoopHierarchy[i];
        uint64_t tripCount = getLoopTripCount(CurL);
        if (tripCount == 0 || tripCount == (uint64_t)-1)
            tripCount = 100; // 默认估计

        estimatedAccesses *= tripCount;

        // 检查该层循环是否有与Ptr相关的内存访问
        bool hasMemAccess = false;
        for (auto *BB : CurL->getBlocks())
        {
            for (auto &I : *BB)
            {
                if (auto *LI = dyn_cast<LoadInst>(&I))
                {
                    if (SE.isSCEVable(LI->getPointerOperand()->getType()))
                    {
                        const SCEV *PtrSCEV = SE.getSCEV(LI->getPointerOperand());
                        // 检查该访问是否与传入的Ptr相关
                        if (SE.isLoopInvariant(PtrSCEV, CurL))
                        {
                            // 该访问在此循环中是循环不变量
                            continue;
                        }
                        hasMemAccess = true;

                        // 分析多维数组访问模式
                        if (auto *GEP = dyn_cast<GetElementPtrInst>(LI->getPointerOperand()))
                        {
                            if (GEP->getNumIndices() > 1)
                            {
                                // 检查不同维度的索引是在哪个循环层变化
                                for (unsigned idx = 0; idx < GEP->getNumIndices(); ++idx)
                                {
                                    Value *IdxOp = GEP->getOperand(idx + 1);
                                    if (!SE.isSCEVable(IdxOp->getType()))
                                        continue;

                                    const SCEV *IdxSCEV = SE.getSCEV(IdxOp);
                                    if (!SE.isLoopInvariant(IdxSCEV, CurL))
                                    {
                                        // 该索引在此循环中变化
                                        if (idx == GEP->getNumIndices() - 1 && i == LoopHierarchy.size() - 1)
                                        {
                                            // 最内层循环访问最后一个维度 - 行优先
                                            hasRowMajorAccess = true;
                                        }
                                        else if (idx == 0 && i == LoopHierarchy.size() - 1)
                                        {
                                            // 最内层循环访问第一个维度 - 列优先
                                            hasColumnMajorAccess = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                else if (auto *SI = dyn_cast<StoreInst>(&I))
                {
                    // 类似的对Store指令进行分析
                    if (SE.isSCEVable(SI->getPointerOperand()->getType()))
                    {
                        const SCEV *PtrSCEV = SE.getSCEV(SI->getPointerOperand());
                        if (!SE.isLoopInvariant(PtrSCEV, CurL))
                        {
                            hasMemAccess = true;
                        }
                    }
                }
            }
        }

        if (hasMemAccess)
        {
            // 该层循环有相关内存访问，计算分数
            double loopScore = 1.0;

            // 最内层循环权重最高
            if (i == LoopHierarchy.size() - 1)
            {
                loopScore *= 2.0;
            }

            // 嵌套很深的循环权重高
            if (LoopHierarchy.size() >= 3)
            {
                loopScore *= 1.5;
            }

            totalScore += loopScore;
        }
    }

    // 基于访问模式调整分数
    if (hasRowMajorAccess)
    {
        totalScore *= 1.2; // 行优先模式通常效率高
    }
    else if (hasColumnMajorAccess)
    {
        totalScore *= 0.8; // 列优先模式可能效率较低
    }

    // 考虑总的访问次数
    double accessFactor = std::log2(estimatedAccesses + 1) / 10.0;
    totalScore *= (1.0 + accessFactor);

    return totalScore;
}
// 检查循环是否为内存密集型
bool LoopAnalyzer::isMemoryIntensiveLoop(Loop *L)
{
    // errs() << "===== Function:isMemoryIntensiveLoop =====\n";
    if (!L)
        return false;

    // 内存操作数
    unsigned memOpCount = 0;
    // 总指令操作数
    unsigned totalOpCount = 0;
    // 计算操作数
    unsigned compOpCount = 0;

    // 收集每种指令类型的数量
    SmallDenseMap<unsigned, unsigned> InstTypeCounts;

    for (auto *BB : L->getBlocks())
    {
        for (auto &I : *BB)
        {
            totalOpCount++;
            unsigned OpCode = I.getOpcode();
            InstTypeCounts[OpCode]++;

            // 内存操作
            if (isa<LoadInst>(I) || isa<StoreInst>(I) ||
                isa<AtomicRMWInst>(I) || isa<AtomicCmpXchgInst>(I) ||
                (isa<CallInst>(I) && cast<CallInst>(I).mayReadOrWriteMemory()))
            {
                memOpCount++;
            }
            // 计算操作
            else if (isArithmeticOrBitwise(I))
            {
                compOpCount++;
            }
        }
    }

    if (totalOpCount == 0)
        return false;

    // 计算内存操作比例
    double memRatio = (double)memOpCount / totalOpCount;

    // 计算计算操作与内存操作比
    double compToMemRatio = memOpCount > 0 ? (double)compOpCount / memOpCount : 0.0;

    // 根据循环特性动态调整阈值
    double memIntensiveThreshold = 0.3; // 默认阈值

    // 如果计算操作和内存操作比例低，可能是内存密集型
    if (compToMemRatio < 1.0)
    {
        memIntensiveThreshold = 0.2; // 降低阈值
    }
    else if (compToMemRatio > 5.0)
    {
        memIntensiveThreshold = 0.4; // 提高阈值
    }

    // 如果循环很小且内存操作占比较高，也视为内存密集型
    if (totalOpCount < 10 && memOpCount >= 2)
    {
        memIntensiveThreshold = 0.2;
    }

    return memRatio > memIntensiveThreshold;
}

// 计算循环嵌套结构得分
double LoopAnalyzer::computeLoopNestingScore(Loop *L)
{
    // errs() << "===== Function:computeLoopNestingScore =====\n";
    if (!L)
        return 0.0;

    double score = 1.0;

    // 检查循环嵌套深度
    unsigned depth = 1;
    Loop *Parent = L->getParentLoop();
    while (Parent)
    {
        depth++;
        Parent = Parent->getParentLoop();
    }

    // 嵌套深度大的循环分数高
    score *= (1.0 + 0.3 * depth * std::log2(depth + 1));

    // 检查是否包含子循环及其内存特性
    unsigned totalSubLoops = 0;
    unsigned memIntensiveSubLoops = 0;

    SmallVector<Loop *, 8> WorkList(L->begin(), L->end());
    while (!WorkList.empty())
    {
        Loop *SubL = WorkList.pop_back_val();
        totalSubLoops++;

        // 添加子循环的子循环到工作列表
        WorkList.append(SubL->begin(), SubL->end());

        if (isMemoryIntensiveLoop(SubL))
        {
            memIntensiveSubLoops++;

            // 分析子循环的内存访问模式
            unsigned memOps = 0;
            for (auto *BB : SubL->getBlocks())
            {
                for (auto &I : *BB)
                {
                    if (isa<LoadInst>(I) || isa<StoreInst>(I))
                    {
                        memOps++;
                    }
                }
            }

            // 内存操作很多的子循环更重要
            if (memOps > 10)
            {
                score *= (1.0 + 0.1 * std::log2(memOps));
            }
        }
    }

    // 子循环数量影响分数
    if (totalSubLoops > 0)
    {
        score *= (1.0 + 0.1 * std::log2(totalSubLoops + 1));
    }

    // 内存密集型子循环占比影响分数
    if (memIntensiveSubLoops > 0 && totalSubLoops > 0)
    {
        double intensiveRatio = (double)memIntensiveSubLoops / totalSubLoops;
        score *= (1.0 + 0.3 * intensiveRatio);
    }

    // 考虑循环大小
    unsigned loopSize = 0;
    for (auto *BB : L->getBlocks())
    {
        loopSize += BB->size();
    }

    // 很大的循环可能有更复杂的访问模式
    if (loopSize > 50)
    {
        score *= (1.0 + 0.2 * std::log2(loopSize / 50.0));
    }

    return score;
}

// 在 LoopAnalyzer.cpp 内部定义一个辅助函数
static bool isSameBasePtr(Value *Ptr1, Value *Ptr2, ScalarEvolution &SE)
{
    // errs() << "===== Function:isSameBasePtr =====\n";
    if (Ptr1 == Ptr2)
        return true;

    // 如果两个指针都是GEP指令，比较它们的基地址
    auto *GEP1 = dyn_cast<GetElementPtrInst>(Ptr1);
    auto *GEP2 = dyn_cast<GetElementPtrInst>(Ptr2);

    if (GEP1 && GEP2)
    {
        return GEP1->getPointerOperand() == GEP2->getPointerOperand();
    }

    // 如果只有一个是GEP指令，检查另一个是否是其基地址
    if (GEP1)
    {
        return GEP1->getPointerOperand() == Ptr2;
    }
    if (GEP2)
    {
        return GEP2->getPointerOperand() == Ptr1;
    }

    // 使用 ScalarEvolution 比较指针的基地址
    if (SE.isSCEVable(Ptr1->getType()) && SE.isSCEVable(Ptr2->getType()))
    {
        const SCEV *SCEV1 = SE.getSCEV(Ptr1);
        const SCEV *SCEV2 = SE.getSCEV(Ptr2);

        // 尝试获取基址部分
        if (auto *Addr1 = dyn_cast<SCEVAddRecExpr>(SCEV1))
        {
            if (auto *Addr2 = dyn_cast<SCEVAddRecExpr>(SCEV2))
            {
                return Addr1->getStart() == Addr2->getStart();
            }
        }
    }

    return false;
}

// 分析数据访问的局部性
LocalityType LoopAnalyzer::analyzeDataLocality(Value *Ptr, Loop *L)
{
    // errs() << "===== Function:analyzeDateLocality =====\n";
    if (!Ptr || !L || !SE.isSCEVable(Ptr->getType()))
        return LocalityType::MODERATE; // 默认中等局部性

    // 收集对该指针的所有访问
    SmallVector<Instruction *, 16> PtrAccesses;
    for (auto *BB : L->getBlocks())
    {
        for (auto &I : *BB)
        {
            // 加载指令
            if (auto *LI = dyn_cast<LoadInst>(&I))
            {
                if (isSameBasePtr(LI->getPointerOperand(), Ptr, SE))
                {
                    PtrAccesses.push_back(&I);
                }
            }
            // 存储指令
            else if (auto *SI = dyn_cast<StoreInst>(&I))
            {
                if (isSameBasePtr(SI->getPointerOperand(), Ptr, SE))
                {
                    PtrAccesses.push_back(&I);
                }
            }
        }
    }

    if (PtrAccesses.empty())
        return LocalityType::MODERATE;

    // 空间和时间局部性评分
    int spatialLocalityScore = 0;
    int temporalLocalityScore = 0;

    // 1. 检查地址表达式
    for (Instruction *I : PtrAccesses)
    {
        Value *AccessPtr = nullptr;
        if (auto *LI = dyn_cast<LoadInst>(I))
            AccessPtr = LI->getPointerOperand();
        else if (auto *SI = dyn_cast<StoreInst>(I))
            AccessPtr = SI->getPointerOperand();
        else
            continue;

        if (!SE.isSCEVable(AccessPtr->getType()))
            continue;

        const SCEV *PtrSCEV = SE.getSCEV(AccessPtr);

        // 检查是否为仿射表达式，这通常意味着良好的空间局部性
        if (auto *AR = dyn_cast<SCEVAddRecExpr>(PtrSCEV))
        {
            if (AR->isAffine())
            {
                // 检查步长
                const SCEV *Step = AR->getStepRecurrence(SE);
                if (auto *StepC = dyn_cast<SCEVConstant>(Step))
                {
                    int64_t StepVal = StepC->getValue()->getSExtValue();

                    // 步长越小，空间局部性越好
                    if (std::abs(StepVal) <= 8) // 典型的字节对齐
                    {
                        // 步长为1是最佳的连续访问
                        if (std::abs(StepVal) == 1)
                            spatialLocalityScore += 3;
                        else if (std::abs(StepVal) <= 4)
                            spatialLocalityScore += 2;
                        else
                            spatialLocalityScore += 1;
                    }
                    // 大步长意味着跨步访问，空间局部性差
                    else if (std::abs(StepVal) > 64)
                    {
                        spatialLocalityScore -= 2;
                    }
                    else
                    {
                        spatialLocalityScore -= 1;
                    }
                }
            }
        }

        // 循环不变的访问可能意味着良好的时间局部性
        if (SE.isLoopInvariant(PtrSCEV, L))
        {
            temporalLocalityScore += 2;
        }
    }

    // 2. 检查访问之间的间隔 - 同一数据被多次访问
    BasicBlock *Header = L->getHeader();
    if (Header && !PtrAccesses.empty())
    {
        // 如果同一位置在循环中被多次访问，可能有良好的时间局部性
        SmallPtrSet<Value *, 8> UniqueAddresses;
        for (Instruction *I : PtrAccesses)
        {
            Value *Addr = nullptr;
            if (auto *LI = dyn_cast<LoadInst>(I))
                Addr = LI->getPointerOperand();
            else if (auto *SI = dyn_cast<StoreInst>(I))
                Addr = SI->getPointerOperand();

            if (Addr)
                UniqueAddresses.insert(Addr);
        }

        // 如果访问次数远大于唯一地址数，可能有良好的时间局部性
        if (PtrAccesses.size() > 2 * UniqueAddresses.size())
        {
            temporalLocalityScore += 2;
        }
    }

    // 3. 嵌套循环中的局部性
    unsigned loopNestDepth = 0;
    Loop *CurLoop = L;
    while (CurLoop)
    {
        loopNestDepth++;
        CurLoop = CurLoop->getParentLoop();
    }

    // 深度嵌套的循环可能有更好的时间局部性
    if (loopNestDepth >= 2)
    {
        temporalLocalityScore += loopNestDepth - 1;
    }

    // 4. 多维数组访问模式分析
    for (Instruction *I : PtrAccesses)
    {
        Value *AccessPtr = nullptr;
        if (auto *LI = dyn_cast<LoadInst>(I))
            AccessPtr = LI->getPointerOperand();
        else if (auto *SI = dyn_cast<StoreInst>(I))
            AccessPtr = SI->getPointerOperand();
        else
            continue;

        if (auto *GEP = dyn_cast<GetElementPtrInst>(AccessPtr))
        {
            if (GEP->getNumIndices() > 1) // 可能是多维数组
            {
                // 检查索引变化模式
                SmallVector<int, 4> IndexVarianceInLoop(GEP->getNumIndices(), 0);

                for (unsigned i = 1; i < GEP->getNumIndices() + 1; ++i)
                {
                    Value *IdxOp = GEP->getOperand(i);
                    if (!SE.isSCEVable(IdxOp->getType()))
                        continue;

                    const SCEV *IdxSCEV = SE.getSCEV(IdxOp);

                    // 检查哪个索引在循环中变化最快
                    if (!SE.isLoopInvariant(IdxSCEV, L))
                    {
                        if (auto *AR = dyn_cast<SCEVAddRecExpr>(IdxSCEV))
                        {
                            if (AR->isAffine() && AR->getLoop() == L)
                            {
                                IndexVarianceInLoop[i - 1] = 1;

                                // 检查变化频率
                                const SCEV *Step = AR->getStepRecurrence(SE);
                                if (auto *SC = dyn_cast<SCEVConstant>(Step))
                                {
                                    // 步长越小意味着变化越频繁
                                    int64_t StepVal = SC->getValue()->getSExtValue();
                                    if (std::abs(StepVal) == 1)
                                    {
                                        IndexVarianceInLoop[i - 1] = 3; // 最频繁变化
                                    }
                                    else if (std::abs(StepVal) <= 4)
                                    {
                                        IndexVarianceInLoop[i - 1] = 2; // 中等频率变化
                                    }
                                }
                            }
                        }
                    }
                }

                // 检查是否是行优先访问（最后一个索引变化最快）
                if (!IndexVarianceInLoop.empty() && IndexVarianceInLoop.size() > 1)
                {
                    if (IndexVarianceInLoop.back() > IndexVarianceInLoop.front())
                    {
                        // 行优先访问通常有良好的空间局部性
                        spatialLocalityScore += 2;
                    }
                    else if (IndexVarianceInLoop.front() > IndexVarianceInLoop.back())
                    {
                        // 列优先访问通常空间局部性较差
                        spatialLocalityScore -= 2;
                    }
                }
            }
        }
    }

    // 5. 数组访问模式
    bool hasStrideAccess = false;
    bool hasScatterAccess = false;

    for (Instruction *I : PtrAccesses)
    {
        Value *AccessPtr = nullptr;
        if (auto *LI = dyn_cast<LoadInst>(I))
            AccessPtr = LI->getPointerOperand();
        else if (auto *SI = dyn_cast<StoreInst>(I))
            AccessPtr = SI->getPointerOperand();
        else
            continue;

        if (!SE.isSCEVable(AccessPtr->getType()))
            continue;

        const SCEV *PtrSCEV = SE.getSCEV(AccessPtr);

        // 识别跨步访问
        if (auto *AR = dyn_cast<SCEVAddRecExpr>(PtrSCEV))
        {
            if (AR->isAffine())
            {
                const SCEV *Step = AR->getStepRecurrence(SE);
                if (auto *StepC = dyn_cast<SCEVConstant>(Step))
                {
                    int64_t StepVal = StepC->getValue()->getSExtValue();
                    if (std::abs(StepVal) > 1)
                    {
                        hasStrideAccess = true;
                        // 大步长降低空间局部性
                        if (std::abs(StepVal) > 64)
                        {
                            spatialLocalityScore -= 3;
                        }
                    }
                }
            }
            else
            {
                // 非仿射表达式通常意味着不规则访问
                hasScatterAccess = true;
                spatialLocalityScore -= 3;
            }
        }
    }

    // 综合评分确定局部性类型
    if (spatialLocalityScore >= 3 && temporalLocalityScore >= 3)
    {
        return LocalityType::EXCELLENT;
    }
    else if (spatialLocalityScore >= 2 || temporalLocalityScore >= 3)
    {
        return LocalityType::GOOD;
    }
    else if (spatialLocalityScore >= 0 || temporalLocalityScore >= 1)
    {
        return LocalityType::MODERATE;
    }
    else
    {
        return LocalityType::POOR;
    }
}
// 分析循环中的交错访问模式
InterleavedAccessInfo LoopAnalyzer::analyzeInterleavedAccess(Loop *L)
{
    // errs() << "===== Function:analyzeInterleavedAccess =====\n";
    InterleavedAccessInfo Result;
    Result.isInterleaved = false;
    Result.accessedArrays = 0;
    Result.strideRatio = 0.0;
    Result.isCoalesced = false;

    if (!L)
        return Result;

    // 收集循环中所有的内存访问
    struct MemAccessInfo
    {
        Value *Ptr;
        const SCEV *PtrSCEV;
        Value *BasePtr;
        const SCEV *StrideSCEV;
        int64_t Stride;
        bool IsLoad;
    };

    SmallVector<MemAccessInfo, 16> MemAccesses;
    SmallPtrSet<Value *, 16> BasePtrs;

    // 收集所有内存访问
    for (auto *BB : L->getBlocks())
    {
        for (auto &I : *BB)
        {
            if (auto *LI = dyn_cast<LoadInst>(&I))
            {
                Value *Ptr = LI->getPointerOperand();
                if (SE.isSCEVable(Ptr->getType()))
                {
                    const SCEV *PtrSCEV = SE.getSCEV(Ptr);
                    if (auto *AR = dyn_cast<SCEVAddRecExpr>(PtrSCEV))
                    {
                        if (AR->isAffine() && AR->getLoop() == L)
                        {
                            MemAccessInfo MAI;
                            MAI.Ptr = Ptr;
                            MAI.PtrSCEV = PtrSCEV;
                            MAI.IsLoad = true;

                            // 获取基址
                            if (auto *GEP = dyn_cast<GetElementPtrInst>(Ptr))
                            {
                                MAI.BasePtr = GEP->getPointerOperand();
                                BasePtrs.insert(MAI.BasePtr);
                            }
                            else
                            {
                                MAI.BasePtr = Ptr;
                                BasePtrs.insert(Ptr);
                            }

                            // 获取步长
                            MAI.StrideSCEV = AR->getStepRecurrence(SE);
                            if (auto *SC = dyn_cast<SCEVConstant>(MAI.StrideSCEV))
                            {
                                MAI.Stride = SC->getValue()->getSExtValue();
                                MemAccesses.push_back(MAI);
                            }
                        }
                    }
                }
            }
            else if (auto *SI = dyn_cast<StoreInst>(&I))
            {
                Value *Ptr = SI->getPointerOperand();
                if (SE.isSCEVable(Ptr->getType()))
                {
                    const SCEV *PtrSCEV = SE.getSCEV(Ptr);
                    if (auto *AR = dyn_cast<SCEVAddRecExpr>(PtrSCEV))
                    {
                        if (AR->isAffine() && AR->getLoop() == L)
                        {
                            MemAccessInfo MAI;
                            MAI.Ptr = Ptr;
                            MAI.PtrSCEV = PtrSCEV;
                            MAI.IsLoad = false;

                            // 获取基址
                            if (auto *GEP = dyn_cast<GetElementPtrInst>(Ptr))
                            {
                                MAI.BasePtr = GEP->getPointerOperand();
                                BasePtrs.insert(MAI.BasePtr);
                            }
                            else
                            {
                                MAI.BasePtr = Ptr;
                                BasePtrs.insert(Ptr);
                            }

                            // 获取步长
                            MAI.StrideSCEV = AR->getStepRecurrence(SE);
                            if (auto *SC = dyn_cast<SCEVConstant>(MAI.StrideSCEV))
                            {
                                MAI.Stride = SC->getValue()->getSExtValue();
                                MemAccesses.push_back(MAI);
                            }
                        }
                    }
                }
            }
        }
    }

    // 分析内存访问模式
    Result.accessedArrays = BasePtrs.size();

    // 如果访问少于两个数组，不太可能有交错访问
    if (BasePtrs.size() < 2 || MemAccesses.size() < 2)
    {
        return Result;
    }

    // 按照基址分组分析
    using AccessGroup = SmallVector<MemAccessInfo, 4>;
    SmallDenseMap<Value *, AccessGroup> BaseToAccesses;

    for (const auto &MAI : MemAccesses)
    {
        BaseToAccesses[MAI.BasePtr].push_back(MAI);
    }

    // 检查是否有多个数组以固定步长访问
    SmallVector<int64_t, 8> UniqueStrides;
    for (const auto &Entry : BaseToAccesses)
    {
        const AccessGroup &Group = Entry.second;
        if (Group.size() >= 2)
        {
            // 检查组内访问是否有相同的步长
            bool HasConsistentStride = true;
            int64_t GroupStride = Group[0].Stride;

            for (size_t i = 1; i < Group.size(); ++i)
            {
                if (Group[i].Stride != GroupStride)
                {
                    HasConsistentStride = false;
                    break;
                }
            }

            if (HasConsistentStride)
            {
                // 记录这个组的步长
                UniqueStrides.push_back(GroupStride);
            }
        }
        else if (Group.size() == 1)
        {
            // 单一访问，记录步长
            UniqueStrides.push_back(Group[0].Stride);
        }
    }

    // 如果有多个不同的步长，检查它们的关系
    if (UniqueStrides.size() >= 2)
    {
        Result.isInterleaved = true;

        // 计算步长比例
        std::sort(UniqueStrides.begin(), UniqueStrides.end(),
                  [](int64_t a, int64_t b)
                  { return std::abs(a) < std::abs(b); });

        // 使用最小和最大步长计算比例
        if (UniqueStrides[0] != 0)
        {
            Result.strideRatio = static_cast<double>(std::abs(UniqueStrides.back())) /
                                 std::abs(UniqueStrides[0]);
        }

        // 检查是否是合并访问模式 - 通常是连续步长
        if (std::abs(UniqueStrides[0]) == 1)
        {
            Result.isCoalesced = true;

            // 检查是否有分歧步长，这会导致非合并访问
            for (size_t i = 1; i < UniqueStrides.size(); ++i)
            {
                if (std::abs(UniqueStrides[i]) > 4) // 较大的步长通常不利于合并
                {
                    Result.isCoalesced = false;
                    break;
                }
            }
        }

        // 检查是否有交错的读写模式
        bool hasInterleavedReadWrite = false;
        for (const auto &Entry : BaseToAccesses)
        {
            bool hasLoad = false;
            bool hasStore = false;

            for (const auto &Access : Entry.second)
            {
                if (Access.IsLoad)
                    hasLoad = true;
                else
                    hasStore = true;

                if (hasLoad && hasStore)
                {
                    hasInterleavedReadWrite = true;
                    break;
                }
            }

            if (hasInterleavedReadWrite)
                break;
        }

        if (hasInterleavedReadWrite)
        {
            Result.hasInterleavedReadWrite = true;
        }
    }

    return Result;
}