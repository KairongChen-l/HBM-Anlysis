#include "BandwidthAnalyzer.h"
#include "ParallelismAnalyzer.h"
#include "StrideAnalyzer.h"
#include "VectorizationAnalyzer.h"
#include "LoopUtils.h"
#include "PointerUtils.h"
#include "Options.h"
#include "LoopAnalyzer.h"
// 新添加的头文件
#include "BankConflictAnalyzer.h"
#include "WeightConfig.h"

#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Constants.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Support/raw_ostream.h"

#include <queue>
#include <cmath>

using namespace llvm;
using namespace MyHBM;

// 计算访问指令的带宽得分
double BandwidthAnalyzer::computeAccessScore(
    Instruction *I,
    bool isWrite,
    MallocRecord &MR)
{
    // errs() << "===== Function:computeAccessScore =====\n";
    using namespace Options;
    using namespace WeightConfig;

    double base = isWrite ? AccessBaseWrite : AccessBaseRead;
    BasicBlock *BB = I->getParent();
    Loop *L = LI.getLoopFor(BB);
    int depth = 0;
    uint64_t tripCount = 1;

    // 访问的指针操作数
    Value *PtrOperand = nullptr;
    if (auto *LD = dyn_cast<LoadInst>(I))
        PtrOperand = LD->getPointerOperand();
    else if (auto *ST = dyn_cast<StoreInst>(I))
        PtrOperand = ST->getPointerOperand();

    if (L)
    {
        depth = L->getLoopDepth();
        tripCount = LoopUtils::getLoopTripCount(L, SE);
        if (tripCount == 0 || tripCount == (uint64_t)-1)
            tripCount = 1;
        MR.LoopDepth = depth;
        MR.TripCount = tripCount;

        // 创建所需的分析器
        LoopAnalyzer LoopAn(SE, LI);
        StrideAnalyzer StrideAn(SE);
        VectorizationAnalyzer VecAn(SE);
        ParallelismAnalyzer ParAn;

        // ===== 嵌套循环分析 =====
        if (PtrOperand)
        {
            double nestedLoopScore = LoopAn.analyzeNestedLoops(L, PtrOperand);
            MR.StreamScore += nestedLoopScore * NestedLoopWeight;
            base += nestedLoopScore * NestedLoopWeight;
        }

        // ===== 数据局部性分析 =====
        if (PtrOperand)
        {
            LocalityType locality = LoopAn.analyzeDataLocality(PtrOperand, L);
            switch (locality)
            {
            case LocalityType::EXCELLENT:
                // 极佳的局部性可能不太需要HBM
                base += StreamBonus * LocalityExcellentBonus;
                break;
            case LocalityType::GOOD:
                // 良好的局部性，但仍可从HBM受益
                base += StreamBonus * LocalityGoodBonus;
                break;
            case LocalityType::MODERATE:
                // 中等局部性，更可能从HBM受益
                base += StreamBonus * LocalityModerateBonus;
                break;
            case LocalityType::POOR:
                // 差的局部性，非常需要HBM
                base += StreamBonus * LocalityPoorBonus;
                MR.IsStreamAccess = true; // 标记为流式访问
                break;
            }
        }

        // ===== 交错访问模式分析 =====
        InterleavedAccessInfo interleavedInfo = LoopAn.analyzeInterleavedAccess(L);
        if (interleavedInfo.isInterleaved)
        {
            if (interleavedInfo.isPotentiallyBandwidthBound)
            {
                // 交错访问多个数组，可能是带宽密集型
                MR.IsStreamAccess = true;
                double interleaveBonus = StreamBonus * 0.8 * (0.6 + 0.15 * interleavedInfo.accessedArrays);
                MR.StreamScore += interleaveBonus;
                base += interleaveBonus;
            }
        }

        // ===== 并行访问分析 =====
        if (MR.IsParallel && PtrOperand)
        {
            // 分析线程访问模式
            ThreadAccessPattern AccessPattern = ParAn.analyzeThreadAccess(PtrOperand, I);

            switch (AccessPattern)
            {
            case ThreadAccessPattern::PARTITIONED:
            {
                // 按线程ID分区访问（良性）
                MR.IsThreadPartitioned = true;
                MR.MayConflict = false;

                // 估计并行线程数并根据线程数增加权重
                unsigned NumThreads = ParAn.estimateParallelThreads(*I->getFunction());
                double threadFactor = std::min(8.0, std::log2(double(NumThreads)));
                double parallelBonus = ParallelBonus * (1.0 + 0.2 * threadFactor);

                MR.ParallelScore += parallelBonus;
                base += parallelBonus;
                break;
            }
            case ThreadAccessPattern::SHARED_READONLY:
            {
                // 只读共享（良性，但带宽需求取决于线程数）
                MR.IsThreadPartitioned = false;
                MR.MayConflict = false;

                // 只读共享在并行环境中也会增加带宽需求
                MR.ParallelScore += ParallelBonus * SharedReadOnlyWeight;
                base += ParallelBonus * SharedReadOnlyWeight;
                break;
            }

            case ThreadAccessPattern::ATOMIC_ACCESS:
            {
                // 原子访问（有同步开销，但不一定是带宽瓶颈）
                MR.IsThreadPartitioned = false;
                MR.MayConflict = true;

                // 原子操作通常不是带宽密集型的
                MR.ParallelScore += ParallelBonus * AtomicAccessWeight;
                base += ParallelBonus * AtomicAccessWeight;
                break;
            }
            case ThreadAccessPattern::FALSE_SHARING:
            {
                // 伪共享（不良）
                MR.IsThreadPartitioned = false;
                MR.MayConflict = true;

                // 伪共享会导致性能问题，但通常不是带宽瓶颈
                MR.ConflictPenalty += ParallelBonus * FalseSharingPenalty;
                base -= ParallelBonus * FalseSharingPenalty;
                break;
            }
            case ThreadAccessPattern::SHARED_WRITE:
            {
                // 写入共享（可能有冲突）
                MR.IsThreadPartitioned = false;
                MR.MayConflict = true;

                // 共享写入可能导致缓存一致性流量
                MR.ConflictPenalty += ParallelBonus * SharedWritePenalty;
                base -= ParallelBonus * SharedWritePenalty;
                break;
            }
            case ThreadAccessPattern::PRIVATE:
            {
                // 线程私有数据
                MR.IsThreadPartitioned = true;
                MR.MayConflict = false;

                // 私有数据在并行环境中通常不是带宽瓶颈
                MR.ParallelScore += ParallelBonus * 0.5;
                base += ParallelBonus * 0.5;
                break;
            }
            default:
                // 未知模式，保守处理
                MR.MayConflict = true;
                break;
            }

            // 检查是否有并行循环元数据
            if (ParAn.hasParallelLoopMetadata(L))
            {
                // 编译器明确标记的并行循环通常是良性的
                MR.ParallelScore += ParallelBonus * 0.5;
                base += ParallelBonus * 0.5;
            }

            // 分析并行框架类型
            if (ParAn.isOpenMPParallel(*I->getFunction()))
            {
                // OpenMP通常有良好的数据局部性
                MR.ParallelScore += ParallelBonus * OpenMPWeight;
                base += ParallelBonus * OpenMPWeight;
            }
            else if (ParAn.isCUDAParallel(*I->getFunction()))
            {
                // CUDA通常有大量并行线程
                MR.ParallelScore += ParallelBonus * CUDAWeight;
                base += ParallelBonus * CUDAWeight;
            }
            else if (ParAn.isTBBParallel(*I->getFunction()))
            {
                // TBB通常有任务窃取调度
                MR.ParallelScore += ParallelBonus * TBBWeight;
                base += ParallelBonus * TBBWeight;
            }
        }

        // ===== 向量化分析 =====
        if (VecAn.isVectorizedInstruction(I))
        {
            MR.IsVectorized = true;
            // 获取向量宽度并根据大小给予额外奖励
            int VecWidth = 0;
            if (auto *LD = dyn_cast<LoadInst>(I))
                VecWidth = VecAn.getVectorWidth(LD->getType());
            else if (auto *ST = dyn_cast<StoreInst>(I))
                VecWidth = VecAn.getVectorWidth(ST->getValueOperand()->getType());

            double vectorBonus = VectorBonus;
            if (VecWidth >= 8)
                vectorBonus *= VectorWidth8Plus; // 512位向量（AVX-512）
            else if (VecWidth >= 4)
                vectorBonus *= VectorWidth4Plus; // 256位向量（AVX）

            MR.VectorScore += vectorBonus;
            base += vectorBonus;
        }

        // 检查循环是否显示出向量化模式
        if (VecAn.isVectorLoopPattern(L))
        {
            MR.IsVectorized = true;
            MR.VectorScore += VectorBonus * VectorLoopPatternBonus;
            base += VectorBonus * VectorLoopPatternBonus;
        }

        // 检查指针操作数是否参与向量操作
        if (PtrOperand)
        {
            std::set<Value *> Visited;
            if (VecAn.hasVectorOperations(PtrOperand, Visited))
            {
                MR.IsVectorized = true;
                MR.VectorScore += VectorBonus;
                base += VectorBonus;
            }
        }
        // temporal locality analysis 时间局部性分析
        if (PtrOperand)
        {
            Function *F = I->getFunction();
            double temporalScore = computeTemporalLocalityScore(PtrOperand, F);

            // Store temporal analysis results in MallocRecord for later use
            MR.TemporalLocalityData.temporalLocalityScore = temporalScore;

            // Adjust base score based on temporal locality
            base += temporalScore;

            // Debug output
            // errs() << "  Adding temporal locality score: " << temporalScore << "\n";
        }

        // ===== Add bank conflict analysis =====
        if (PtrOperand)
        {
            // Analyze potential bank conflicts and adjust score accordingly
            double bankConflictScore = analyzeBankConflicts(PtrOperand, MR);

            // Adjust base score based on bank conflict analysis
            base += bankConflictScore;

            // Debug output
            // errs() << "  Adding bank conflict score adjustment: " << bankConflictScore << "\n";
        }
        // ===== Add dependency chain analysis =====
        if (PtrOperand)
        {
            // Analyze dependency chains and adjust score accordingly
            double depChainScore = analyzeDependencyChains(PtrOperand, MR);

            // Adjust base score based on dependency chain analysis
            base += depChainScore;

            // Debug output
            // errs() << "  Adding dependency chain score adjustment: " << depChainScore << "\n";
        }
        // 检查函数是否包含SIMD内部函数
        Function *F = I->getFunction();
        if (F && VecAn.detectSIMDIntrinsics(*F))
        {
            MR.IsVectorized = true;
            MR.VectorScore += VectorBonus * 0.8;
            base += VectorBonus * 0.8;
        }

        // ===== MemorySSA 结构复杂度分析 =====
        MR.SSAPenalty = computeMemorySSAStructureScore(I);
        base -= MR.SSAPenalty;

        // ===== 混乱度评分 =====
        MR.ChaosPenalty = computeAccessChaosScore(PtrOperand);
        base -= MR.ChaosPenalty;
        
        // ===== 流式访问分析 =====
        if (PtrOperand)
        {
            // 使用流式访问分析方法
            if (StrideAn.isStreamingAccess(PtrOperand, L, AA))
            {
                MR.IsStreamAccess = true;
                double streamBonus = StreamBonus;

                // 如果是GEP，进一步分析步长类型
                if (auto *GEP = dyn_cast<GetElementPtrInst>(PtrOperand))
                {
                    StrideType stride = StrideAn.analyzeGEPStride(GEP);

                    switch (stride)
                    {
                    case StrideType::CONSTANT:
                        streamBonus *= StrideConstantBonus * 1.2; // 常量步长，最优
                        break;
                    case StrideType::LINEAR:
                        streamBonus *= StrideLinearBonus * 1.1; // 线性步长，很好
                        break;
                    case StrideType::COMPLEX:
                        streamBonus *= StrideComplexBonus; // 复杂但有规律，还可以
                        break;
                    case StrideType::IRREGULAR:
                        streamBonus *= StrideIrregularBonus * 0.8; // 不规则，但仍有一定流式特性
                        break;
                    default:
                        streamBonus *= 0.25; // 未知
                        break;
                    }
                }

                // 检查是否在最内层循环，这通常是最热的访问点
                if (L->getSubLoops().empty())
                {
                    streamBonus *= InnerLoopBonus; // 最内层循环的流式访问更重要
                }

                MR.StreamScore += streamBonus;
                base += streamBonus;
            }
        }
    }

    // MODIFIED: Adjustment to final score calculation to reduce loop depth dominance
    // Now logarithmic scaling with trip count and depth to avoid excessive influence
    double depthFactor = std::log2(depth + 1) + 1;
    double tripFactor = std::log2(std::max(2.0, static_cast<double>(tripCount))) / 2.0;
    
    double score = base * depthFactor * tripFactor;

    // Update MallocRecord
    MR.Score += score;

    return score;
}

// 计算MemorySSA结构复杂度分析
double BandwidthAnalyzer::computeMemorySSAStructureScore(const Instruction *I)
{
    // errs() << "===== Function:computeMemorySSAStructureScore =====\n";
    using namespace WeightConfig;
    const unsigned MaxDepth = 12;
    const unsigned MaxFanOut = 5;

    const MemoryAccess *Root = MSSA.getMemoryAccess(I);
    if (!Root)
        return 0.0;

    std::set<const MemoryAccess *> Visited;
    std::queue<const MemoryAccess *> Queue;
    Queue.push(Root);

    unsigned FanOutPenalty = 0;
    unsigned PhiPenalty = 0;
    unsigned NodeCount = 0;

    while (!Queue.empty() && NodeCount < 100)
    {
        const MemoryAccess *Cur = Queue.front();
        Queue.pop();

        if (!Visited.insert(Cur).second)
            continue;

        NodeCount++;

        // 统计 MemoryPhi 的分支数
        if (auto *MP = dyn_cast<MemoryPhi>(Cur))
        {
            PhiPenalty += MP->getNumIncomingValues() - 1;
            for (auto &Op : MP->incoming_values())
                if (auto *MA = dyn_cast<MemoryAccess>(Op))
                    Queue.push(MA);
        }
        // MemoryDef / MemoryUse
        else if (auto *MU = dyn_cast<MemoryUseOrDef>(Cur))
        {
            const MemoryAccess *Def = MU->getDefiningAccess();
            if (Def)
                Queue.push(Def);
        }

        // Fan-out: 统计一个 MemoryAccess 被多个 MemoryUse 使用的情况
        unsigned UseCount = 0;
        for (const auto *User : Cur->users())
        {
            if (isa<MemoryUseOrDef>(User))
                UseCount++;
        }
        if (UseCount > MaxFanOut)
            FanOutPenalty += UseCount - MaxFanOut;
    }

    // 聚合 penalty 转换为得分（值越高说明结构越复杂）
    double penalty = PhiPenalty * PhiNodePenaltyFactor + FanOutPenalty * FanOutPenaltyFactor;
    return std::min(penalty, MaxSSAPenalty); // 最多扣MaxSSAPenalty
}

// 计算内存访问混乱度
double BandwidthAnalyzer::computeAccessChaosScore(Value *BasePtr)
{
    // errs() << "===== Function:computeAccessChaosScore =====\n";

    using namespace WeightConfig;

    if (!BasePtr)
        return 0.0;

    std::unordered_set<const GetElementPtrInst *> GEPs;
    std::unordered_set<const Type *> AccessTypes;
    std::unordered_set<const Value *> IndexSources;
    unsigned BitcastCount = 0;
    unsigned IndirectIndexCount = 0;
    unsigned NonAffineAccesses = 0;

    std::queue<const Value *> Q;
    std::unordered_set<const Value *> Visited;
    Q.push(BasePtr);

    while (!Q.empty())
    {
        const Value *V = Q.front();
        Q.pop();
        if (!Visited.insert(V).second)
            continue;

        for (const User *U : V->users())
        {
            if (auto *I = dyn_cast<Instruction>(U))
            {
                if (auto *GEP = dyn_cast<GetElementPtrInst>(I))
                {
                    GEPs.insert(GEP);
                    for (auto idx = GEP->idx_begin(); idx != GEP->idx_end(); ++idx)
                    {
                        if (!isa<ConstantInt>(idx->get()))
                        {
                            IndexSources.insert(idx->get());
                            if (isa<LoadInst>(idx->get()))
                                IndirectIndexCount++;
                        }
                    }
                    Q.push(GEP);
                }
                else if (auto *BC = dyn_cast<BitCastInst>(I))
                {
                    BitcastCount++;
                    Q.push(BC);
                }
                else if (auto *LD = dyn_cast<LoadInst>(I))
                {
                    AccessTypes.insert(LD->getType());
                }
                else if (auto *ST = dyn_cast<StoreInst>(I))
                {
                    AccessTypes.insert(ST->getValueOperand()->getType());
                }
                // 检测是否是复杂的非线性 SCEV
                if (SE.isSCEVable(I->getType()))
                {
                    const SCEV *S = SE.getSCEV(const_cast<Instruction *>(I));
                    const auto *AR = dyn_cast<SCEVAddRecExpr>(S);
                    if (!AR || !AR->isAffine())
                    {
                        NonAffineAccesses++;
                    }
                }
            }
        }
    }

    // 计算 chaos 分值
    double chaosScore = 0.0;
    if (GEPs.size() > 5)
        chaosScore += (GEPs.size() - 5) * GEPCountPenalty;
    if (IndirectIndexCount > 0)
        chaosScore += IndirectIndexCount * IndirectIndexPenalty;
    if (NonAffineAccesses > 0)
        chaosScore += NonAffineAccesses * NonAffineAccessPenalty;
    if (BitcastCount > 3)
        chaosScore += (BitcastCount - 3) * BitcastCountPenalty;
    if (AccessTypes.size() > 3)
        chaosScore += (AccessTypes.size() - 3) * TypeDiversityPenalty;
    if (chaosScore > MaxChaosPenalty)
        chaosScore = MaxChaosPenalty;

    return chaosScore;
}

// 递归分析指针用户以获取带宽使用信息
void BandwidthAnalyzer::explorePointerUsers(
    Value *RootPtr,
    Value *V,
    double &Score,
    MallocRecord &MR,
    std::unordered_set<Value *> &Visited)
{
    // errs() << "===== Function:explorePointerUsers =====\n";

    // 检查是否是空指针
    if (!RootPtr || !V)
    {
        errs() << "Warning: Null pointer passed to explorePointerUsers\n";
        return;
    }

    // 如果是访问过的Value，直接跳过
    if (Visited.count(V))
        return;
    // 标记为访问过
    Visited.insert(V);

    // Limit the number of instructions analyzed to prevent excessive runtime
    // static const size_t MAX_INSTRUCTIONS = 1000;
    // if (Visited.size() > MAX_INSTRUCTIONS) {
    //     errs() << "Warning: Stopping pointer analysis after " << MAX_INSTRUCTIONS
    //            << " instructions to prevent excessive runtime\n";
    //     return;
    // }

    try
    {
        for (User *U : V->users())
        {
            if (auto *I = dyn_cast<Instruction>(U))
            {
                // Make sure the instruction belongs to a function
                Function *InstF = I->getFunction();
                if (!InstF)
                {
                    errs() << "Warning: Instruction without function encountered\n";
                    continue;
                }

                if (auto *LD = dyn_cast<LoadInst>(I))
                {
                    if (LD->getType()->isVectorTy())
                        MR.IsVectorized = true;
                    // Calculate read access score
                    Score += computeAccessScore(LD, false, MR);
                }
                else if (auto *ST = dyn_cast<StoreInst>(I))
                {
                    if (ST->getValueOperand()->getType()->isVectorTy())
                        MR.IsVectorized = true;
                    Score += computeAccessScore(ST, true, MR);
                }
                else if (auto *CallI = dyn_cast<CallInst>(I))
                {
                    Function *CalledFunc = CallI->getCalledFunction();
                    // Handle calls safely
                    if (!CalledFunc)
                    {
                        // Indirect call - be conservative
                        Score += 5.0;
                    }
                    else if (dyn_cast<MemIntrinsic>(CallI))
                    {
                        // Memory intrinsic (memcpy, memmove, etc.)
                        Score += 3.0;
                    }
                    else
                    {
                        // Regular function call
                        Score += 3.0;
                    }
                }
                else if (auto *GEP = dyn_cast<GetElementPtrInst>(I))
                {
                    // Analyze GEP instruction for parallelism
                    ParallelismAnalyzer ParAn;
                    for (auto idx = GEP->idx_begin(); idx != GEP->idx_end(); ++idx)
                    {
                        if (!idx->get())
                            continue; // Skip null indices

                        Value *IV = idx->get();
                        if (ParAn.isThreadIDRelated(IV))
                        {
                            MR.IsThreadPartitioned = true;
                        }
                    }

                    // Handle threading conflicts
                    if (MR.IsParallel && !MR.IsThreadPartitioned && !MR.IsStreamAccess)
                    {
                        MR.MayConflict = true;
                        Score -= 5.0;
                    }

                    // Simple stream access detection
                    bool IsLikelyStream = true;
                    for (auto idx = GEP->idx_begin(); idx != GEP->idx_end(); ++idx)
                    {
                        if (!idx->get())
                            continue; // Skip null indices

                        if (auto *CI = dyn_cast<ConstantInt>(idx->get()))
                        {
                            if (CI->getSExtValue() != 0 && CI->getSExtValue() != 1)
                            {
                                IsLikelyStream = false;
                                break;
                            }
                        }
                        else
                        {
                            IsLikelyStream = false;
                            break;
                        }
                    }

                    if (IsLikelyStream)
                        MR.IsStreamAccess = true;

                    // Continue exploration
                    explorePointerUsers(RootPtr, GEP, Score, MR, Visited);
                }
                else if (auto *BC = dyn_cast<BitCastInst>(I))
                {
                    explorePointerUsers(RootPtr, BC, Score, MR, Visited);
                }
                else if (auto *ASCI = dyn_cast<AddrSpaceCastInst>(I))
                {
                    explorePointerUsers(RootPtr, ASCI, Score, MR, Visited);
                }
                else if (auto *PN = dyn_cast<PHINode>(I))
                {
                    explorePointerUsers(RootPtr, PN, Score, MR, Visited);
                }
                else if (auto *SI = dyn_cast<SelectInst>(I))
                {
                    explorePointerUsers(RootPtr, SI, Score, MR, Visited);
                }
                else if (dyn_cast<PtrToIntInst>(I))
                {
                    Score += 3.0;
                }
                else if (dyn_cast<IntToPtrInst>(I))
                {
                    explorePointerUsers(RootPtr, I, Score, MR, Visited);
                }
                else
                {
                    Score += 1.0;
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        errs() << "Error in explorePointerUsers: " << e.what() << "\n";
    }
}

// 计算带宽得分
double BandwidthAnalyzer::computeBandwidthScore(uint64_t approximateBytes, double approximateTime)
{
    // errs() << "===== Function:computeBandwidthScore =====\n";
    if (approximateTime <= 0.0)
        approximateTime = 1.0;
    double bwGBs = (double)approximateBytes / (1024.0 * 1024.0 * 1024.0) / approximateTime;
    return bwGBs * WeightConfig::BandwidthScale;
}

// Compute score based on temporal locality
double BandwidthAnalyzer::computeTemporalLocalityScore(Value *Ptr, Function *F)
{
    // errs() << "===== Function:computeTemporalLocalityScore =====\n";
    if (!Ptr || !F)
        return 0.0;

    // Use the TemporalLocalityAnalyzer to assess temporal locality
    TemporalLocalityInfo TLInfo = TLA.analyzeTemporalLocality(Ptr, *F);

    // Map locality level to score adjustment
    double score = 0.0;

    switch (TLInfo.level)
    {
    case TemporalLocalityLevel::EXCELLENT:
        // Excellent temporal locality might actually reduce HBM benefit
        // since CPU caches would be very effective
        score = -15.0; // Penalty for excellent local caching
        break;
    case TemporalLocalityLevel::GOOD:
        // Good locality slightly reduces HBM benefit
        score = -5.0;
        break;
    case TemporalLocalityLevel::MODERATE:
        // Moderate locality is neutral
        score = 0.0;
        break;
    case TemporalLocalityLevel::POOR:
        // Poor locality increases HBM benefit as caches are ineffective
        score = 20.0;
        break;
    default:
        score = 0.0;
        break;
    }

    // Consider reuse distance - very short reuse distances benefit more from CPU cache
    if (TLInfo.estimatedReuseDistance < 10)
    {
        score -= 10.0;
    }
    else if (TLInfo.estimatedReuseDistance > 1000)
    {
        score += 10.0;
    }

    return score;
}
// Analyze bank conflicts and adjust score
double BandwidthAnalyzer::analyzeBankConflicts(Value *Ptr, MallocRecord &MR)
{
    // errs() << "===== Function:analyzeBankConflicts =====\n";
    if (!Ptr)
        return 0.0;

    // Find the function this pointer is used in
    Function *F = nullptr;
    if (auto *I = dyn_cast<Instruction>(Ptr))
    {
        F = I->getFunction();
    }
    else if (auto *Arg = dyn_cast<Argument>(Ptr))
    {
        F = Arg->getParent();
    }
    else
    {
        return 0.0; // Can't determine function
    }

    if (!F)
        return 0.0;

    // Analyze bank conflicts at function level
    BankConflictInfo BCI = BCA.analyzeFunctionBankConflicts(Ptr, *F);

    // Store results in MallocRecord for later use
    MR.BankConflictSeverity = static_cast<int>(BCI.severity);
    MR.BankConflictType = static_cast<int>(BCI.type);
    MR.BankConflictRate = BCI.conflictRate;
    MR.BankConflictScore = BCI.conflictScore;
    MR.BankPerformanceImpact = BCI.performanceImpact;

    // Return score adjustment based on bank conflict analysis
    return BCI.conflictScore;
}

// Analyze dependency chains and adjust score
double BandwidthAnalyzer::analyzeDependencyChains(Value *Ptr, MallocRecord &MR)
{
    // errs() << "===== Function:analyzeDependencyChains =====\n";
    if (!Ptr)
        return 0.0;

    // Find the function this pointer is used in
    Function *F = nullptr;
    if (auto *I = dyn_cast<Instruction>(Ptr))
    {
        F = I->getFunction();
    }
    else if (auto *Arg = dyn_cast<Argument>(Ptr))
    {
        F = Arg->getParent();
    }
    else
    {
        return 0.0; // Can't determine function
    }

    if (!F)
        return 0.0;

    // Create a dependency chain analyzer for this function
    DependencyChainAnalyzer DCA(SE, LI, *F);

    // Analyze pointer's dependency characteristics
    DependencyInfo DI = DCA.analyzeDependencies(Ptr);

    // Store results in MallocRecord for later use
    MR.LatencySensitivityScore = DI.LatencySensitivityScore;
    MR.BandwidthSensitivityScore = DI.BandwidthSensitivityScore;
    MR.IsLatencyBound = DI.IsLatencyBound;
    MR.CriticalPathLatency = DI.CriticalPathLatency;
    MR.LongestPathMemoryRatio = DI.LongestPathMemoryRatio;
    MR.DependencyAnalysis = DI.Analysis;

    // Return score adjustment based on dependency analysis
    double scoreAdjustment = 0.0;

    // If primarily bandwidth sensitive, increase HBM suitability
    if (!DI.IsLatencyBound && DI.BandwidthSensitivityScore > 0.5)
    {
        scoreAdjustment += DI.BandwidthSensitivityScore * 20.0;
    }

    // If primarily latency sensitive but with high memory dependency ratio,
    // HBM can still help by reducing latency
    else if (DI.IsLatencyBound && DI.LongestPathMemoryRatio > 0.4)
    {
        scoreAdjustment += DI.LatencySensitivityScore * 10.0;
    }
    // If latency sensitive but not memory dominated, HBM helps less
    else if (DI.IsLatencyBound)
    {
        scoreAdjustment += DI.LatencySensitivityScore * 5.0;
    }

    return scoreAdjustment;
}