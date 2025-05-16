// FunctionBandwidthAnalyzer.cpp
#include "FunctionBandwidthAnalyzer.h"
#include "WeightConfig.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include <numeric>

using namespace llvm;
using namespace MyHBM;

FunctionBandwidthInfo FunctionBandwidthAnalyzer::analyze(
    Function &F, const std::vector<MallocRecord> &Allocations) {
  
  FunctionBandwidthInfo Info;
  Info.F = &F;
  Info.Name = F.getName().str();
  Info.SourceLocation = getSourceLocation(F);
  
  // Count memory operations
  Info.MemoryOperations = countMemoryOperations(F);
  
  // Analyze allocations
  Info.AllocationsCount = Allocations.size();
  if (!Allocations.empty()) {
    // Calculate average and max allocation scores
    double TotalScore = 0.0;
    double MaxScore = 0.0;
    
    for (const auto &Alloc : Allocations) {
      TotalScore += Alloc.Score;
      MaxScore = std::max(MaxScore, Alloc.Score);
      
      // Check flags from allocations
      if (Alloc.IsStreamAccess)
        Info.IsStreamingFunction = true;
      if (Alloc.IsVectorized)
        Info.IsVectorized = true;
      if (Alloc.IsParallel)
        Info.IsParallel = true;
      if (Alloc.BankConflictSeverity > 0)
        Info.HasBankConflicts = true;
      if (Alloc.TemporalLocalityData.level == TemporalLocalityLevel::POOR)
        Info.HasPoorTemporalLocality = true;
    }
    
    Info.AvgAllocScore = TotalScore / Allocations.size();
    Info.MaxAllocScore = MaxScore;
  }
  
  // Use analyzers to check for additional patterns
  VectorizationAnalyzer VecAnalyzer(SE);
  ParallelismAnalyzer ParAnalyzer;
  
  // Check for vectorization patterns in the whole function
  Info.IsVectorized |= VecAnalyzer.detectSIMDIntrinsics(F);
  
  // Check for parallelism in the whole function
  Info.IsParallel |= ParAnalyzer.detectParallelRuntime(F);
  
  // Check for streaming access patterns
  Info.IsStreamingFunction |= hasStreamingAccesses(F);
  
  // Analyze loop access patterns
  analyzeLoopAccesses(F, Info);
  
  // Calculate bandwidth sensitivity score
  Info.BandwidthScore = calculateBandwidthScore(Info);
  
  return Info;
}

ModuleBandwidthInfo FunctionBandwidthAnalyzer::analyzeModule(
    Module &M, const std::map<Function*, std::vector<MallocRecord>> &FunctionAllocations) {
  
  ModuleBandwidthInfo ModuleInfo;
  ModuleInfo.ModuleName = M.getModuleIdentifier();
  
  for (auto &F : M) {
    if (F.isDeclaration())
      continue;
      
    auto AllocIt = FunctionAllocations.find(&F);
    std::vector<MallocRecord> Allocations;
    if (AllocIt != FunctionAllocations.end()) {
      Allocations = AllocIt->second;
    }
    
    FunctionBandwidthInfo FuncInfo = analyze(F, Allocations);
    ModuleInfo.Functions.push_back(std::move(FuncInfo));
  }
  
  // Sort functions by bandwidth score (highest first)
  std::sort(ModuleInfo.Functions.begin(), ModuleInfo.Functions.end(),
           [](const FunctionBandwidthInfo &A, const FunctionBandwidthInfo &B) {
             return A.BandwidthScore > B.BandwidthScore;
           });
           
  return ModuleInfo;
}

unsigned FunctionBandwidthAnalyzer::countMemoryOperations(Function &F) {
  unsigned Count = 0;
  
  for (auto &BB : F) {
    for (auto &I : BB) {
      if (isa<LoadInst>(I) || isa<StoreInst>(I) || 
          isa<AtomicRMWInst>(I) || isa<AtomicCmpXchgInst>(I) ||
          isa<MemIntrinsic>(I)) {
        Count++;
      }
    }
  }
  
  return Count;
}

void FunctionBandwidthAnalyzer::analyzeLoopAccesses(Function &F, FunctionBandwidthInfo &Info) {
  unsigned MaxNestingDepth = 0;
  
  for (auto &BB : F) {
    Loop *L = LI.getLoopFor(&BB);
    if (!L)
      continue;
      
    // Calculate loop nesting depth
    unsigned Depth = 1;
    Loop *Parent = L->getParentLoop();
    while (Parent) {
      Depth++;
      Parent = Parent->getParentLoop();
    }
    
    MaxNestingDepth = std::max(MaxNestingDepth, Depth);
    
    // Analyze loops for bandwidth sensitivity
    LoopAnalyzer LoopAn(SE, LI);
    
    // Check for streaming access patterns in the loop
    for (auto &I : BB) {
      if (auto *LI = dyn_cast<LoadInst>(&I)) {
        Value *Ptr = LI->getPointerOperand();
        StrideAnalyzer StrideAn(SE);
        if (StrideAn.isStreamingAccess(Ptr, L, AA)) {
          Info.IsStreamingFunction = true;
        }
      }
      else if (auto *SI = dyn_cast<StoreInst>(&I)) {
        Value *Ptr = SI->getPointerOperand();
        StrideAnalyzer StrideAn(SE);
        if (StrideAn.isStreamingAccess(Ptr, L, AA)) {
          Info.IsStreamingFunction = true;
        }
      }
    }
  }
  
  Info.LoopNestingDepth = MaxNestingDepth;
}

std::string FunctionBandwidthAnalyzer::getSourceLocation(Function &F) {
  // Get the debug location of the first instruction
  for (auto &BB : F) {
    if (!BB.empty()) {
      if (DILocation *Loc = BB.front().getDebugLoc()) {
        unsigned Line = Loc->getLine();
        StringRef File = Loc->getFilename();
        StringRef Dir = Loc->getDirectory();
        
        std::string FullPath;
        if (!Dir.empty() && !File.empty() && File.front() != '/')
          FullPath = (Dir + "/" + File).str();
        else
          FullPath = File.str();
          
        if (!FullPath.empty())
          return FullPath + ":" + std::to_string(Line);
      }
    }
  }
  
  return F.getName().str() + ":<no_dbg>";
}

bool FunctionBandwidthAnalyzer::hasStreamingAccesses(Function &F) {
  for (auto &BB : F) {
    for (auto &I : BB) {
      if (auto *LI = dyn_cast<LoadInst>(&I)) {
        Value *Ptr = LI->getPointerOperand();
        Loop *L = this->LI.getLoopFor(&BB);
        if (L && SE.isSCEVable(Ptr->getType())) {
          StrideAnalyzer StrideAn(SE);
          if (StrideAn.isStreamingAccess(Ptr, L, AA)) {
            return true;
          }
        }
      }
      else if (auto *SI = dyn_cast<StoreInst>(&I)) {
        Value *Ptr = SI->getPointerOperand();
        Loop *L = this->LI.getLoopFor(&BB);
        if (L && SE.isSCEVable(Ptr->getType())) {
          StrideAnalyzer StrideAn(SE);
          if (StrideAn.isStreamingAccess(Ptr, L, AA)) {
            return true;
          }
        }
      }
    }
  }
  
  return false;
}

double FunctionBandwidthAnalyzer::calculateBandwidthScore(FunctionBandwidthInfo &Info) {
  using namespace WeightConfig;
  
  double Score = 0.0;
  
  // Base score from memory operations count
  Score += std::min(20.0, Info.MemoryOperations / 10.0);
  
  // Add allocation-based score
  if (Info.AllocationsCount > 0) {
    Score += Info.AvgAllocScore * 0.2; // 20% of average allocation score
    Score += Info.MaxAllocScore * 0.3; // 30% of max allocation score
  }
  
  // Add bonus for streaming access patterns
  if (Info.IsStreamingFunction) {
    Score += StreamBonus * 0.5;
  }
  
  // Add bonus for vectorization
  if (Info.IsVectorized) {
    Score += VectorBonus * 1.5;
  }
  
  // Add bonus for parallelism
  if (Info.IsParallel) {
    Score += ParallelBonus * 1.5;
  }
  
  // Add bonus for deep loop nesting
  if (Info.LoopNestingDepth > 0) {
    Score += Info.LoopNestingDepth * 5.0;
  }
  
  // Penalty for bank conflicts
  if (Info.HasBankConflicts) {
    Score -= 10.0;
  }
  
  // Bonus for poor temporal locality (less benefit from cache)
  if (Info.HasPoorTemporalLocality) {
    Score += 15.0;
  }
  
  return Score;
}