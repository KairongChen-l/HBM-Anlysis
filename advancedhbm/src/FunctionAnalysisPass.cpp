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

#include "WeightConfig.h" // Include the weight configuration header
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
        // Add this line to provide a fallback for error cases:
        if (!AA)
            AA = nullptr;
        if (!MSSA)
            MSSA = nullptr;
        return {};
    }

    // 创建并行化分析器
    ParallelismAnalyzer ParallelAnalyzer;
    bool parallelFound = ParallelAnalyzer.detectParallelRuntime(F);

    // 所有malloc分析的结果容器
    FunctionMallocInfo FMI;

    // 所有调用free或者释放的指令
    // std::vector<CallInst *> freeCalls;

    // In FunctionAnalysisPass.cpp, modify the loop in run() that detects allocations:
    for (auto &BB : F)
    {
        for (auto &I : BB)
        {
            if (auto *CI = dyn_cast<CallInst>(&I))
            {
                Function *Callee = CI->getCalledFunction();

                if (Callee && Callee->getName() == "malloc")
                {
                    MallocRecord MR;
                    MR.MallocCall = CI;

                    // 检查参数大小
                    if (CI->arg_size() >= 1)
                    {
                        Value *SizeArg = CI->getArgOperand(0);
                        // 尝试获取常量大小
                        if (auto *ConstSize = dyn_cast<ConstantInt>(SizeArg))
                        {
                            // 如果是常量大小，可以直接获取
                            MR.AllocSize = ConstSize->getZExtValue();
                        }
                        else
                        {
                            // 如果大小不是常量，标记为未知大小
                            MR.UnknownAllocSize = true;
                            // 设置一个合理的非零默认值
                            MR.AllocSize = 16; // 使用一个小但非零的默认值
                        }
                    }
                    else
                    {
                        // 如果参数数量不对，也标记为未知大小
                        MR.UnknownAllocSize = true;
                        MR.AllocSize = 16;
                    }

                    // 设置源码位置等其他信息...
                    setSourceLocation(CI, F, MR);

                    // 检查热内存属性
                    if (F.hasFnAttribute("hot_mem"))
                        MR.UserForcedHot = true;
                    if (CI->hasMetadata("hot_mem"))
                        MR.UserForcedHot = true;

                    // 检查并行执行
                    MR.IsParallel = parallelFound;

                    // 分析和评分
                    if (AA && MSSA)
                    {
                        const LoopAccessInfo *LAI = nullptr;
                        // 获取 LAI...

                        MR.Score = analyzeMallocStatic(CI, F, LI, SE, *AA, *MSSA, LAI, MR);
                        FMI.MallocRecords.push_back(MR);
                    }
                }
                //     // Create a helper function to recognize allocations
                //     bool isAllocation = false;
                //     uint64_t allocSize = 0;
                //     std::string allocType = "";

                //     if (Callee)
                //     {
                //         StringRef CalleeName = Callee->getName();

                //         // Standard malloc/calloc functions
                //         if (CalleeName == "malloc")
                //         {
                //             isAllocation = true;
                //             allocType = "malloc";
                //             if (CI->arg_size() >= 1)
                //             {
                //                 allocSize = PointerUtils::getConstantAllocSize(CI->getArgOperand(0));
                //             }
                //         }
                //         else if (CalleeName == "calloc")
                //         {
                //             isAllocation = true;
                //             allocType = "calloc";
                //             if (CI->arg_size() >= 2)
                //             {
                //                 uint64_t numElements = PointerUtils::getConstantAllocSize(CI->getArgOperand(0));
                //                 uint64_t elemSize = PointerUtils::getConstantAllocSize(CI->getArgOperand(1));
                //                 allocSize = numElements * elemSize;
                //             }
                //         }
                //         else if (CalleeName == "realloc")
                //         {
                //             isAllocation = true;
                //             allocType = "realloc";
                //             if (CI->arg_size() >= 2)
                //             {
                //                 allocSize = PointerUtils::getConstantAllocSize(CI->getArgOperand(1));
                //             }
                //         }
                //         // C++ allocation functions
                //         else if (CalleeName.starts_with("_Znwm") || CalleeName.starts_with("_Znam"))
                //         {
                //             isAllocation = true;
                //             allocType = "new";
                //             if (CI->arg_size() >= 1)
                //             {
                //                 allocSize = PointerUtils::getConstantAllocSize(CI->getArgOperand(0));
                //             }
                //         }
                //         // Custom allocators (more aggressive matching)
                //         else if (CalleeName.contains("alloc") || CalleeName.contains("Alloc") ||
                //                  CalleeName.contains("create") || CalleeName.contains("Create") ||
                //                  CalleeName.contains("get") || CalleeName.contains("Get"))
                //         {

                //             // Check return type is pointer
                //             if (CI->getType()->isPointerTy())
                //             {
                //                 // Check if any argument is a size
                //                 for (unsigned i = 0; i < CI->arg_size(); ++i)
                //                 {
                //                     Value *Arg = CI->getArgOperand(i);
                //                     if (Arg->getType()->isIntegerTy())
                //                     {
                //                         uint64_t potentialSize = PointerUtils::getConstantAllocSize(Arg);
                //                         if (potentialSize > 0)
                //                         {
                //                             isAllocation = true;
                //                             allocSize = potentialSize;
                //                             allocType = CalleeName.str();
                //                             break;
                //                         }
                //                     }
                //                 }
                //             }
                //         }
                //     }
                //     else
                //     {
                //         // Handle indirect calls that look like allocations
                //         if (CI->getType()->isPointerTy() && CI->arg_size() > 0)
                //         {
                //             Value *CalledValue = CI->getCalledOperand();
                //             // If the called value has "alloc" in its name or types match malloc pattern
                //             if ((CalledValue->getName().contains("alloc") ||
                //                  CalledValue->getName().contains("Alloc")) &&
                //                 CI->getArgOperand(0)->getType()->isIntegerTy())
                //             {

                //                 isAllocation = true;
                //                 allocType = "indirect_alloc";
                //                 allocSize = PointerUtils::getConstantAllocSize(CI->getArgOperand(0));
                //             }
                //         }
                //     }

                //     // Process the allocation if found
                //     if (isAllocation && CI->getType()->isPointerTy())
                //     {
                //         MallocRecord MR;
                //         MR.MallocCall = CI;
                //         MR.AllocSize = allocSize;
                //         setSourceLocation(CI, F, MR);

                //         // Skip extremely small allocations
                //         if (MR.AllocSize < minAllocationSize)
                //         {
                //             LLVM_DEBUG(dbgs() << "Skipping small allocation of size "
                //                               << MR.AllocSize << " bytes\n");
                //             continue;
                //         }

                //         // Check for hot memory attributes
                //         if (F.hasFnAttribute("hot_mem"))
                //             MR.UserForcedHot = true;
                //         if (CI->hasMetadata("hot_mem"))
                //             MR.UserForcedHot = true;

                //         // Check for parallel execution
                //         MR.IsParallel = parallelFound;

                //         // Analyze and score the allocation
                //         const LoopAccessInfo *LAI = nullptr;
                //         if (auto *L = LI.getLoopFor(CI->getParent()))
                //         {
                //             try
                //             {
                //                 LAI = &FAM.getResult<LoopAccessAnalysis>(F).getInfo(*L);
                //             }
                //             catch (const std::exception &)
                //             {
                //                 LAI = nullptr;
                //             }
                //         }

                //         if (AA && MSSA)
                //         {
                //             MR.Score = analyzeMallocStatic(CI, F, LI, SE, *AA, *MSSA, LAI, MR);
                //             FMI.MallocRecords.push_back(MR);
                //         }
                //     }
            }
        }
    }

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
    using namespace WeightConfig; // Use our weight configuration
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
        Score += MR.CrossFnInfo.crossFuncScore * 0.8;
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
        if (MR.ContentionData.type == ContentionInfo::ContentionType::BANDWIDTH_CONTENTION)
        {
            // Bandwidth contention is important for HBM selection - weight higher
            Score += MR.ContentionData.contentionScore * 1.2;
        }
        else
        {
            Score += MR.ContentionData.contentionScore;
        }
    }
    catch (const std::exception &e)
    {
        errs() << "Warning: Contention analysis failed: " << e.what() << "\n";
    }

    // 根据分配大小调整分数 - 改进的算法
    if (MR.AllocSize > 0)
    {
        // 使用对数函数使得大小影响更合理
        double sizeFactorMB = static_cast<double>(MR.AllocSize) / (1024.0 * 1024.0);

        // 小型分配(<1MB)得分较低，中型分配(1-64MB)得分适中，大型分配(>64MB)得分高
        if (sizeFactorMB < 0.1)
        {
            // Very small allocations (<100KB) - minimal score
            Score += sizeFactorMB * 3.0;
        }
        else if (sizeFactorMB < 1.0)
        {
            // Small allocations (100KB-1MB) - modest score
            Score += 0.3 + (sizeFactorMB - 0.1) * 4.7;
        }
        else if (sizeFactorMB < 16.0)
        {
            // Medium allocations (1MB-16MB) - progressive score
            Score += 5.0 + (sizeFactorMB - 1.0) * 0.5;
        }
        else if (sizeFactorMB < 256.0)
        {
            // Large allocations (16MB-256MB) - high score, diminishing returns
            Score += 12.5 + std::log2(sizeFactorMB / 16.0) * 5.0;
        }
        else
        {
            // Very large allocations (>256MB) - high but capped score
            Score += 20.0;
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

            // MODIFIED: Adjust bandwidth scaling based on whether it's stream access
            double scalingFactor = MR.IsStreamAccess ? 1.2 : 1.0;
            Score += MR.BandwidthScore * BandwidthScale * scalingFactor;
        }
    }
    catch (const std::exception &e)
    {
        errs() << "Warning: Bandwidth analysis failed: " << e.what() << "\n";
    }

    // 如果是并行代码，额外加分
    if (MR.IsParallel)
        Score += WeightConfig::ParallelBonus;

    // Temporal locality adjustments - MODIFIED to be more nuanced
    if (MR.TemporalLocalityData.temporalLocalityScore != 0.0)
    {
        if (MR.TemporalLocalityData.level == TemporalLocalityLevel::POOR)
        {
            // Poor temporal locality means CPU caches are ineffective
            // This makes HBM more beneficial - increase score
            Score += 15.0;
        }
        else if (MR.TemporalLocalityData.level == TemporalLocalityLevel::MODERATE)
        {
            // Moderate locality - slight boost
            Score += 5.0;
        }
        else if (MR.TemporalLocalityData.level == TemporalLocalityLevel::GOOD)
        {
            // Good locality might make CPU cache effective
            // This reduces the relative benefit of HBM - slight penalty
            Score -= 5.0;
        }
        else if (MR.TemporalLocalityData.level == TemporalLocalityLevel::EXCELLENT)
        {
            // Excellent locality makes CPU caches very effective
            // This significantly reduces the benefit of HBM - larger penalty
            Score -= 15.0;
        }
    }

    // Consider bank conflict impact on HBM effectiveness
    // Bank conflict adjustments - MODIFIED to be more calibrated
    if (MR.BankConflictScore != 0.0)
    {
        double conflictImpact = MR.BankConflictScore;

        // For large allocations, conflicts have more impact
        if (MR.AllocSize > 1024 * 1024)
        {
            conflictImpact *= (1.0 + std::log2(MR.AllocSize / (1024.0 * 1024.0)) * 0.1);
        }

        Score += conflictImpact;
    }

    // Dependency chain analysis
    if (MR.LatencySensitivityScore > 0.0 || MR.BandwidthSensitivityScore > 0.0)
    {
        // MODIFIED: More nuanced scoring based on dependency characteristics
        if (MR.IsLatencyBound)
        {
            if (MR.LongestPathMemoryRatio > 0.7)
            {
                // Memory operations dominate critical path
                Score += 10.0 + 10.0 * MR.LongestPathMemoryRatio;
            }
            else
            {
                // Memory on critical path but not dominant
                Score += 5.0 + 5.0 * MR.LongestPathMemoryRatio;
            }
        }
        else
        {
            // Bandwidth-bound allocations benefit strongly from HBM
            // MODIFIED: Increased bandwidth sensitivity impact
            Score += MR.BandwidthSensitivityScore * 25.0;

            // Bonus if also somewhat latency sensitive
            if (MR.LatencySensitivityScore > 0.3)
            {
                Score += 5.0;
            }
        }
    }
    // Memory access pattern bonuses - MODIFIED to emphasize streaming
    if (MR.IsStreamAccess)
        Score += StreamBonus * 1.1; // Additional 10% bonus for verified streaming access

    if (MR.IsVectorized)
        Score += VectorBonus;

    return Score;
}

// Enhanced malloc detection in FunctionAnalysisPass.cpp
bool isAllocationFunction(StringRef FuncName)
{
    // Standard C/C++ allocation functions
    if (FuncName == "malloc" ||
        FuncName == "calloc" ||
        FuncName == "realloc" ||
        FuncName == "aligned_alloc" ||
        FuncName == "posix_memalign" ||
        FuncName.starts_with("_Znwm") || // C++ new
        FuncName.starts_with("_Znam"))   // C++ new[]
        return true;

    // Common custom allocators often have these patterns
    if (FuncName.contains("alloc") ||
        FuncName.contains("Alloc") ||
        FuncName.contains("create") ||
        FuncName.contains("Create") ||
        FuncName.contains("new") ||
        FuncName.contains("New") ||
        (FuncName.contains("get") && FuncName.contains("memory")))
        return true;

    return false;
}

// void FunctionAnalysisPass::matchFreeCalls(FunctionMallocInfo &FMI, std::vector<CallInst *> &freeCalls)
// {
//     // 第一遍：直接匹配
//     for (auto &MR : FMI.MallocRecords)
//     {
//         if (!MR.MallocCall)
//             continue;

//         Value *mallocPtr = MR.MallocCall;
//         bool matched = false;

//         for (auto iter = freeCalls.begin(); iter != freeCalls.end();)
//         {
//             CallInst *fc = *iter;
//             if (!fc || fc->arg_size() < 1)
//             {
//                 ++iter;
//                 continue;
//             }

//             Value *freeArg = fc->getArgOperand(0);
//             if (!freeArg)
//             {
//                 ++iter;
//                 continue;
//             }

//             Value *base = nullptr;
//             try
//             {
//                 base = PointerUtils::resolveBasePointer(freeArg);
//             }
//             catch (const std::exception &)
//             {
//                 // 如果解析失败，继续尝试下一个
//                 ++iter;
//                 continue;
//             }

//             if (base == mallocPtr)
//             {
//                 MR.FreeCalls.push_back(fc);
//                 matched = true;
//                 // 从freeCalls中移除已匹配的项，避免重复匹配
//                 iter = freeCalls.erase(iter);
//             }
//             else
//             {
//                 ++iter;
//             }
//         }

//         if (!matched)
//         {
//             MR.UnmatchedFree = true;
//             // 未匹配free可能是因为:
//             // 1. 真的没有释放 - 内存泄漏
//             // 2. 在另一个函数中释放
//             // 3. 通过间接调用释放

//             // 对于长寿命的分配，未匹配free不应该过度惩罚
//             if (MR.DataFlowData.hasInitPhase && MR.DataFlowData.hasReadOnlyPhase)
//             {
//                 // 可能是长寿命对象，减轻惩罚
//                 MR.Score -= 5.0;
//             }
//             else
//             {
//                 // 标准惩罚
//                 MR.Score -= 10.0;
//             }
//         }
//     }

//     // 第二遍：尝试使用别名分析进行更复杂的匹配
//     if (!freeCalls.empty() && !FMI.MallocRecords.empty())
//     {
//         for (auto &MR : FMI.MallocRecords)
//         {
//             if (!MR.UnmatchedFree || !MR.MallocCall)
//                 continue;

//             // 已经找到匹配项，跳过
//             if (!MR.FreeCalls.empty())
//                 continue;

//             Value *mallocPtr = MR.MallocCall;
//             bool matched = false;

//             for (auto iter = freeCalls.begin(); iter != freeCalls.end();)
//             {
//                 CallInst *fc = *iter;
//                 if (!fc || fc->arg_size() < 1)
//                 {
//                     ++iter;
//                     continue;
//                 }

//                 Value *freeArg = fc->getArgOperand(0);
//                 if (!freeArg)
//                 {
//                     ++iter;
//                     continue;
//                 }

//                 // 通过数据流分析进行间接匹配
//                 // 例如：检查是否有指针运算或类型转换
//                 bool potentialMatch = false;

//                 if (auto *GEP = dyn_cast<GetElementPtrInst>(freeArg))
//                 {
//                     Value *gepPtr = GEP->getPointerOperand();
//                     if (gepPtr == mallocPtr)
//                     {
//                         potentialMatch = true;
//                     }
//                 }
//                 else if (auto *BC = dyn_cast<BitCastInst>(freeArg))
//                 {
//                     Value *srcPtr = BC->getOperand(0);
//                     if (srcPtr == mallocPtr)
//                     {
//                         potentialMatch = true;
//                     }
//                 }

//                 if (potentialMatch)
//                 {
//                     MR.FreeCalls.push_back(fc);
//                     matched = true;
//                     MR.UnmatchedFree = false;
//                     // 恢复因未匹配扣除的分数
//                     MR.Score += 10.0;
//                     // 从freeCalls中移除已匹配的项
//                     iter = freeCalls.erase(iter);
//                 }
//                 else
//                 {
//                     ++iter;
//                 }
//             }
//         }
//     }
// }

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