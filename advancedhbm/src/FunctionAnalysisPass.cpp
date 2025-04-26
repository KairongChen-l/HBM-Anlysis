#include "FunctionAnalysisPass.h"
#include "BandwidthAnalyzer.h"
#include "ContentionAnalyzer.h"
#include "CrossFunctionAnalyzer.h"
#include "DataFlowAnalyzer.h"
#include "LoopAnalyzer.h"
#include "ParallelismAnalyzer.h"
// #include "ProfileGuidedAnalyzer.h"
#include "StrideAnalyzer.h"
#include "VectorizationAnalyzer.h"
#include "PointerUtils.h"
#include "Options.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include <cmath>
#include <exception>

using namespace llvm;
using namespace MyHBM;
#define DEBUG_TYPE "function-analysis-pass"

AnalysisKey FunctionAnalysisPass::Key;

FunctionAnalysisPass::Result
FunctionAnalysisPass::run(Function &F, FunctionAnalysisManager &FAM)
{
    // 若函数只是声明，直接返回空结果
    if (F.isDeclaration())
        return {};

    // 获取需要的分析结果
    auto &LI = FAM.getResult<LoopAnalysis>(F);
    auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);

    // 获取数据布局确定指针大小
    const DataLayout &DL = F.getParent()->getDataLayout();
    uint64_t minAllocationSize = std::max(uint64_t(16), static_cast<uint64_t>(DL.getPointerSize()));

    // 添加错误处理，确保能够获取到所需的分析结果
    AAResults *AA = nullptr;
    MemorySSA *MSSA = nullptr;
    try
    {
        auto &AAM = FAM.getResult<AAManager>(F);
        AA = &AAM;
        MSSA = &FAM.getResult<MemorySSAAnalysis>(F).getMSSA();
    }
    catch (const std::exception &e)
    {
        LLVM_DEBUG(dbgs() << "Failed to get analysis results for function "
                          << F.getName() << ": " << e.what() << "\n");
        errs() << "Warning: Failed to get analysis results for function "
               << F.getName() << ": " << e.what() << "\n";
        return {};
    }

    // 创建并行化分析器
    ParallelismAnalyzer ParallelAnalyzer;
    bool parallelFound = ParallelAnalyzer.detectParallelRuntime(F);

    // 所有malloc分析的结果容器
    FunctionMallocInfo FMI;

    // 所有调用free或者释放的指令
    std::vector<CallInst *> freeCalls;

    // 遍历所有基本块和指令，寻找malloc和free调用
    for (auto &BB : F)
    {
        for (auto &I : BB)
        {
            if (auto *CI = dyn_cast<CallInst>(&I))
            {
                Function *Callee = CI->getCalledFunction();
                if (!Callee)
                    continue;

                // 获取函数名
                StringRef CalleeName = Callee->getName();

                // 识别malloc调用
                if (CalleeName == "malloc")
                {
                    MallocRecord MR;
                    MR.MallocCall = CI;

                    // 增加对参数的检查
                    if (CI->arg_size() >= 1)
                    {
                        MR.AllocSize = PointerUtils::getConstantAllocSize(CI->getArgOperand(0));

                        // 跳过极小的分配，通常不需要移到HBM
                        if (MR.AllocSize < minAllocationSize)
                        {
                            LLVM_DEBUG(dbgs() << "Skipping small allocation of size "
                                              << MR.AllocSize << " bytes\n");
                            continue;
                        }
                    }
                    else
                    {
                        // 异常情况，malloc应该至少有一个参数
                        errs() << "Warning: malloc call without arguments in "
                               << F.getName() << "\n";
                        errs() << "Warning: malloc call without arguments in "
                               << F.getName() << "\n";
                        continue;
                    }

                    // 检查返回类型是否为指针类型
                    if (!CI->getType()->isPointerTy())
                    {
                        LLVM_DEBUG(dbgs() << "Malloc call does not return a pointer in "
                                          << F.getName() << "\n");
                        continue;
                    }

                    // 检查是否通过属性或元数据标记为热点
                    if (F.hasFnAttribute("hot_mem"))
                        MR.UserForcedHot = true;
                    if (CI->hasMetadata("hot_mem"))
                        MR.UserForcedHot = true;

                    // 记录源代码位置信息
                    setSourceLocation(CI, F, MR);

                    // 检查是否为并行函数调用
                    MR.IsParallel = parallelFound;

                    // 分析并评分
                    const LoopAccessInfo *LAI = nullptr;

                    if (auto *L = LI.getLoopFor(CI->getParent()))
                    {
                        try
                        {
                            LAI = &FAM.getResult<LoopAccessAnalysis>(F).getInfo(*L);
                        }
                        catch (const std::exception &)
                        {
                            // 如果无法获取LoopAccessInfo，继续但不使用它
                            LAI = nullptr;
                        }
                    }

                    if (AA && MSSA)
                    {
                        MR.Score = analyzeMallocStatic(CI, F, LI, SE, *AA, *MSSA, LAI, MR);
                        // 记录到结果中
                        FMI.MallocRecords.push_back(MR);
                    }
                }
                // 识别C++ new操作符
                else if (CalleeName.starts_with("_Znwm") || CalleeName.starts_with("_Znam"))
                {
                    MallocRecord MR;
                    MR.MallocCall = CI;

                    // 增加参数检查
                    if (CI->arg_size() >= 1)
                    {
                        MR.AllocSize = PointerUtils::getConstantAllocSize(CI->getArgOperand(0));

                        // 跳过极小的分配
                        if (MR.AllocSize < minAllocationSize)
                        {
                            continue;
                        }
                    }
                    else
                    {
                        LLVM_DEBUG(dbgs() << "C++ new operator without size argument in "
                                          << F.getName() << "\n");
                        errs() << "Warning: C++ new operator without size argument in "
                               << F.getName() << "\n";
                        continue;
                    }

                    // 检查返回类型是否为指针类型
                    if (!CI->getType()->isPointerTy())
                    {
                        continue;
                    }

                    // 设置源码位置信息
                    setSourceLocation(CI, F, MR);

                    if (F.hasFnAttribute("hot_mem"))
                        MR.UserForcedHot = true;
                    if (CI->hasMetadata("hot_mem"))
                        MR.UserForcedHot = true;

                    MR.IsParallel = parallelFound;

                    const LoopAccessInfo *LAI = nullptr;
                    if (auto *L = LI.getLoopFor(CI->getParent()))
                    {
                        try
                        {
                            LAI = &FAM.getResult<LoopAccessAnalysis>(F).getInfo(*L);
                        }
                        catch (const std::exception &)
                        {
                            LAI = nullptr;
                        }
                    }

                    if (AA && MSSA)
                    {
                        MR.Score = analyzeMallocStatic(CI, F, LI, SE, *AA, *MSSA, LAI, MR);
                        FMI.MallocRecords.push_back(MR);
                    }
                }
                // 识别free调用
                else if (CalleeName == "free")
                {
                    freeCalls.push_back(CI);
                }
                // 识别C++ delete操作符
                else if (CalleeName.starts_with("_ZdlPv") || CalleeName.starts_with("_ZdaPv"))
                {
                    freeCalls.push_back(CI);
                }
                // 添加对自定义内存分配函数的识别
                else if (CalleeName.contains("alloc") || CalleeName.contains("Alloc"))
                {
                    // 可能是自定义的分配函数，进一步分析
                    if (CI->arg_size() >= 1 && CI->getType()->isPointerTy())
                    {
                        MallocRecord MR;
                        MR.MallocCall = CI;

                        // 尝试推断分配大小
                        for (unsigned i = 0; i < CI->arg_size(); ++i)
                        {
                            Value *Arg = CI->getArgOperand(i);
                            if (Arg->getType()->isIntegerTy())
                            {
                                MR.AllocSize = PointerUtils::getConstantAllocSize(Arg);
                                if (MR.AllocSize > 0)
                                {
                                    break;
                                }
                            }
                        }

                        // 设置源码位置
                        setSourceLocation(CI, F, MR);

                        if (F.hasFnAttribute("hot_mem"))
                            MR.UserForcedHot = true;
                        if (CI->hasMetadata("hot_mem"))
                            MR.UserForcedHot = true;

                        MR.IsParallel = parallelFound;

                        const LoopAccessInfo *LAI = nullptr;
                        if (auto *L = LI.getLoopFor(CI->getParent()))
                        {
                            try
                            {
                                LAI = &FAM.getResult<LoopAccessAnalysis>(F).getInfo(*L);
                            }
                            catch (const std::exception &)
                            {
                                LAI = nullptr;
                            }
                        }

                        if (AA && MSSA && MR.AllocSize > 16)
                        {
                            MR.Score = analyzeMallocStatic(CI, F, LI, SE, *AA, *MSSA, LAI, MR);
                            FMI.MallocRecords.push_back(MR);
                        }
                    }
                }
            }
        }
    }

    // 匹配malloc和free调用
    matchFreeCalls(FMI, freeCalls);

    return FMI;
}

double FunctionAnalysisPass::analyzeMallocStatic(
    CallInst *CI,
    Function &F,
    LoopInfo &LI,
    ScalarEvolution &SE,
    AAResults &AA,
    MemorySSA &MSSA,
    const LoopAccessInfo *LAI,
    MallocRecord &MR)
{

    if (!CI)
        return 0.0;

    double Score = 0.0;

    // 获取模块
    Module *M = F.getParent();
    if (!M)
    {
        errs() << "Error: Cannot get module for function " << F.getName() << "\n";
        return Score;
    }

    // 创建各种分析器
    CrossFunctionAnalyzer CrossFnAnalyzer;
    DataFlowAnalyzer DataFlowAnalyzer;
    ContentionAnalyzer ContentionAnalyzer(LI);
    // ProfileGuidedAnalyzer ProfileAnalyzer;

    // 添加跨函数分析
    try
    {
        MR.CrossFnInfo = CrossFnAnalyzer.analyzeCrossFunctionUsage(CI, *M);
        Score += MR.CrossFnInfo.crossFuncScore;
    }
    catch (const std::exception &e)
    {
        errs() << "Warning: Cross-function analysis failed: " << e.what() << "\n";
    }

    // 添加数据流分析
    try
    {
        MR.DataFlowData = DataFlowAnalyzer.analyzeDataFlow(CI, F);
        Score += MR.DataFlowData.dataFlowScore;
    }
    catch (const std::exception &e)
    {
        errs() << "Warning: Data flow analysis failed: " << e.what() << "\n";
    }

    // 添加竞争分析
    try
    {
        MR.ContentionData = ContentionAnalyzer.analyzeContention(CI, F);
        Score += MR.ContentionData.contentionScore;
    }
    catch (const std::exception &e)
    {
        errs() << "Warning: Contention analysis failed: " << e.what() << "\n";
    }

    // 添加Profile引导分析
    // try
    // {
    //     MR.ProfileInfo = ProfileAnalyzer.analyzeProfileData(CI, F);

    // 添加多维度评分计算
    //     MR.MultiDimScore = ProfileAnalyzer.computeMultiDimensionalScore(MR);

    // 使用Profile数据调整分数,目前暂时不适用Profile
    // MR.ProfileAdjustedScore = ProfileAnalyzer.adjustScoreWithProfile(Score, MR.ProfileInfo);
    //     MR.ProfileAdjustedScore = 60;
    // }
    // catch (const std::exception &e)
    // {
    //     errs() << "Warning: Profile analysis failed: " << e.what() << "\n";
    // 回退到基本分数
    //     MR.ProfileAdjustedScore = Score;
    // }

    // 根据分配大小调整分数 - 改进的算法
    if (MR.AllocSize > 0)
    {
        // 使用对数函数使得大小影响更合理
        double sizeFactorMB = static_cast<double>(MR.AllocSize) / (1024.0 * 1024.0);

        // 小型分配(<1MB)得分较低，中型分配(1-64MB)得分适中，大型分配(>64MB)得分高
        if (sizeFactorMB < 1.0)
        {
            Score += sizeFactorMB * 5.0; // 较小的奖励
        }
        else if (sizeFactorMB < 64.0)
        {
            Score += 5.0 + (sizeFactorMB - 1.0) * 0.2; // 中等奖励
        }
        else
        {
            Score += 17.6 + std::log2(sizeFactorMB / 64.0) * 5.0; // 大型分配的奖励
        }
    }

    // 创建带宽分析器分析内存访问模式
    try
    {
        BandwidthAnalyzer BWAnalyzer(SE, LI, AA, MSSA);

        // 递归分析指针用户和内存访问模式
        std::unordered_set<Value *> visited;
        BWAnalyzer.explorePointerUsers(CI, CI, Score, MR, visited);

        // 根据带宽使用计算额外分数
        if (MR.AccessedBytes > 0 && MR.AccessTime > 0.0)
        {
            MR.BandwidthScore = BWAnalyzer.computeBandwidthScore(MR.AccessedBytes, MR.AccessTime);
            Score += MR.BandwidthScore * Options::BandwidthScale;
        }
    }
    catch (const std::exception &e)
    {
        errs() << "Warning: Bandwidth analysis failed: " << e.what() << "\n";
    }

    // 如果是并行代码，额外加分
    if (MR.IsParallel)
        Score += Options::ParallelBonus;

    // 读取性能Profile元数据
    if (MDNode *ProfMD = CI->getMetadata("prof.memusage"))
    {
        if (ProfMD->getNumOperands() > 0)
        {
            if (auto *Op = dyn_cast<ConstantAsMetadata>(ProfMD->getOperand(0)))
            {
                if (auto *CInt = dyn_cast<ConstantInt>(Op->getValue()))
                {
                    uint64_t usage = CInt->getZExtValue();
                    Score += std::sqrt((double)usage) / 10.0;
                    MR.DynamicAccessCount = usage;
                }
            }
        }
    }

    // Consider temporal locality
    if (MR.TemporalLocalityData.temporalLocalityScore != 0.0)
    {
        // Temporal locality factors are already incorporated in the score during
        // the bandwidth analysis, but we might want to adjust here based on the
        // overall pattern
        if (MR.TemporalLocalityData.level == TemporalLocalityLevel::POOR)
        {
            // Poor temporal locality means CPU caches are ineffective
            // This makes HBM more beneficial
            Score += 5.0;
        }
        else if (MR.TemporalLocalityData.level == TemporalLocalityLevel::EXCELLENT)
        {
            // Excellent locality might make CPU caches more effective
            // This could reduce the relative benefit of HBM
            Score -= 5.0;
        }
    }

    // Consider bank conflict impact on HBM effectiveness
    if (MR.BankConflictScore != 0.0)
    {
        // Bank conflict scores are already incorporated during bandwidth analysis,
        // but we might want to weight them based on allocation size

        // For large allocations, bank conflicts can be more impactful
        if (MR.AllocSize > 1024 * 1024)
        {                                        // > 1MB
            Score += MR.BankConflictScore * 1.5; // Increase impact by 50%
        }

        // Log the impact
        errs() << "  Bank conflict adjustment to final score: "
               << (MR.BankConflictScore * (MR.AllocSize > 1024 * 1024 ? 1.5 : 1.0))
               << "\n";
    }

    // 根据内存访问模式加分
    if (MR.IsStreamAccess)
        Score += Options::StreamBonus;
    if (MR.IsVectorized)
        Score += Options::VectorBonus;

    return Score;
}

void FunctionAnalysisPass::matchFreeCalls(FunctionMallocInfo &FMI, std::vector<CallInst *> &freeCalls)
{
    // 第一遍：直接匹配
    for (auto &MR : FMI.MallocRecords)
    {
        if (!MR.MallocCall)
            continue;

        Value *mallocPtr = MR.MallocCall;
        bool matched = false;

        for (auto iter = freeCalls.begin(); iter != freeCalls.end();)
        {
            CallInst *fc = *iter;
            if (!fc || fc->arg_size() < 1)
            {
                ++iter;
                continue;
            }

            Value *freeArg = fc->getArgOperand(0);
            if (!freeArg)
            {
                ++iter;
                continue;
            }

            Value *base = nullptr;
            try
            {
                base = PointerUtils::resolveBasePointer(freeArg);
            }
            catch (const std::exception &)
            {
                // 如果解析失败，继续尝试下一个
                ++iter;
                continue;
            }

            if (base == mallocPtr)
            {
                MR.FreeCalls.push_back(fc);
                matched = true;
                // 从freeCalls中移除已匹配的项，避免重复匹配
                iter = freeCalls.erase(iter);
            }
            else
            {
                ++iter;
            }
        }

        if (!matched)
        {
            MR.UnmatchedFree = true;
            // 未匹配free可能是因为:
            // 1. 真的没有释放 - 内存泄漏
            // 2. 在另一个函数中释放
            // 3. 通过间接调用释放

            // 对于长寿命的分配，未匹配free不应该过度惩罚
            if (MR.DataFlowData.hasInitPhase && MR.DataFlowData.hasReadOnlyPhase)
            {
                // 可能是长寿命对象，减轻惩罚
                MR.Score -= 5.0;
            }
            else
            {
                // 标准惩罚
                MR.Score -= 10.0;
            }
        }
    }

    // 第二遍：尝试使用别名分析进行更复杂的匹配
    if (!freeCalls.empty() && !FMI.MallocRecords.empty())
    {
        for (auto &MR : FMI.MallocRecords)
        {
            if (!MR.UnmatchedFree || !MR.MallocCall)
                continue;

            // 已经找到匹配项，跳过
            if (!MR.FreeCalls.empty())
                continue;

            Value *mallocPtr = MR.MallocCall;
            bool matched = false;

            for (auto iter = freeCalls.begin(); iter != freeCalls.end();)
            {
                CallInst *fc = *iter;
                if (!fc || fc->arg_size() < 1)
                {
                    ++iter;
                    continue;
                }

                Value *freeArg = fc->getArgOperand(0);
                if (!freeArg)
                {
                    ++iter;
                    continue;
                }

                // 通过数据流分析进行间接匹配
                // 例如：检查是否有指针运算或类型转换
                bool potentialMatch = false;

                if (auto *GEP = dyn_cast<GetElementPtrInst>(freeArg))
                {
                    Value *gepPtr = GEP->getPointerOperand();
                    if (gepPtr == mallocPtr)
                    {
                        potentialMatch = true;
                    }
                }
                else if (auto *BC = dyn_cast<BitCastInst>(freeArg))
                {
                    Value *srcPtr = BC->getOperand(0);
                    if (srcPtr == mallocPtr)
                    {
                        potentialMatch = true;
                    }
                }

                if (potentialMatch)
                {
                    MR.FreeCalls.push_back(fc);
                    matched = true;
                    MR.UnmatchedFree = false;
                    // 恢复因未匹配扣除的分数
                    MR.Score += 10.0;
                    // 从freeCalls中移除已匹配的项
                    iter = freeCalls.erase(iter);
                }
                else
                {
                    ++iter;
                }
            }
        }
    }
}

// 设置MallocRecord的源码位置
void MyHBM::FunctionAnalysisPass::setSourceLocation(llvm::CallInst *CI, llvm::Function &F, MyHBM::MallocRecord &MR)
{
    if (DILocation *Loc = CI->getDebugLoc())
    {
        unsigned Line = Loc->getLine();
        StringRef File = Loc->getFilename();
        StringRef Dir = Loc->getDirectory();

        std::string FullPath;
        if (!Dir.empty() && !File.empty() && File.front() != '/')
            FullPath = (Dir + "/" + File).str();
        else
            FullPath = File.str();

        if (!FullPath.empty())
            MR.SourceLocation = FullPath + ":" + std::to_string(Line);
        else
            MR.SourceLocation = F.getName().str() + ":<unknown>";
    }
    else
    {
        MR.SourceLocation = F.getName().str() + ":<no_dbg>";
    }
}