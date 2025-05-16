// FunctionBandwidthAnalyzer.h
#ifndef FUNCTION_BANDWIDTH_ANALYZER_H
#define FUNCTION_BANDWIDTH_ANALYZER_H

#include "FunctionBandwidthInfo.h"
#include "BandwidthAnalyzer.h"
#include "TemporalLocalityAnalyzer.h"
#include "VectorizationAnalyzer.h"
#include "ParallelismAnalyzer.h"
#include "StrideAnalyzer.h"
#include "LoopAnalyzer.h"
#include "llvm/IR/Function.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include <string>

namespace MyHBM {

class FunctionBandwidthAnalyzer {
private:
  llvm::LoopInfo &LI;
  llvm::ScalarEvolution &SE;
  llvm::AAResults &AA;
  llvm::MemorySSA &MSSA;
  
  // Count memory operations in a function
  unsigned countMemoryOperations(llvm::Function &F);
  
  // Analyze loop access patterns
  void analyzeLoopAccesses(llvm::Function &F, FunctionBandwidthInfo &Info);
  
  // Get source location for a function
  std::string getSourceLocation(llvm::Function &F);
  
  // Check for streaming access patterns
  bool hasStreamingAccesses(llvm::Function &F);
  
  // Calculate the bandwidth sensitivity score
  double calculateBandwidthScore(FunctionBandwidthInfo &Info);

public:
  FunctionBandwidthAnalyzer(
      llvm::LoopInfo &LI,
      llvm::ScalarEvolution &SE,
      llvm::AAResults &AA,
      llvm::MemorySSA &MSSA)
      : LI(LI), SE(SE), AA(AA), MSSA(MSSA) {}
      
  // Analyze a function and return its bandwidth information
  FunctionBandwidthInfo analyze(llvm::Function &F, 
                               const std::vector<MallocRecord> &Allocations);
                               
  // Analyze a whole module
  ModuleBandwidthInfo analyzeModule(llvm::Module &M, 
                                   const std::map<llvm::Function*, std::vector<MallocRecord>> &FunctionAllocations);
};

} // namespace MyHBM

#endif // FUNCTION_BANDWIDTH_ANALYZER_H