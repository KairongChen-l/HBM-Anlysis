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

  enum class TemporalLocalityLevel
  {
    EXCELLENT, // Reused immediately or very frequently
    GOOD,      // Short reuse distance or consistent reuse
    MODERATE,  // Medium reuse distance
    POOR,      // Long or no reuse pattern detected
    UNKNOWN    // Cannot determine
  };

  struct TemporalLocalityInfo
  {
    TemporalLocalityLevel level;
    unsigned estimatedReuseDistance;
    double reuseFrequency;        // Estimated reuses per loop iteration
    bool isLoopInvariant;         // Is value invariant across loop iterations
    double temporalLocalityScore; // 0-100 score for HBM suitability

    TemporalLocalityInfo()
        : level(TemporalLocalityLevel::UNKNOWN),
          estimatedReuseDistance(UINT_MAX),
          reuseFrequency(0.0),
          isLoopInvariant(false),
          temporalLocalityScore(0.0) {}
  };

  // Represents a node in the dependency graph
  struct DependencyNode
  {
    llvm::Instruction *I;                // The instruction
    std::vector<DependencyNode *> Deps;  // Dependencies (predecessor nodes)
    std::vector<DependencyNode *> Users; // Users (successor nodes)
    double EstimatedLatency;             // Estimated execution latency
    double CriticalPathLatency;          // Latency of the longest path to this node
    bool IsMemoryOp;                     // Whether this is a memory operation
    bool IsInCriticalPath;               // Whether this is in a critical path
    double MemoryLatencySensitivity;     // How sensitive to memory latency (0-1)

    DependencyNode(llvm::Instruction *Inst)
        : I(Inst),
          EstimatedLatency(0.0),
          CriticalPathLatency(0.0),
          IsMemoryOp(false),
          IsInCriticalPath(false),
          MemoryLatencySensitivity(0.0) {}
  };

  // Represents a critical dependency path through the code
  struct CriticalPath
  {
    std::vector<llvm::Instruction *> Instructions; // Instructions in the path
    double TotalLatency;                           // Total estimated latency
    unsigned MemoryDependencies;                   // Number of memory operations
    double MemoryDependencyRatio;                  // Ratio of memory ops to total ops
    double LatencySensitivityScore;                // Overall latency sensitivity

    CriticalPath()
        : TotalLatency(0.0),
          MemoryDependencies(0),
          MemoryDependencyRatio(0.0),
          LatencySensitivityScore(0.0) {}
  };

  // Aggregated dependency information for a memory allocation
  struct DependencyInfo
  {
    std::vector<CriticalPath> CriticalPaths;            // Critical paths involving this allocation
    double LatencySensitivityScore;                     // Overall latency sensitivity score
    double BandwidthSensitivityScore;                   // Overall bandwidth sensitivity score
    double CriticalPathLatency;                         // Maximum critical path latency
    bool IsLatencyBound;                                // Whether more latency sensitive than bandwidth
    double HBMBenefitScore;                             // Estimated benefit from HBM placement
    double LongestPathMemoryRatio;                      // Memory ops ratio in longest path
    std::vector<llvm::Instruction *> CriticalMemoryOps; // Memory ops in critical paths
    std::string Analysis;                               // Detailed analysis explanation

    DependencyInfo()
        : LatencySensitivityScore(0.0),
          BandwidthSensitivityScore(0.0),
          CriticalPathLatency(0.0),
          IsLatencyBound(false),
          HBMBenefitScore(0.0),
          LongestPathMemoryRatio(0.0) {}
  };

  // Represents the bank conflict pattern severity and type
  enum class BankConflictSeverity
  {
    NONE,     // No conflicts detected
    LOW,      // Occasional conflicts
    MODERATE, // Regular conflicts but manageable
    HIGH,     // Frequent conflicts with substantial impact
    SEVERE    // Critical conflicts, severe performance impact
  };

  // Specific types of bank conflicts
  enum class BankConflictType
  {
    NONE,               // No conflicts
    SAME_BANK_ACCESS,   // Multiple accesses to same bank
    STRIDED_CONFLICT,   // Regular access pattern causing conflicts
    RANDOM_CONFLICT,    // Unpredictable access pattern with conflicts
    PARTIAL_ROW_ACCESS, // Accessing parts of a row inefficiently
    CHANNEL_IMBALANCE   // Uneven distribution across channels
  };

  // Detailed bank conflict information
  struct BankConflictInfo
  {
    BankConflictSeverity severity = BankConflictSeverity::NONE;
    BankConflictType type = BankConflictType::NONE;
    unsigned affectedBanks = 0;          // Number of banks with conflicts
    unsigned totalAccessedBanks = 0;     // Total number of banks accessed
    double conflictRate = 0.0;           // Estimated percentage of conflicting accesses
    double performanceImpact = 0.0;      // Estimated slowdown factor (1.0 = no impact)
    bool hasBankingFunction = false;     // Whether address mapping function was determined
    double conflictScore = 0.0;          // Score for HBM suitability (negative = worse)
    std::vector<unsigned> bankHistogram; // Distribution of accesses across banks

    // For reporting
    std::string analysisDescription;

    BankConflictInfo()
    {
      // Initialize bank histogram with zeros for all banks
      bankHistogram.resize(32, 0); // Assuming 32 banks in HBM2
    }
  };

  // Parameters for different HBM configurations
  struct HBMConfiguration
  {
    unsigned numBanks;    // Number of banks
    unsigned numChannels; // Number of channels
    unsigned rowSize;     // Size of a row in bytes
    unsigned bankXORBits; // Bits used for XOR banking function
    unsigned channelBits; // Bits used for channel selection
    unsigned addressMask; // Address bits that matter for conflicts

    HBMConfiguration() : numBanks(32),
                         numChannels(8),
                         rowSize(1024),
                         bankXORBits(7),
                         channelBits(3),
                         addressMask(0x7FFF) {} // 15 bits for bank+row addressing

    // HBM2 configuration
    static HBMConfiguration HBM2()
    {
      HBMConfiguration cfg;
      cfg.numBanks = 32;
      cfg.numChannels = 8;
      cfg.rowSize = 1024;
      cfg.bankXORBits = 7;
      cfg.channelBits = 3;
      cfg.addressMask = 0x7FFF;
      return cfg;
    }

    // HBM3 configuration (adjust as specifications evolve)
    static HBMConfiguration HBM3()
    {
      HBMConfiguration cfg;
      cfg.numBanks = 64;
      cfg.numChannels = 16;
      cfg.rowSize = 1024;
      cfg.bankXORBits = 8;
      cfg.channelBits = 4;
      cfg.addressMask = 0xFFFF;
      return cfg;
    }
  };
} // namespace MyHBM

#endif // MYHBM_ANALYSIS_TYPES_H