#include "ContentionAnalyzer.h"
#include "ParallelismAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include <queue>
#include <unordered_set>

using namespace llvm;
using namespace MyHBM;

// 分析竞争
ContentionInfo ContentionAnalyzer::analyzeContention(Value *AllocPtr, Function &F)
{
    ContentionInfo Result;
    if (!AllocPtr)
        return Result;

    // 创建并行分析器
    ParallelismAnalyzer ParAn;

    // 检查是否为并行函数
    bool isParallelFunction = ParAn.detectParallelRuntime(F);
    if (!isParallelFunction)
    {
        // 非并行函数不存在竞争
        return Result;
    }

    // 收集所有对该指针的使用
    std::vector<Instruction *> UseInsts;
    std::queue<Value *> WorkList;
    std::set<Value *> Visited;
    WorkList.push(AllocPtr);

    while (!WorkList.empty())
    {
        Value *V = WorkList.front();
        WorkList.pop();

        if (!Visited.insert(V).second)
            continue;

        for (User *U : V->users())
        {
            if (auto *I = dyn_cast<Instruction>(U))
            {
                if (I->getFunction() == &F)
                {
                    UseInsts.push_back(I);

                    // 继续跟踪衍生值
                    if (isa<GetElementPtrInst>(I) || isa<BitCastInst>(I) ||
                        isa<LoadInst>(I) || isa<PHINode>(I) || isa<SelectInst>(I))
                    {
                        WorkList.push(I);
                    }
                }
            }
        }
    }

    // 没有使用，无竞争
    if (UseInsts.empty())
    {
        return Result;
    }

    // 估计线程数
    unsigned threadCount = ParAn.estimateParallelThreads(F);

    // 1. 检查伪共享
    bool hasFalseSharing = false;
    const DataLayout &DL = F.getParent()->getDataLayout();

    // 找出所有GEP指令
    for (Instruction *I : UseInsts)
    {
        if (auto *GEP = dyn_cast<GetElementPtrInst>(I))
        {
            // 检查元素大小
            Type *ElemTy = GEP->getResultElementType();
            unsigned elemSize = DL.getTypeAllocSize(ElemTy);

            if (detectFalseSharing(GEP, elemSize, threadCount))
            {
                hasFalseSharing = true;
                Result.potentialContentionPoints++;
            }
        }
    }

    // 2. 检查原子操作竞争
    bool hasAtomicContention = false;
    for (Instruction *I : UseInsts)
    {
        if (isa<AtomicRMWInst>(I) || isa<AtomicCmpXchgInst>(I) ||
            (isa<LoadInst>(I) && cast<LoadInst>(I)->isAtomic()) ||
            (isa<StoreInst>(I) && cast<StoreInst>(I)->isAtomic()))
        {
            hasAtomicContention = true;
            Result.potentialContentionPoints++;
        }
    }

    // 3. 检查锁竞争
    bool hasLockContention = false;
    for (Instruction *I : UseInsts)
    {
        if (auto *Call = dyn_cast<CallInst>(I))
        {
            Function *Callee = Call->getCalledFunction();
            if (Callee)
            {
                StringRef Name = Callee->getName();
                if (Name.contains("lock") || Name.contains("mutex") ||
                    Name.contains("critical") || Name.contains("barrier"))
                {
                    hasLockContention = true;
                    Result.potentialContentionPoints++;
                }
            }
        }
    }

    // 4. 检查带宽竞争
    bool hasBandwidthContention = false;
    for (auto &L : LI)
    {
        if (detectBandwidthContention(AllocPtr, L, threadCount))
        {
            hasBandwidthContention = true;
            Result.potentialContentionPoints++;
        }
    }

    // 确定竞争类型和概率
    if (hasBandwidthContention)
    {
        Result.type = ContentionInfo::ContentionType::BANDWIDTH_CONTENTION;
        Result.contentionProbability = 0.8; // 带宽竞争概率很高
    }
    else if (hasLockContention)
    {
        Result.type = ContentionInfo::ContentionType::LOCK_CONTENTION;
        Result.contentionProbability = 0.6; // 锁竞争概率中等
    }
    else if (hasAtomicContention)
    {
        Result.type = ContentionInfo::ContentionType::ATOMIC_CONTENTION;
        Result.contentionProbability = 0.7; // 原子操作竞争概率较高
    }
    else if (hasFalseSharing)
    {
        Result.type = ContentionInfo::ContentionType::FALSE_SHARING;
        Result.contentionProbability = 0.5; // 伪共享概率一般
    }
    else
    {
        Result.type = ContentionInfo::ContentionType::NONE;
        Result.contentionProbability = 0.0;
    }

    // 计算竞争分数
    // 带宽竞争对HBM需求更高，其他竞争则降低HBM的效益
    if (Result.type == ContentionInfo::ContentionType::BANDWIDTH_CONTENTION)
    {
        // 带宽竞争是HBM的主要目标
        Result.contentionScore = 25.0 * Result.contentionProbability;
    }
    else if (Result.type == ContentionInfo::ContentionType::FALSE_SHARING)
    {
        // 伪共享对HBM带宽的利用不太好
        Result.contentionScore = -10.0 * Result.contentionProbability;
    }
    else if (Result.type == ContentionInfo::ContentionType::ATOMIC_CONTENTION)
    {
        // 原子操作竞争会降低并行效率
        Result.contentionScore = -15.0 * Result.contentionProbability;
    }
    else if (Result.type == ContentionInfo::ContentionType::LOCK_CONTENTION)
    {
        // 锁竞争会严重降低并行效率
        Result.contentionScore = -20.0 * Result.contentionProbability;
    }

    // 调整基于竞争点数量的分数
    if (Result.potentialContentionPoints > 1)
    {
        // 多个竞争点会放大效应
        Result.contentionScore *= (1.0 + 0.1 * std::min(10u, Result.potentialContentionPoints));
    }

    return Result;
}

// 检测伪共享
bool ContentionAnalyzer::detectFalseSharing(Value *Ptr, unsigned elemSize, unsigned threadCount)
{
    if (!Ptr || elemSize == 0 || threadCount <= 1)
        return false;

    // 如果元素大小小于缓存行大小的一部分，可能存在伪共享
    const unsigned CacheLineSize = 64; // 典型的缓存行大小

    if (elemSize < CacheLineSize / 4)
    {
        // 创建并行分析器
        ParallelismAnalyzer ParAn;

        // 检查索引是否与线程ID相关
        if (auto *GEP = dyn_cast<GetElementPtrInst>(Ptr))
        {
            for (auto I = GEP->idx_begin(), E = GEP->idx_end(); I != E; ++I)
            {
                Value *Idx = *I;

                // 检查索引是否依赖线程ID
                if (ParAn.isThreadIDRelated(Idx))
                {
                    // 如果索引与线程ID相关，且元素很小，可能导致伪共享
                    return true;
                }
            }
        }
    }

    return false;
}

// 检测带宽竞争
bool ContentionAnalyzer::detectBandwidthContention(Value *Ptr, Loop *L, unsigned threadCount)
{
    if (!Ptr || !L || threadCount <= 1)
        return false;

    // 1. 检查循环是否是并行循环
    ParallelismAnalyzer ParAn;
    Instruction *Term = L->getHeader()->getTerminator();
    bool isParallelLoop = Term && Term->getMetadata("llvm.loop.parallel_accesses");

    if (!isParallelLoop)
    {
        // 检查是否有其他并行提示
        for (BasicBlock *BB : L->getBlocks())
        {
            for (Instruction &I : *BB)
            {
                if (auto *Call = dyn_cast<CallInst>(&I))
                {
                    Function *Callee = Call->getCalledFunction();
                    if (Callee && (Callee->getName().contains("parallel") ||
                                   Callee->getName().contains("omp") ||
                                   Callee->getName().contains("thread")))
                    {
                        isParallelLoop = true;
                        break;
                    }
                }

                if (isParallelLoop)
                    break;
            }
        }
    }

    if (!isParallelLoop)
        return false;

    // 2. 检查是否有频繁的内存访问
    unsigned memOpCount = 0;
    bool usesPtr = false;

    for (BasicBlock *BB : L->getBlocks())
    {
        for (Instruction &I : *BB)
        {
            if (auto *LI = dyn_cast<LoadInst>(&I))
            {
                memOpCount++;
                if (ParAn.isPtrDerivedFrom(LI->getPointerOperand(), Ptr))
                {
                    usesPtr = true;
                }
            }
            else if (auto *SI = dyn_cast<StoreInst>(&I))
            {
                memOpCount++;
                if (ParAn.isPtrDerivedFrom(SI->getPointerOperand(), Ptr))
                {
                    usesPtr = true;
                }
            }
        }
    }

    // 如果循环中没有使用该指针，则不存在带宽竞争
    if (!usesPtr)
        return false;

    // 3. 估计循环迭代次数
    unsigned tripCount = 100; // 默认估计

    // 4. 估计带宽使用
    // 假设每个存储器操作平均访问 8 字节
    uint64_t bytesPerIteration = memOpCount * 8;
    uint64_t totalBytes = bytesPerIteration * tripCount;

    // 5. 考虑线程数的影响
    // 多线程会放大带宽需求
    uint64_t estimatedBandwidth = totalBytes * threadCount;

    // 如果预计带宽使用超过阈值，认为存在带宽竞争
    const uint64_t BandwidthThreshold = 1ULL * 1024 * 1024 * 1024; // 1 GB

    return estimatedBandwidth > BandwidthThreshold;
}
