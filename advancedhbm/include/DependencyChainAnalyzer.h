#ifndef DEPENDENCYCHAINANALYZER_H
#define DEPENDENCYCHAINANALYZER_H

#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/ValueTracking.h"
#include <vector>
#include <map>
#include <set>
#include <queue>

namespace MyHBM
{

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

    class DependencyChainAnalyzer
    {
    private:
        llvm::ScalarEvolution &SE;
        llvm::LoopInfo &LI;
        llvm::Function &F;

        // Maps to track dependency information
        llvm::DenseMap<llvm::Instruction *, DependencyNode *> NodeMap;
        llvm::DenseMap<llvm::Value *, std::vector<llvm::Instruction *>> MemoryAccessMap;

        // Build dependency graph
        void buildDependencyGraph();

        // Estimate instruction latency
        double estimateInstructionLatency(llvm::Instruction *I);

        // Find all critical paths in the dependency graph
        std::vector<CriticalPath> findAllCriticalPaths();

        // Calculate critical path latencies
        void calculateCriticalPathLatencies();

        // Identify memory-dependent instructions
        bool isMemoryDependent(llvm::Instruction *I);

        // Check if there's a chain from Src to Dst
        bool hasDataDependencyChain(llvm::Instruction *Src, llvm::Instruction *Dst,
                                    std::set<llvm::Instruction *> &Visited);

        // Calculate memory latency sensitivity for each instruction
        void calculateMemoryLatencySensitivity();

        // Get all memory accesses for a specific pointer
        std::vector<llvm::Instruction *> getMemoryAccesses(llvm::Value *Ptr);

        // Trace value back to original pointer
        llvm::Value *traceToOriginalPointer(llvm::Value *V);

        // Check if instruction is a memory operation
        bool isMemoryOperation(llvm::Instruction *I);

        // Check if instruction is part of a critical path
        bool isInCriticalPath(llvm::Instruction *I, double threshold);

        // Clear all data structures
        void clear();

    public:
        DependencyChainAnalyzer(llvm::ScalarEvolution &SE, llvm::LoopInfo &LI, llvm::Function &F)
            : SE(SE), LI(LI), F(F) {}

        ~DependencyChainAnalyzer()
        {
            clear();
        }

        // Main analysis methods
        DependencyInfo analyzeDependencies(llvm::Value *Ptr);

        // Find all critical paths in the function
        std::vector<CriticalPath> findCriticalPaths();

        // Check if a memory access is on a critical path
        bool isOnCriticalPath(llvm::Instruction *I);

        // Calculate latency sensitivity score for a specific pointer
        double calculateLatencySensitivityScore(llvm::Value *Ptr);

        // Calculate bandwidth sensitivity score for a specific pointer
        double calculateBandwidthSensitivityScore(llvm::Value *Ptr);

        // Identify instructions that would benefit most from reduced memory latency
        std::vector<llvm::Instruction *> rankByLatencySensitivity();

        // Determine if a pointer is more latency or bandwidth sensitive
        bool isLatencySensitive(llvm::Value *Ptr);
    };

} // namespace MyHBM

#endif // DEPENDENCYCHAINANALYZER_H