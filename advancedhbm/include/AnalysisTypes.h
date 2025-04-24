#ifndef MYHBM_ANALYSIS_TYPES_H
#define MYHBM_ANALYSIS_TYPES_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include <vector>
#include <string>
#include <map>
#include <optional>

namespace MyHBM
{

  // 枚举定义步长类型
  enum class StrideType
  {
    UNKNOWN,   // 无法确定
    IRREGULAR, // 不规则访问
    COMPLEX,   // 复杂但有规律的访问
    LINEAR,    // 线性步长访问
    CONSTANT   // 常量步长访问（最优）
  };

  // 线程并行分析枚举
  enum class ThreadAccessPattern
  {
    UNKNOWN,         // 未知模式
    PRIVATE,         // 线程私有数据
    PARTITIONED,     // 按线程ID分区访问（良性）
    SHARED_READONLY, // 只读共享（良性）
    SHARED_WRITE,    // 写入共享（可能有冲突）
    ATOMIC_ACCESS,   // 原子访问（有同步开销）
    FALSE_SHARING    // 伪共享（不良）
  };

  // 数据局部性分析
  enum class LocalityType
  {
    POOR,     // 差的局部性
    MODERATE, // 中等局部性
    GOOD,     // 良好的局部性
    EXCELLENT // 极佳的局部性
  };

  // Profile引导优化
  struct ProfileGuidedInfo
  {
    bool hasProfileData = false;
    double staticConfidence = 0.0;                            // 0.0-1.0，表示对静态分析结果的信心
    double dynamicWeight = 0.0;                               // 动态Profile的权重
    std::vector<std::pair<std::string, double>> hotspotHints; // 热点提示
  };

  // 自适应阈值分析
  struct AdaptiveThresholdInfo
  {
    double baseThreshold = 50.0;     // 基础阈值
    double adjustedThreshold = 50.0; // 调整后的阈值
    std::string adjustmentReason;    // 调整原因
  };

  // 多维度评分
  struct MultiDimensionalScore
  {
    double bandwidthScore = 0.0;      // 带宽需求得分
    double latencyScore = 0.0;        // 延迟敏感度得分
    double utilizationScore = 0.0;    // 利用率得分
    double sizeEfficiencyScore = 0.0; // 大小效率得分
    double finalScore = 0.0;          // 最终综合得分
  };

  // 跨函数分析
  struct CrossFunctionInfo
  {
    bool analyzedCrossFn = false;
    std::vector<llvm::Function *> calledFunctions;
    std::vector<llvm::Function *> callerFunctions;
    bool isPropagatedToExternalFunc = false;
    bool isUsedInHotFunction = false;
    double crossFuncScore = 0.0;
  };

  // 全程序数据流分析
  struct DataFlowInfo
  {
    enum class LifetimePhase
    {
      ALLOCATION,
      INITIALIZATION,
      ACTIVE_USE,
      READ_ONLY,
      DORMANT,
      DEALLOCATION
    };

    std::map<llvm::Instruction *, LifetimePhase> phaseMap;
    bool hasInitPhase = false;
    bool hasReadOnlyPhase = false;
    bool hasDormantPhase = false;
    double avgUsesPerPhase = 0.0;
    double dataFlowScore = 0.0;
  };

  // 竞争分析
  struct ContentionInfo
  {
    enum class ContentionType
    {
      NONE,                // 无竞争
      FALSE_SHARING,       // 伪共享
      ATOMIC_CONTENTION,   // 原子操作竞争
      LOCK_CONTENTION,     // 锁竞争
      BANDWIDTH_CONTENTION // 带宽竞争
    };

    ContentionType type = ContentionType::NONE;
    double contentionProbability = 0.0;
    unsigned potentialContentionPoints = 0;
    double contentionScore = 0.0;
  };

  // 交错访问模式分析
  struct InterleavedAccessInfo
  {
    bool isInterleaved = false;
    unsigned accessedArrays = 0;
    double strideRatio = 0.0;
    bool isPotentiallyBandwidthBound = false;
    bool isCoalesced = false;
    bool hasInterleavedReadWrite = false;
  };

} // namespace MyHBM

#endif // MYHBM_ANALYSIS_TYPES_H