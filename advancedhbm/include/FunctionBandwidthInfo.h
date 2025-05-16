// FunctionBandwidthInfo.h
#ifndef MYHBM_FUNCTION_BANDWIDTH_INFO_H
#define MYHBM_FUNCTION_BANDWIDTH_INFO_H

#include "llvm/IR/Function.h"
#include "llvm/Support/JSON.h"
#include <string>
#include <vector>

namespace MyHBM {

struct FunctionBandwidthInfo {
  llvm::Function* F;                 // The analyzed function
  std::string Name;                  // Function name
  std::string SourceLocation;        // Source location if available
  double BandwidthScore;             // Overall bandwidth sensitivity score
  bool IsStreamingFunction;          // Has streaming access patterns
  bool IsVectorized;                 // Uses vectorization
  bool IsParallel;                   // Has parallel execution
  unsigned LoopNestingDepth;         // Maximum loop nesting depth
  unsigned MemoryOperations;         // Number of memory operations
  unsigned AllocationsCount;         // Number of allocations
  double AvgAllocScore;              // Average allocation score
  double MaxAllocScore;              // Maximum allocation score
  bool HasBankConflicts;             // Has potential bank conflicts
  bool HasPoorTemporalLocality;      // Has poor temporal locality
  
  // Constructor
  FunctionBandwidthInfo() : 
    F(nullptr),
    BandwidthScore(0.0),
    IsStreamingFunction(false),
    IsVectorized(false),
    IsParallel(false),
    LoopNestingDepth(0),
    MemoryOperations(0),
    AllocationsCount(0),
    AvgAllocScore(0.0),
    MaxAllocScore(0.0),
    HasBankConflicts(false),
    HasPoorTemporalLocality(false) {}
    
  // Convert to JSON
  llvm::json::Object toJSON() const;
};

// Collection of function bandwidth information
struct ModuleBandwidthInfo {
  std::vector<FunctionBandwidthInfo> Functions;
  std::string ModuleName;
  
  // Convert to JSON
  llvm::json::Object toJSON() const;
};

} // namespace MyHBM

#endif // FUNCTION_BANDWIDTH_INFO_H