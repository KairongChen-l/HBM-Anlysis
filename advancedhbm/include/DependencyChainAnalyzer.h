#ifndef MYHBM_DEPENDENCY_CHAINAN_ALYZER_H
#define MYHBM_DEPENDENCY_CHAINAN_ALYZER_H

#include "AnalysisTypes.h"

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