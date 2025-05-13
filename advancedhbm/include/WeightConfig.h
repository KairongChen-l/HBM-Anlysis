#ifndef WEIGHT_CONFIG_H
#define WEIGHT_CONFIG_H

#include "llvm/Support/CommandLine.h"

namespace MyHBM
{
    namespace WeightConfig
    {

        //===----------------------------------------------------------------------===//
        // Command-line configurable weights
        //===----------------------------------------------------------------------===//

        // Base weights for access types
        extern llvm::cl::opt<double> AccessBaseRead;
        extern llvm::cl::opt<double> AccessBaseWrite;

        // Feature bonus weights
        extern llvm::cl::opt<double> StreamBonus;
        extern llvm::cl::opt<double> VectorBonus;
        extern llvm::cl::opt<double> ParallelBonus;

        // Bandwidth scaling factor
        extern llvm::cl::opt<double> BandwidthScale;

        //===----------------------------------------------------------------------===//
        // Data locality weights
        //===----------------------------------------------------------------------===//

        // Weights for different locality types
        const double LocalityExcellentBonus = 0.2; // Excellent locality (least benefit from HBM)
        const double LocalityGoodBonus = 0.5;      // Good locality
        const double LocalityModerateBonus = 0.8;  // Moderate locality
        const double LocalityPoorBonus = 1.2;      // Poor locality (most benefit from HBM)

        //===----------------------------------------------------------------------===//
        // Loop nesting weights
        //===----------------------------------------------------------------------===//

        // Weight for nested loop analysis
        const double NestedLoopWeight = 0.3;

        //===----------------------------------------------------------------------===//
        // Memory parallelism weights
        //===----------------------------------------------------------------------===//

        // Thread access pattern weights
        const double ThreadPartitionedWeight = 1.0; // Partitioned access (good)
        const double SharedReadOnlyWeight = 0.7;    // Read-only shared data
        const double AtomicAccessWeight = 0.3;      // Atomic operations
        const double FalseSharingPenalty = 0.8;     // False sharing (bad)
        const double SharedWritePenalty = 0.5;      // Shared write conflicts

        // Parallel framework detection weights
        const double OpenMPWeight = 0.3; // OpenMP parallelism
        const double CUDAWeight = 0.6;   // CUDA parallelism (higher core count)
        const double TBBWeight = 0.4;    // TBB task-stealing

        //===----------------------------------------------------------------------===//
        // Vectorization weights
        //===----------------------------------------------------------------------===//

        // Vector width scaling factors
        const double VectorWidth8Plus = 1.5; // 512-bit vectors (AVX-512)
        const double VectorWidth4Plus = 1.2; // 256-bit vectors (AVX)

        // Vector pattern detection weight
        const double VectorLoopPatternBonus = 1.2;

        //===----------------------------------------------------------------------===//
        // Streaming weights
        //===----------------------------------------------------------------------===//

        // Stride pattern weights
        const double StrideConstantBonus = 1.2;  // Constant stride (optimal)
        const double StrideLinearBonus = 1.0;    // Linear stride (good)
        const double StrideComplexBonus = 0.8;   // Complex but regular stride
        const double StrideIrregularBonus = 0.5; // Irregular but some streaming

        // Inner loop bonus factor
        const double InnerLoopBonus = 0.25;

        //===----------------------------------------------------------------------===//
        // Penalty weights
        //===----------------------------------------------------------------------===//

        // Memory SSA complexity penalties
        const double PhiNodePenaltyFactor = 0.5;
        const double FanOutPenaltyFactor = 0.2;
        const double MaxSSAPenalty = 5.0;

        // Address calculation chaos penalties
        const double GEPCountPenalty = 0.2;        // Per GEP beyond 5
        const double IndirectIndexPenalty = 0.5;   // Per indirect load for index
        const double NonAffineAccessPenalty = 0.3; // Per non-affine access
        const double BitcastCountPenalty = 0.2;    // Per bitcast beyond 3
        const double TypeDiversityPenalty = 0.3;   // Per unique type beyond 3
        const double MaxChaosPenalty = 5.0;

        //===----------------------------------------------------------------------===//
        // Contention weights
        //===----------------------------------------------------------------------===//

        // Different contention type weights
        const double BandwidthContentionBonus = 25.0;      // Bandwidth contention is good for HBM
        const double FalseSharingContentionPenalty = 10.0; // False sharing penalty
        const double AtomicContentionPenalty = 15.0;       // Atomic contention penalty
        const double LockContentionPenalty = 20.0;         // Lock contention penalty

        //===----------------------------------------------------------------------===//
        // Data flow weights
        //===----------------------------------------------------------------------===//

        // Phase weights
        const double InitPhaseBonus = 10.0;
        const double ReadOnlyPhaseBonus = 15.0;
        const double NoDormantPhaseBonus = 5.0;
        const double UsageDensityFactor = 2.0;

        //===----------------------------------------------------------------------===//
        // Temporal locality weights
        //===----------------------------------------------------------------------===//

        // HBM benefit adjustments based on temporal locality
        const double PoorTemporalLocalityBonus = 20.0;        // Poor locality benefits from HBM
        const double ExcellentTemporalLocalityPenalty = 15.0; // Excellent locality reduces HBM benefit
        const double ShortReuseDistancePenalty = 10.0;        // Very short reuse distances (benefit from CPU cache)
        const double LongReuseDistanceBonus = 10.0;           // Long reuse distances (benefit from HBM)

        //===----------------------------------------------------------------------===//
        // Bank conflict weights
        //===----------------------------------------------------------------------===//

        // Bank conflict severity impacts
        const double NoConflictEffect = 0.0;
        const double LowConflictEffect = -5.0;
        const double ModerateConflictEffect = -15.0;
        const double HighConflictEffect = -30.0;
        const double SevereConflictEffect = -50.0;

        // Bank conflict type penalties
        const double SameBankAccessPenalty = 20.0;
        const double StridedConflictPenalty = 15.0;
        const double RandomConflictPenalty = 10.0;
        const double PartialRowAccessPenalty = 5.0;
        const double ChannelImbalancePenalty = 8.0;

        //===----------------------------------------------------------------------===//
        // Dependency chain weights
        //===----------------------------------------------------------------------===//

        // Latency vs bandwidth sensitivity weights
        const double BandwidthSensitivityWeight = 20.0;
        const double LatencySensitivityWithHighMemRatio = 10.0;
        const double LatencySensitivityWithLowMemRatio = 5.0;

        //===----------------------------------------------------------------------===//
        // Cross-function weights
        //===----------------------------------------------------------------------===//

        // Cross-function score adjustments
        const double LocalOnlyScore = 5.0;
        const double ExternalPropagationScore = 2.0;
        const double HotFunctionScore = 15.0;
        const double OtherInternalFunctionBaseScore = 8.0;
        const double PerCalledFunctionScore = 1.5;

        //===----------------------------------------------------------------------===//
        // Initialization function
        //===----------------------------------------------------------------------===//

        // Initialize any dynamic weight configurations
        void initializeWeights();

    } // namespace WeightConfig
} // namespace MyHBM

#endif // WEIGHT_CONFIG_H