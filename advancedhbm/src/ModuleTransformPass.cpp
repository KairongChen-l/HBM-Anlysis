#include "ModuleTransformPass.h"
#include "FunctionAnalysisPass.h"
#include "FunctionBandwidthAnalyzer.h"
#include "Options.h"
// #include "HBMMemoryManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/CommandLine.h"
#include <fstream>
#include <sstream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <string>
#include <regex>

using namespace llvm;
using namespace MyHBM;

PreservedAnalyses ModuleTransformPass::run(Module &M, ModuleAnalysisManager &MAM)
{
    // errs() << "===== ENTERING HBM TRANSFORM PASS =====\n";
    // 收集所有函数中的MallocRecord
    SmallVector<MallocRecord *, 16> AllMallocs;

    // 如果是通过Module来获取FunctionAnalysis就需要通过proxy获取

    // 通过ModuleProxy访问FunctionAnalysisManager获取分析结果
    auto &FAMProxy = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M);
    auto &FAM = FAMProxy.getManager();

    // 为每个函数获取分析结果
    for (auto &F : M)
    {
        // errs() << "TransformPass on function" << F.getName() << ".\n";
        if (F.isDeclaration())
            continue;

        // 获取函数级分析结果
        auto &FMI = FAM.getResult<FunctionAnalysisPass>(F);

        // 添加到总记录集合
        for (auto &MR : FMI.MallocRecords)
            AllMallocs.push_back(const_cast<MallocRecord *>(&MR));
    }

    // 如果在仅分析模式下，只生成报告，不执行变换
    if (Options::AnalysisOnly)
    {
        generateReport(M, AllMallocs, true);
        return PreservedAnalyses::all(); // 保留所有分析结果，因为我们没有修改IR
    }

    // 如果指定了外部Profile文件，加载其中的数据
    // if (!Options::ExternalProfileFile.empty())
    //     loadExternalProfile(M, AllMallocs);
    // 处理收集到的MallocRecord，决定哪些需要使用HBM
    processMallocRecords(M, AllMallocs);
    // 生成分析报告
    generateReport(M, AllMallocs, true);

    // 整理函数分配映射
    std::map<Function *, std::vector<MallocRecord *>> FunctionAllocations;
    for (auto *MR : AllMallocs)
    {
        if (MR && MR->MallocCall)
        {
            Function *F = MR->MallocCall->getFunction();
            if (F)
            {
                FunctionAllocations[F].push_back(MR);
            }
        }
    }

    // 生成函数带宽报告
    generateFunctionBandwidthReport(M, FunctionAllocations, MAM, true);
    return PreservedAnalyses::none();
}

// createMallocRecordJSON 方法实现 - 提取重复的JSON生成代码
llvm::json::Object ModuleTransformPass::createMallocRecordJSON(const MallocRecord *MR, bool includeExtendedInfo)
{
    json::Object obj;

    if (!MR || !MR->MallocCall)
        return obj;

    // 位置信息
    obj["location"] = MR->SourceLocation;
    obj["size"] = MR->AllocSize;

    // 总得分
    obj["score"] = MR->Score;

    // 得分因子（加分项）
    obj["stream_score"] = MR->StreamScore;
    obj["vector_score"] = MR->VectorScore;
    obj["parallel_score"] = MR->ParallelScore;

    // 扣分因子
    obj["ssa_penalty"] = MR->SSAPenalty;
    obj["chaos_penalty"] = MR->ChaosPenalty;
    obj["conflict_penalty"] = MR->ConflictPenalty;

    // 静态分析状态
    obj["stream"] = MR->IsStreamAccess;
    obj["vectorized"] = MR->IsVectorized;
    obj["parallel"] = MR->IsParallel;
    obj["thread_partitioned"] = MR->IsThreadPartitioned;
    obj["may_conflict"] = MR->MayConflict;

    // Loop特征
    obj["loop_depth"] = MR->LoopDepth;
    obj["trip_count"] = MR->TripCount;

    // 动态 profile 信息
    //obj["dyn_access"] = MR->DynamicAccessCount;
    //obj["est_bw"] = MR->EstimatedBandwidth;

    // 分析矛盾标志
    obj["dynamic_hot_static_low"] = MR->WasDynamicHotButStaticLow;
    obj["static_hot_dynamic_cold"] = MR->WasStaticHotButDynamicCold;

    // 其它状态标记
    obj["forced_hot"] = MR->UserForcedHot;
    // obj["unmatched_free"] = MR->UnmatchedFree;

    // 添加扩展分析结果（如果需要）
    if (includeExtendedInfo)
    {
        // 跨函数分析
        json::Object crossFnObj;
        crossFnObj["cross_func_score"] = MR->CrossFnInfo.crossFuncScore;
        crossFnObj["called_funcs_count"] = static_cast<uint64_t>(MR->CrossFnInfo.calledFunctions.size());
        crossFnObj["caller_funcs_count"] = static_cast<uint64_t>(MR->CrossFnInfo.callerFunctions.size());
        crossFnObj["external_func_propagation"] = MR->CrossFnInfo.isPropagatedToExternalFunc;
        crossFnObj["hot_func_usage"] = MR->CrossFnInfo.isUsedInHotFunction;
        obj["cross_function"] = std::move(crossFnObj);

        // 数据流分析
        json::Object dataFlowObj;
        dataFlowObj["data_flow_score"] = MR->DataFlowData.dataFlowScore;
        dataFlowObj["has_init_phase"] = MR->DataFlowData.hasInitPhase;
        dataFlowObj["has_read_only_phase"] = MR->DataFlowData.hasReadOnlyPhase;
        dataFlowObj["has_dormant_phase"] = MR->DataFlowData.hasDormantPhase;
        dataFlowObj["avg_uses_per_phase"] = MR->DataFlowData.avgUsesPerPhase;
        obj["data_flow"] = std::move(dataFlowObj);

        // 竞争分析
        json::Object contentionObj;
        contentionObj["contention_score"] = MR->ContentionData.contentionScore;
        switch (MR->ContentionData.type)
        {
        case ContentionInfo::ContentionType::NONE:
            contentionObj["contention_type"] = "none";
            break;
        case ContentionInfo::ContentionType::FALSE_SHARING:
            contentionObj["contention_type"] = "false_sharing";
            break;
        case ContentionInfo::ContentionType::ATOMIC_CONTENTION:
            contentionObj["contention_type"] = "atomic_contention";
            break;
        case ContentionInfo::ContentionType::LOCK_CONTENTION:
            contentionObj["contention_type"] = "lock_contention";
            break;
        case ContentionInfo::ContentionType::BANDWIDTH_CONTENTION:
            contentionObj["contention_type"] = "bandwidth_contention";
            break;
        }
        contentionObj["contention_probability"] = MR->ContentionData.contentionProbability;
        contentionObj["contention_points"] = MR->ContentionData.potentialContentionPoints;
        obj["contention"] = std::move(contentionObj);
    }

    return obj;
}

void ModuleTransformPass::processMallocRecords(Module &M, SmallVectorImpl<MallocRecord *> &AllMallocs)
{
    // Use fixed threshold settings
    AdaptiveThresholdInfo ThresholdInfo;
    ThresholdInfo.baseThreshold = Options::HBMThreshold;
    ThresholdInfo.adjustedThreshold = Options::HBMThreshold;
    ThresholdInfo.adjustmentReason = "Using fixed threshold (disabled ProfileGuidedAnalyzer)";

    errs() << "[HBM] Using fixed threshold: " << ThresholdInfo.adjustedThreshold << "\n";

    // Sort MallocRecords by score, prioritizing higher scores
    std::sort(AllMallocs.begin(), AllMallocs.end(),
              [](const MallocRecord *A, const MallocRecord *B)
              {
                  if (!A)
                      return false;
                  if (!B)
                      return true;

                  // First sort by user force settings
                  if (A->UserForcedHot != B->UserForcedHot)
                      return (A->UserForcedHot > B->UserForcedHot);
                  // Then sort by score
                  return (A->Score > B->Score);
              });

    // Initialize HBM capacity tracking and statistics
    uint64_t used = 0ULL;
    uint64_t capacity = DefaultHBMCapacity;

    // Create HBM allocation function
    LLVMContext &Ctx = M.getContext();
    auto *Int64Ty = Type::getInt64Ty(Ctx);
    auto *Int8PtrTy = PointerType::getUnqual(Type::getInt8Ty(Ctx));

    // Get or create the required functions
    FunctionCallee HBMAlloc = M.getOrInsertFunction(
        "hbm_malloc",
        FunctionType::get(Int8PtrTy, {Int64Ty}, false));

    //     FunctionCallee HBMCalloc = M.getOrInsertFunction(
    //     "hbm_calloc",
    //     FunctionType::get(Int8PtrTy, {Int64Ty, Int64Ty}, false));

    // FunctionCallee HBMRealloc = M.getOrInsertFunction(
    //     "hbm_realloc",
    //     FunctionType::get(Int8PtrTy, {Int8PtrTy, Int64Ty}, false));

    // FunctionCallee HBMFree = M.getOrInsertFunction(
    //     "hbm_free",
    //     FunctionType::get(Type::getVoidTy(Ctx), {Int8PtrTy}, false));

    // // For C++: Create HBM new/delete functions
    // FunctionCallee HBMNew = M.getOrInsertFunction(
    //     "hbm_new",
    //     FunctionType::get(Int8PtrTy, {Int64Ty}, false));

    // FunctionCallee HBMNewArray = M.getOrInsertFunction(
    //     "hbm_new_array",
    //     FunctionType::get(Int8PtrTy, {Int64Ty}, false));
    // Initialize memory manager function
    // FunctionCallee HBMInit = M.getOrInsertFunction(
    //     "hbm_memory_init",
    //     FunctionType::get(Type::getVoidTy(Ctx), {}, false));

    // Make sure we call the initialization function at program start
    // if (Function *MainFunc = M.getFunction("main")) {
    // Insert call to hbm_memory_init at the beginning of main
    //     if (!MainFunc->empty() && !MainFunc->front().empty()) {
    //         IRBuilder<> Builder(&MainFunc->front().front());
    //         Builder.CreateCall(HBMInit);
    //         errs() << "[HBM] Inserted initialization call in main\n";
    //     }
    // }

    // Keep track of transformed functions
    std::map<Function *, unsigned> transformedFunctions;
    std::map<std::string, unsigned> transformedTypes;

    // Track which pointers have been moved to HBM
    std::set<Value *> hbmPointers;
    // Process each MallocRecord and decide if it should be moved to HBM
    for (auto *MR : AllMallocs)
    {
        // 跳过无效记录
        if (!MR || !MR->MallocCall)
            continue;

        // 只处理 malloc 函数
        Function *Callee = MR->MallocCall->getCalledFunction();
        if (!Callee || Callee->getName() != "malloc")
            continue;

        // Determine if we should use HBM for this allocation
        bool shouldUseHBM = false;
        std::string reason;

        // User-forced hot memory always gets HBM
        if (MR->UserForcedHot)
        {
            shouldUseHBM = true;
            reason = "user-forced hot memory";
        }
        // Check score threshold
        else if (MR->Score >= ThresholdInfo.adjustedThreshold)
        {
            // Check size constraints
            if (used + MR->AllocSize <= capacity)
            {
                shouldUseHBM = true;
                reason = "score above threshold";
            }
            else
            {
                reason = "HBM capacity limit reached";
            }
        }
        else
        {
            reason = "score below threshold";
        }

        // Detailed logging
        std::string location = getSourceLocation(MR->MallocCall);
        errs() << "[HBM] Allocation at " << location
               << " | Size: " << (MR->UnknownAllocSize ? "unknown" : std::to_string(MR->AllocSize) + " bytes")
               << " | Score: " << MR->Score
               << " | Decision: " << (shouldUseHBM ? "Move to HBM" : "Keep in RAM")
               << " | Reason: " << reason << "\n";

        if (shouldUseHBM)
        {
            // Get the called function
            Value *HBMAllocCallee = HBMAlloc.getCallee();
            if (HBMAllocCallee)
            {
                // Replace malloc call with hbm_malloc
                MR->MallocCall->setCalledOperand(HBMAllocCallee);

                // Update used HBM space
                used += MR->AllocSize;

                // Track which functions were transformed
                if (Function *F = MR->MallocCall->getFunction())
                {
                    transformedFunctions[F]++;
                }

                // Keep track of allocation type
                if (Function *F = MR->MallocCall->getCalledFunction())
                {
                    if (F->getName() == "malloc")
                    {
                        transformedTypes["malloc"]++;
                    }
                }
            }
        }
    }

    // Output HBM usage statistics
    errs() << "[ModuleTransformPass] HBM used: " << used << " bytes";
    if (capacity > 0)
    {
        double usagePercent = static_cast<double>(used) / static_cast<double>(capacity) * 100.0;
        errs() << " (" << usagePercent << "% of capacity)";
    }
    errs() << "\n";

    // Output statistics on transformed functions
    errs() << "[ModuleTransformPass] Transformed allocation calls in "
           << transformedFunctions.size() << " functions:\n";
    for (auto &Pair : transformedFunctions)
    {
        if (Pair.first)
        {
            errs() << "  - " << Pair.first->getName() << ": " << Pair.second << " allocations\n";
        }
    }

    // Output statistics on transformed allocation types
    errs() << "[ModuleTransformPass] Transformed allocation types:\n";
    for (auto &Pair : transformedTypes)
    {
        errs() << "  - " << Pair.first << ": " << Pair.second << " allocations\n";
    }
}

void ModuleTransformPass::generateReport(
    const Module &M,
    ArrayRef<MallocRecord *> AllMallocs,
    bool JSONOutput)
{
    // 创建JSON数组
    json::Array root;

    // 添加更多统计信息
    uint64_t totalAllocSize = 0;
    uint64_t movedToHBMSize = 0;
    int allocCount = 0;
    int movedToHBMCount = 0;

    // 为每个MallocRecord创建JSON对象
    for (auto *MR : AllMallocs)
    {
        if (!MR || !MR->MallocCall)
            continue;
        // 使用辅助方法创建JSON对象
        allocCount++;
        totalAllocSize += MR->AllocSize;
        if (MR->UserForcedHot || MR->Score >= Options::HBMThreshold)
        {
            movedToHBMCount++;
            movedToHBMSize += MR->AllocSize;
        }

        root.push_back(createMallocRecordJSON(MR, true));
    }

    // 转换为字符串
    std::string jsonStr;
    raw_string_ostream jsonStream(jsonStr);
    jsonStream << json::Value(std::move(root));
    jsonStream.flush();

    // Use module name in the report file
    if (!Options::HBMReportFile.empty())
    {
        // Extract module name for the filename
        std::string moduleName = M.getModuleIdentifier();
        // Clean up module name to be a valid filename part
        std::replace(moduleName.begin(), moduleName.end(), '/', '_');
        std::replace(moduleName.begin(), moduleName.end(), '\\', '_');
        std::replace(moduleName.begin(), moduleName.end(), ':', '_');

        // Create filename with module name
        std::string reportFileName;

        // Check if HBMReportFile has an extension
        size_t dotPos = Options::HBMReportFile.find_last_of('.');
        if (dotPos != std::string::npos)
        {
            // Insert module name before extension
            reportFileName = Options::HBMReportFile.substr(0, dotPos) +
                             "_" + moduleName +
                             Options::HBMReportFile.substr(dotPos);
        }
        else
        {
            // No extension, just append module name
            reportFileName = Options::HBMReportFile + "_" + moduleName;
        }

        std::error_code EC;
        raw_fd_ostream out(reportFileName, EC, sys::fs::OF_Text);
        if (EC)
        {
            errs() << "Cannot open report file: " << reportFileName << " - " << EC.message() << "\n";
            return;
        }

        out << jsonStr << "\n";
        errs() << "[ModuleTransformPass] Report written to: " << reportFileName << "\n";
        errs() << "[ModuleTransformPass] Statistics: " << allocCount << " allocations, "
               << movedToHBMCount << " moved to HBM" << "\n";
    }
    else
    {
        errs() << "=== HBM Analysis Report for module: " << M.getModuleIdentifier() << " ===\n";
        errs() << jsonStr << "\n";
        errs() << "===========================\n";
    }
}

std::string ModuleTransformPass::getSourceLocation(CallInst *CI)
{
    if (!CI)
        return "<null>";

    // 尝试获取调试信息
    if (DILocation *Loc = CI->getDebugLoc())
    {
        unsigned Line = Loc->getLine();
        unsigned Column = Loc->getColumn();
        StringRef File = Loc->getFilename();
        StringRef Directory = Loc->getDirectory();

        // 构建完整路径（如果有目录信息）
        std::string FullPath;
        if (!Directory.empty() && !File.empty() && File.front() != '/')
            FullPath = (Directory + "/" + File).str();
        else
            FullPath = File.str();

        // 如果没有文件名，使用函数名代替
        if (File.empty())
        {
            Function *F = CI->getFunction();
            if (!F)
                return "<unknown function>:<no_file>:" + std::to_string(Line);

            return (F->getName() + ":<no_file>:" + std::to_string(Line) +
                    ":" + std::to_string(Column))
                .str();
        }

        // 返回"文件名:行号:列号"格式
        return FullPath + ":" + std::to_string(Line) + ":" + std::to_string(Column);
    }

    // 如果没有调试信息，只返回函数名
    Function *F = CI->getFunction();
    if (!F)
        return "<unknown function>:<no_dbg>";

    return (F->getName() + ":<no_dbg>").str();
}

void ModuleTransformPass::generateFunctionBandwidthReport(
    const Module &M,
    const std::map<Function *, std::vector<MallocRecord *>> &FunctionAllocations,
    ModuleAnalysisManager &MAM,
    bool JSONOutput)
{
    // 通过ModuleProxy访问FunctionAnalysisManager获取分析结果
    auto &FAMProxy = MAM.getResult<FunctionAnalysisManagerModuleProxy>(const_cast<Module &>(M));
    auto &FAM = FAMProxy.getManager();

    // 创建ModuleBandwidthInfo对象来存储全局结果
    ModuleBandwidthInfo ModuleInfo;
    ModuleInfo.ModuleName = M.getModuleIdentifier();

    // 分析每个函数
    for (auto &F : M)
    {
        if (F.isDeclaration())
            continue;

        // 获取该函数需要的分析结果
        LoopInfo &LI = FAM.getResult<LoopAnalysis>(const_cast<Function &>(F));
        ScalarEvolution &SE = FAM.getResult<ScalarEvolutionAnalysis>(const_cast<Function &>(F));
        AAResults &AA = FAM.getResult<AAManager>(const_cast<Function &>(F));
        MemorySSA &MSSA = FAM.getResult<MemorySSAAnalysis>(const_cast<Function &>(F)).getMSSA();

        // 创建分析器
        FunctionBandwidthAnalyzer Analyzer(LI, SE, AA, MSSA);

        // 获取该函数的内存分配记录
        std::vector<MallocRecord> Allocations;
        auto It = FunctionAllocations.find(const_cast<Function *>(&F));
        if (It != FunctionAllocations.end())
        {
            for (auto *MR : It->second)
            {
                if (MR)
                {
                    Allocations.push_back(*MR);
                }
            }
        }

        // 分析函数
        FunctionBandwidthInfo FuncInfo = Analyzer.analyze(const_cast<Function &>(F), Allocations);

        // 添加到模块信息中
        ModuleInfo.Functions.push_back(std::move(FuncInfo));
    }

    // 按带宽得分排序函数（从高到低）
    std::sort(ModuleInfo.Functions.begin(), ModuleInfo.Functions.end(),
              [](const FunctionBandwidthInfo &A, const FunctionBandwidthInfo &B)
              {
                  return A.BandwidthScore > B.BandwidthScore;
              });

    // 转换为JSON
    json::Object RootObj = ModuleInfo.toJSON();

    // 输出到文件或控制台
    std::string JsonStr;
    raw_string_ostream JsonStream(JsonStr);
    JsonStream << json::Value(std::move(RootObj));
    JsonStream.flush();

    // 根据需要输出到文件
    if (JSONOutput && !Options::HBMReportFile.empty())
    {
        // 提取模块名用于文件名
        std::string ModuleName = M.getModuleIdentifier();
        // 清理模块名以确保是有效的文件名部分
        std::replace(ModuleName.begin(), ModuleName.end(), '/', '_');
        std::replace(ModuleName.begin(), ModuleName.end(), '\\', '_');
        std::replace(ModuleName.begin(), ModuleName.end(), ':', '_');

        // 创建带有模块名的文件名
        std::string ReportFileName = Options::HBMReportFile;
        size_t DotPos = ReportFileName.find_last_of('.');
        if (DotPos != std::string::npos)
        {
            ReportFileName = ReportFileName.substr(0, DotPos) +
                             "_functions_" + ModuleName +
                             ReportFileName.substr(DotPos);
        }
        else
        {
            ReportFileName += "_functions_" + ModuleName;
        }

        // 打开文件并写入
        std::error_code EC;
        raw_fd_ostream Out(ReportFileName, EC, sys::fs::OF_Text);
        if (EC)
        {
            errs() << "Cannot open function bandwidth report file: "
                   << ReportFileName << " - " << EC.message() << "\n";
            // 如果无法打开文件，回退到控制台输出
            errs() << "Outputting report to console instead:\n";
            errs() << JsonStr << "\n";
            return;
        }

        Out << JsonStr << "\n";
        errs() << "[ModuleTransformPass] Function bandwidth report written to: "
               << ReportFileName << "\n";
        errs() << "[ModuleTransformPass] Found "
               << ModuleInfo.Functions.size() << " functions with "
               << std::count_if(ModuleInfo.Functions.begin(), ModuleInfo.Functions.end(),
                                [](const FunctionBandwidthInfo &Info)
                                {
                                    return Info.BandwidthScore > Options::HBMThreshold;
                                })
               << " bandwidth-sensitive functions\n";
    }
    else
    {
        // 直接输出到控制台
        errs() << "=== Function Bandwidth Analysis Report for module: "
               << M.getModuleIdentifier() << " ===\n";
        errs() << JsonStr << "\n";
        errs() << "===========================\n";
    }
}