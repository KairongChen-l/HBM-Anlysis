#include "MallocRecord.h"
#include "llvm/Support/JSON.h"
#include <sstream>

using namespace llvm;
using namespace MyHBM;

// 生成记录的JSON表示
std::string MallocRecord::ToJsonString() const
{   
    errs() << "===== Function:ToJsonString =====\n";
    json::Object Obj;

    // 基本信息
    Obj["source_location"] = SourceLocation;
    Obj["alloc_size"] = static_cast<uint64_t>(AllocSize);
    Obj["loop_depth"] = LoopDepth;
    Obj["trip_count"] = TripCount;

    // 状态标志
    Obj["is_parallel"] = IsParallel;
    Obj["is_vectorized"] = IsVectorized;
    Obj["is_stream_access"] = IsStreamAccess;
    Obj["is_thread_partitioned"] = IsThreadPartitioned;
    Obj["may_conflict"] = MayConflict;
    Obj["user_forced_hot"] = UserForcedHot;
    Obj["unmatched_free"] = UnmatchedFree;

    // 动态Profile
    Obj["dynamic_access_count"] = DynamicAccessCount;
    Obj["estimated_bandwidth"] = EstimatedBandwidth;

    // 带宽相关
    Obj["accessed_bytes"] = AccessedBytes;
    Obj["access_time"] = AccessTime;
    Obj["bandwidth_score"] = BandwidthScore;

    // 动态静态冲突标记
    Obj["was_dynamic_hot_but_static_low"] = WasDynamicHotButStaticLow;
    Obj["was_static_hot_but_dynamic_cold"] = WasStaticHotButDynamicCold;

    // 并行访问分析
    Obj["parallel_framework"] = ParallelFramework;
    Obj["estimated_threads"] = EstimatedThreads;
    Obj["has_atomic_access"] = HasAtomicAccess;
    Obj["has_false_sharing"] = HasFalseSharing;
    Obj["is_read_only"] = IsReadOnly;

    // 得分结果
    Obj["score"] = Score;
    Obj["stream_score"] = StreamScore;
    Obj["vector_score"] = VectorScore;
    Obj["parallel_score"] = ParallelScore;
    Obj["ssa_penalty"] = SSAPenalty;
    Obj["chaos_penalty"] = ChaosPenalty;
    Obj["conflict_penalty"] = ConflictPenalty;

    // Profile调整的得分
    Obj["profile_adjusted_score"] = ProfileAdjustedScore;

    // 多维度评分
    json::Object MultiDimObj;
    MultiDimObj["bandwidth_score"] = MultiDimScore.bandwidthScore;
    MultiDimObj["latency_score"] = MultiDimScore.latencyScore;
    MultiDimObj["utilization_score"] = MultiDimScore.utilizationScore;
    MultiDimObj["size_efficiency_score"] = MultiDimScore.sizeEfficiencyScore;
    MultiDimObj["final_score"] = MultiDimScore.finalScore;
    Obj["multi_dim_score"] = std::move(MultiDimObj);

    // 跨函数分析
    json::Object CrossFnObj;
    CrossFnObj["analyzed_cross_fn"] = CrossFnInfo.analyzedCrossFn;
    CrossFnObj["called_functions_count"] = CrossFnInfo.calledFunctions.size();
    CrossFnObj["caller_functions_count"] = CrossFnInfo.callerFunctions.size();
    CrossFnObj["is_propagated_to_external_func"] = CrossFnInfo.isPropagatedToExternalFunc;
    CrossFnObj["is_used_in_hot_function"] = CrossFnInfo.isUsedInHotFunction;
    CrossFnObj["cross_func_score"] = CrossFnInfo.crossFuncScore;
    Obj["cross_fn_info"] = std::move(CrossFnObj);

    // 数据流分析
    json::Object DataFlowObj;
    DataFlowObj["has_init_phase"] = DataFlowData.hasInitPhase;
    DataFlowObj["has_read_only_phase"] = DataFlowData.hasReadOnlyPhase;
    DataFlowObj["has_dormant_phase"] = DataFlowData.hasDormantPhase;
    DataFlowObj["avg_uses_per_phase"] = DataFlowData.avgUsesPerPhase;
    DataFlowObj["data_flow_score"] = DataFlowData.dataFlowScore;
    Obj["data_flow_info"] = std::move(DataFlowObj);

    // 竞争分析
    json::Object ContentionObj;
    switch (ContentionData.type)
    {
    case ContentionInfo::ContentionType::NONE:
        ContentionObj["type"] = "NONE";
        break;
    case ContentionInfo::ContentionType::FALSE_SHARING:
        ContentionObj["type"] = "FALSE_SHARING";
        break;
    case ContentionInfo::ContentionType::ATOMIC_CONTENTION:
        ContentionObj["type"] = "ATOMIC_CONTENTION";
        break;
    case ContentionInfo::ContentionType::LOCK_CONTENTION:
        ContentionObj["type"] = "LOCK_CONTENTION";
        break;
    case ContentionInfo::ContentionType::BANDWIDTH_CONTENTION:
        ContentionObj["type"] = "BANDWIDTH_CONTENTION";
        break;
    }
    ContentionObj["contention_probability"] = ContentionData.contentionProbability;
    ContentionObj["potential_contention_points"] = ContentionData.potentialContentionPoints;
    ContentionObj["contention_score"] = ContentionData.contentionScore;
    Obj["contention_info"] = std::move(ContentionObj);

    json::Object TemporalObj;

    // 转换为字符串
    std::string JsonStr;
    // 预分配足够的空间，避免频繁重新分配
    JsonStr.reserve(2048);
    raw_string_ostream OS(JsonStr);
    OS << json::Value(std::move(Obj));
    OS.flush();
    return JsonStr;
}