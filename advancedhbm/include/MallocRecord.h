#ifndef MYHBM_MALLOC_RECORD_H
#define MYHBM_MALLOC_RECORD_H

#include "llvm/IR/Instruction.h"
#include "AnalysisTypes.h"
#include <string>
#include <vector>

namespace MyHBM
{

  // 用于记录单个 malloc 调用点的分析结果
  class MallocRecord
  {
  public:
    MallocRecord() = default;
    ~MallocRecord() = default;

    // 基本信息
    llvm::CallInst *MallocCall = nullptr;
    std::vector<llvm::CallInst *> FreeCalls;

    // 位置信息（文件+行号）
    std::string SourceLocation;

    // 静态信息
    size_t AllocSize = 0;
    unsigned LoopDepth = 0;
    uint64_t TripCount = 1;

    // 状态标志
    bool IsParallel = false;
    bool IsVectorized = false;
    bool IsStreamAccess = false;
    bool IsThreadPartitioned = false;
    bool MayConflict = false;
    bool UserForcedHot = false;
    bool UnmatchedFree = false;

    // 动态 profile
    uint64_t DynamicAccessCount = 0;
    double EstimatedBandwidth = 0.0;

    // 为带宽计算添加的辅助字段
    uint64_t AccessedBytes = 0;
    double AccessTime = 0.0;
    double BandwidthScore = 0.0;

    // 动态静态冲突标记
    bool WasDynamicHotButStaticLow = false;
    bool WasStaticHotButDynamicCold = false;

    // 增加并行访问分析字段
    std::string ParallelFramework; // 并行框架类型（OpenMP、CUDA、TBB等）
    unsigned EstimatedThreads = 1; // 估计的并行线程数
    bool HasAtomicAccess = false;  // 是否有原子访问
    bool HasFalseSharing = false;  // 是否存在伪共享
    bool IsReadOnly = false;       // 是否只读访问

    // 评分结果（原始总分）
    double Score = 0.0;

    // 以下为拆分后的细化得分因子
    // 加分项
    double StreamScore = 0.0;
    double VectorScore = 0.0;
    double ParallelScore = 0.0;

    // 扣分项
    double SSAPenalty = 0.0;
    double ChaosPenalty = 0.0;
    double ConflictPenalty = 0.0;

    // 新增的动态特性成员
    ProfileGuidedInfo ProfileInfo;
    double ProfileAdjustedScore = 0.0;

    // 多维度评分
    MultiDimensionalScore MultiDimScore;

    // 新增的扩展分析成员
    CrossFunctionInfo CrossFnInfo;
    DataFlowInfo DataFlowData;
    ContentionInfo ContentionData;
    // 返回记录的JSON表示
    std::string ToJsonString() const;
  };

  // 函数级的分析结果容器
  struct FunctionMallocInfo
  {
    std::vector<MallocRecord> MallocRecords;
  };

} // namespace MyHBM

#endif // MYHBM_MALLOC_RECORD_H