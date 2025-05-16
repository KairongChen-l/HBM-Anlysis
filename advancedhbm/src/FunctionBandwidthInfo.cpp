// FunctionBandwidthInfo.cpp
#include "FunctionBandwidthInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"

using namespace llvm;
using namespace MyHBM;

json::Object FunctionBandwidthInfo::toJSON() const {
  json::Object Obj;
  
  // Basic information
  Obj["function_name"] = Name;
  Obj["source_location"] = SourceLocation;
  Obj["bandwidth_score"] = BandwidthScore;
  
  // Access patterns
  Obj["is_streaming"] = IsStreamingFunction;
  Obj["is_vectorized"] = IsVectorized;
  Obj["is_parallel"] = IsParallel;
  
  // Structure information
  Obj["loop_nesting_depth"] = LoopNestingDepth;
  Obj["memory_operations"] = MemoryOperations;
  
  // Allocation information
  Obj["allocations_count"] = AllocationsCount;
  Obj["avg_alloc_score"] = AvgAllocScore;
  Obj["max_alloc_score"] = MaxAllocScore;
  
  // Additional flags
  Obj["has_bank_conflicts"] = HasBankConflicts;
  Obj["has_poor_temporal_locality"] = HasPoorTemporalLocality;
  
  return Obj;
}

json::Object ModuleBandwidthInfo::toJSON() const {
  json::Object Obj;
  
  // Module information
  Obj["module_name"] = ModuleName;
  
  // Functions data
  json::Array FuncsArray;
  for (const auto& FuncInfo : Functions) {
    FuncsArray.push_back(FuncInfo.toJSON());
  }
  Obj["functions"] = std::move(FuncsArray);
  
  return Obj;
}