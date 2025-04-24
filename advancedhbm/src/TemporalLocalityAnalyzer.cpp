#include "TemporalLocalityAnalyzer.h"
#include "LoopUtils.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include <queue>
#include <algorithm>
#include <cmath>

using namespace llvm;
using namespace MyHBM;

// Main analysis method
TemporalLocalityInfo TemporalLocalityAnalyzer::analyzeTemporalLocality(Value *Ptr, Function &F) {
  errs() << "===== Function:analyzeTemporalLocality =====\n";
  TemporalLocalityInfo Result;
  
  if (!Ptr) return Result;
  
  // Find all memory accesses to this pointer
  std::vector<Instruction*> Accesses = findAllMemoryAccesses(Ptr, F);
  
  // If few or no accesses, likely poor temporal locality
  if (Accesses.size() <= 1) {
    Result.level = TemporalLocalityLevel::POOR;
    Result.temporalLocalityScore = 10.0; // Low score for single access
    return Result;
  }
  
  // Identify reuse patterns
  auto ReusePairs = identifyReusePatterns(Accesses);
  
  // Calculate statistics
  unsigned TotalDist = 0;
  unsigned MinDist = UINT_MAX;
  unsigned MaxDist = 0;
  unsigned ReuseCount = 0;
  
  for (auto &Pair : ReusePairs) {
    Instruction *First = Pair.first;
    Instruction *Second = Pair.second;
    
    unsigned Dist = calculateInstructionDistance(First, Second);
    TotalDist += Dist;
    MinDist = std::min(MinDist, Dist);
    MaxDist = std::max(MaxDist, Dist);
    ReuseCount++;
  }
  
  // Calculate average reuse distance
  unsigned AvgDist = ReuseCount > 0 ? TotalDist / ReuseCount : UINT_MAX;
  Result.estimatedReuseDistance = AvgDist;
  
  // Check if accesses are within loops
  bool HasLoopCarriedReuse = false;
  bool MostAccessesInLoop = false;
  Loop *CommonLoop = nullptr;
  
  // Find common loop containing most accesses
  std::map<Loop*, unsigned> LoopAccessCount;
  for (auto *I : Accesses) {
    Loop *L = LI.getLoopFor(I->getParent());
    while (L) {
      LoopAccessCount[L]++;
      L = L->getParentLoop();
    }
  }
  
  unsigned MaxAccessCount = 0;
  for (auto &Entry : LoopAccessCount) {
    if (Entry.second > MaxAccessCount) {
      MaxAccessCount = Entry.second;
      CommonLoop = Entry.first;
    }
  }
  
  MostAccessesInLoop = MaxAccessCount > Accesses.size() / 2;
  
  // If most accesses are in a loop, check for loop-carried reuse
  if (MostAccessesInLoop && CommonLoop) {
    HasLoopCarriedReuse = detectLoopCarriedReuse(Ptr, CommonLoop);
    
    // If in loop, reuse distance might be affected by loop iterations
    if (HasLoopCarriedReuse) {
      uint64_t TripCount = LoopUtils::getLoopTripCount(CommonLoop, SE);
      if (TripCount > 1) {
        // Adjust reuse distance for loop iterations
        Result.estimatedReuseDistance = std::min(Result.estimatedReuseDistance, 
                                               static_cast<unsigned>(TripCount));
      }
    }
  }
  
  // Determine temporal locality level based on statistics
  if (MinDist <= 3 && AvgDist <= 10) {
    Result.level = TemporalLocalityLevel::EXCELLENT;
  } else if (MinDist <= 10 && AvgDist <= 50) {
    Result.level = TemporalLocalityLevel::GOOD;
  } else if (MinDist <= 50 && AvgDist <= 200) {
    Result.level = TemporalLocalityLevel::MODERATE;
  } else {
    Result.level = TemporalLocalityLevel::POOR;
  }
  
  // Check for loop invariant accesses
  if (CommonLoop && SE.isSCEVable(Ptr->getType())) {
    const SCEV *PtrSCEV = SE.getSCEV(Ptr);
    Result.isLoopInvariant = SE.isLoopInvariant(PtrSCEV, CommonLoop);
  }
  
  // Calculate reuse frequency (approximate)
  if (ReuseCount > 0 && Accesses.size() > 0) {
    Result.reuseFrequency = static_cast<double>(ReuseCount) / Accesses.size();
  }
  
  // Calculate final temporal locality score
  Result.temporalLocalityScore = scoreTemporalLocality(Result.level, 
                                                      Result.estimatedReuseDistance, 
                                                      MostAccessesInLoop);
  
  // Adjust score based on keepaliveness
  double KeepaliveFactor = analyzeDataKeepaliveness(Ptr, F);
  Result.temporalLocalityScore *= KeepaliveFactor;
  
  return Result;
}

// Find all memory accesses to a pointer
std::vector<Instruction*> TemporalLocalityAnalyzer::findAllMemoryAccesses(Value *Ptr, Function &F) {
  errs() << "===== Function:findAllMemoryAccesses =====\n";
  std::vector<Instruction*> Accesses;
  std::set<Value*> VisitedPtrs;
  std::queue<Value*> WorkList;
  
  WorkList.push(Ptr);
  VisitedPtrs.insert(Ptr);
  
  // First, collect all derived pointers
  while (!WorkList.empty()) {
    Value *CurPtr = WorkList.front();
    WorkList.pop();
    
    for (User *U : CurPtr->users()) {
      if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
        if (VisitedPtrs.insert(GEP).second) {
          WorkList.push(GEP);
        }
      } else if (auto *BC = dyn_cast<BitCastInst>(U)) {
        if (VisitedPtrs.insert(BC).second) {
          WorkList.push(BC);
        }
      }
    }
  }
  
  // Then collect all memory accesses to those pointers
  for (Value *DerivedPtr : VisitedPtrs) {
    for (User *U : DerivedPtr->users()) {
      if (auto *LI = dyn_cast<LoadInst>(U)) {
        Accesses.push_back(LI);
      } else if (auto *SI = dyn_cast<StoreInst>(U)) {
        if (SI->getPointerOperand() == DerivedPtr) {
          Accesses.push_back(SI);
        }
      } else if (auto *Call = dyn_cast<CallInst>(U)) {
        // Memory intrinsics and other calls that may access memory
        if (Call->mayReadOrWriteMemory()) {
          Accesses.push_back(Call);
        }
      }
    }
  }
  
  // Sort accesses by program order
  std::sort(Accesses.begin(), Accesses.end(), 
    [&](Instruction *A, Instruction *B) {
      // If in same BB, sort by instruction order
      if (A->getParent() == B->getParent()) {
        // Traverse the BB to determine order
        for (auto &I : *A->getParent()) {
          if (&I == A) return true;
          if (&I == B) return false;
        }
      }
      
      // Otherwise, use dominator relationship if available
      DominatorTree DT(F);
      if (DT.dominates(A->getParent(), B->getParent())) return true;
      if (DT.dominates(B->getParent(), A->getParent())) return false;
      
      // As a fallback, use block name lexicographic ordering
      return A->getParent()->getName() < B->getParent()->getName();
    });
  
  return Accesses;
}

// Identify reuse patterns between accesses
SmallVector<std::pair<Instruction*, Instruction*>, 8> 
TemporalLocalityAnalyzer::identifyReusePatterns(const std::vector<Instruction*> &Accesses) {
  errs() << "===== Function:identifyReusePatterns =====\n";
  SmallVector<std::pair<Instruction*, Instruction*>, 8> ReusePairs;
  
  // Simple sequential reuse detection
  for (size_t i = 0; i < Accesses.size() - 1; ++i) {
    for (size_t j = i + 1; j < Accesses.size(); ++j) {
      Instruction *First = Accesses[i];
      Instruction *Second = Accesses[j];
      
      // Check if they access same memory location
      bool SameLocation = false;
      
      // Two loads from the same location
      if (isa<LoadInst>(First) && isa<LoadInst>(Second)) {
        LoadInst *Load1 = cast<LoadInst>(First);
        LoadInst *Load2 = cast<LoadInst>(Second);
        if (Load1->getPointerOperand() == Load2->getPointerOperand()) {
          SameLocation = true;
        }
      }
      // Store followed by load from same location
      else if (isa<StoreInst>(First) && isa<LoadInst>(Second)) {
        StoreInst *Store = cast<StoreInst>(First);
        LoadInst *Load = cast<LoadInst>(Second);
        if (Store->getPointerOperand() == Load->getPointerOperand()) {
          SameLocation = true;
        }
      }
      // Load followed by store to same location
      else if (isa<LoadInst>(First) && isa<StoreInst>(Second)) {
        LoadInst *Load = cast<LoadInst>(First);
        StoreInst *Store = cast<StoreInst>(Second);
        if (Load->getPointerOperand() == Store->getPointerOperand()) {
          SameLocation = true;
        }
      }
      // Two stores to the same location
      else if (isa<StoreInst>(First) && isa<StoreInst>(Second)) {
        StoreInst *Store1 = cast<StoreInst>(First);
        StoreInst *Store2 = cast<StoreInst>(Second);
        if (Store1->getPointerOperand() == Store2->getPointerOperand()) {
          SameLocation = true;
        }
      }
      
      if (SameLocation) {
        ReusePairs.push_back(std::make_pair(First, Second));
        // Only consider the closest reuse for simple analysis
        break;
      }
    }
  }
  
  return ReusePairs;
}

// Calculate distance between two instructions
unsigned TemporalLocalityAnalyzer::calculateInstructionDistance(Instruction *I1, Instruction *I2) {
  errs() << "===== Function:calculateInstructionDistance =====\n";
  if (!I1 || !I2) return UINT_MAX;
  
  // If in same basic block, count instructions between them
  if (I1->getParent() == I2->getParent()) {
    unsigned Distance = 0;
    bool Started = false;
    bool Finished = false;
    
    for (auto &I : *I1->getParent()) {
      if (&I == I1) {
        Started = true;
        continue;
      }
      
      if (Started && !Finished) {
        Distance++;
      }
      
      if (&I == I2) {
        Finished = true;
        break;
      }
    }
    
    if (Finished) return Distance;
  }
  
  // If in different BBs, use simple BFS to find shortest path
  std::queue<std::pair<BasicBlock*, unsigned>> Queue;
  std::set<BasicBlock*> Visited;
  
  Queue.push(std::make_pair(I1->getParent(), 0));
  Visited.insert(I1->getParent());
  
  while (!Queue.empty()) {
    BasicBlock *BB = Queue.front().first;
    unsigned Distance = Queue.front().second;
    Queue.pop();
    
    if (BB == I2->getParent()) {
      // Found path between BBs, add instruction-level distance
      unsigned IntraBBDist = 0;
      
      // Count remaining instructions in I1's BB
      bool FoundI1 = false;
      for (auto &I : *I1->getParent()) {
        if (&I == I1) {
          FoundI1 = true;
          continue;
        }
        if (FoundI1) IntraBBDist++;
      }
      
      // Count instructions before I2 in its BB
      unsigned I2Dist = 0;
      for (auto &I : *I2->getParent()) {
        if (&I == I2) break;
        I2Dist++;
      }
      
      // Add all distances
      return Distance * 10 + IntraBBDist + I2Dist; // Weight BB transitions higher
    }
    
    // Add successors to queue
    for (auto *Succ : successors(BB)) {
      if (Visited.insert(Succ).second) {
        Queue.push(std::make_pair(Succ, Distance + 1));
      }
    }
  }
  
  // Could not find path between instructions
  return UINT_MAX;
}

// Check if there's loop-carried reuse
bool TemporalLocalityAnalyzer::detectLoopCarriedReuse(Value *Ptr, Loop *L) {
  errs() << "===== Function:detectLoopCarriedReuse =====\n";
  if (!Ptr || !L || !SE.isSCEVable(Ptr->getType())) return false;
  
  // Check if this is an array/vector access with loop-dependent index
  if (auto *GEP = dyn_cast<GetElementPtrInst>(Ptr)) {
    for (auto I = GEP->idx_begin(), E = GEP->idx_end(); I != E; ++I) {
      Value *Idx = *I;
      
      // Skip constant indices
      if (isa<ConstantInt>(Idx)) continue;
      
      if (SE.isSCEVable(Idx->getType())) {
        const SCEV *IdxSCEV = SE.getSCEV(Idx);
        
        // Check if index depends on loop induction variable
        if (auto *AR = dyn_cast<SCEVAddRecExpr>(IdxSCEV)) {
          if (AR->getLoop() == L) {
            // This is loop-carried access pattern
            return true;
          }
        }
      }
    }
  }
  
  // Check for reuse between iterations
  SmallVector<Instruction*, 8> LoopAccesses;
  for (auto *BB : L->getBlocks()) {
    for (auto &I : *BB) {
      if (auto *LI = dyn_cast<LoadInst>(&I)) {
        if (LI->getPointerOperand() == Ptr) {
          LoopAccesses.push_back(LI);
        }
      } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
        if (SI->getPointerOperand() == Ptr) {
          LoopAccesses.push_back(SI);
        }
      }
    }
  }
  
  // If multiple accesses in loop, likely has reuse
  if (LoopAccesses.size() > 1) {
    return true;
  }
  
  return false;
}

// Analyze reuse pattern in a loop
TemporalLocalityLevel TemporalLocalityAnalyzer::analyzeReusePattern(Value *Ptr, Loop *L) {
  errs() << "===== Function:analyzeReusePattern =====\n";
  if (!Ptr || !L) return TemporalLocalityLevel::UNKNOWN;
  
  // Check if pointer is loop invariant
  bool IsLoopInvariant = false;
  if (SE.isSCEVable(Ptr->getType())) {
    const SCEV *PtrSCEV = SE.getSCEV(Ptr);
    IsLoopInvariant = SE.isLoopInvariant(PtrSCEV, L);
  }
  
  // Count accesses in each category
  unsigned LoadCount = 0;
  unsigned StoreCount = 0;
  unsigned UniqueAddressCount = 0;
  
  std::set<Value*> UniqueAddresses;
  
  for (auto *BB : L->getBlocks()) {
    for (auto &I : *BB) {
      if (auto *LI = dyn_cast<LoadInst>(&I)) {
        Value *AccessPtr = LI->getPointerOperand();
        if (AccessPtr == Ptr || AccessPtr->stripPointerCasts() == Ptr) {
          LoadCount++;
          UniqueAddresses.insert(AccessPtr);
        }
      } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
        Value *AccessPtr = SI->getPointerOperand();
        if (AccessPtr == Ptr || AccessPtr->stripPointerCasts() == Ptr) {
          StoreCount++;
          UniqueAddresses.insert(AccessPtr);
        }
      }
    }
  }
  
  UniqueAddressCount = UniqueAddresses.size();
  
  // Infer temporal locality level
  if (IsLoopInvariant && LoadCount > 0 && StoreCount == 0) {
    // Read-only loop invariant data has excellent locality (e.g., constants)
    return TemporalLocalityLevel::EXCELLENT;
  } else if (LoadCount + StoreCount > 3 * UniqueAddressCount) {
    // High access count relative to unique addresses suggests good reuse
    return TemporalLocalityLevel::GOOD;
  } else if (LoadCount + StoreCount > UniqueAddressCount) {
    // Moderate reuse
    return TemporalLocalityLevel::MODERATE;
  } else {
    // Few reuses or unique addresses close to access count
    return TemporalLocalityLevel::POOR;
  }
}

// Estimate reuse distance in a loop
unsigned TemporalLocalityAnalyzer::estimateReuseDistance(Value *Ptr, Loop *L) {
  errs() << "===== Function:estimateReuseDistance =====\n";
  if (!Ptr || !L) return UINT_MAX;
  
  // Collect all accesses to this pointer in the loop
  std::vector<Instruction*> Accesses;
  
  for (auto *BB : L->getBlocks()) {
    for (auto &I : *BB) {
      if (auto *LI = dyn_cast<LoadInst>(&I)) {
        if (LI->getPointerOperand() == Ptr || 
            LI->getPointerOperand()->stripPointerCasts() == Ptr) {
          Accesses.push_back(LI);
        }
      } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
        if (SI->getPointerOperand() == Ptr || 
            SI->getPointerOperand()->stripPointerCasts() == Ptr) {
          Accesses.push_back(SI);
        }
      }
    }
  }
  
  if (Accesses.size() <= 1) {
    // Not enough accesses to establish reuse
    return UINT_MAX;
  }
  
  // Calculate distances between consecutive accesses
  unsigned TotalDist = 0;
  unsigned MinDist = UINT_MAX;
  
  for (size_t i = 0; i < Accesses.size() - 1; ++i) {
    unsigned Dist = calculateInstructionDistance(Accesses[i], Accesses[i+1]);
    if (Dist != UINT_MAX) {
      TotalDist += Dist;
      MinDist = std::min(MinDist, Dist);
    }
  }
  
  // Also check loop-carried reuse by measuring distance from last to first access
  if (Accesses.size() >= 2) {
    // For loop-carried reuse, factor in trip count
    uint64_t TripCount = LoopUtils::getLoopTripCount(L, SE);
    if (TripCount != 0 && TripCount != (uint64_t)-1) {
      // Reuse might happen across iterations
      return MinDist + 1; // Simplified estimate
    }
  }
  
  // Return average distance or min distance if we have measurements
  if (Accesses.size() > 1) {
    return TotalDist / (Accesses.size() - 1);
  }
  
  return UINT_MAX;
}

// Score temporal locality for HBM suitability
double TemporalLocalityAnalyzer::scoreTemporalLocality(TemporalLocalityLevel level, 
                                                    unsigned reuseDistance, 
                                                    bool isInLoop) {
  errs() << "===== Function:scoreTemporalLocality =====\n";
  double baseScore = 0.0;
  
  // Base score based on locality level
  switch (level) {
    case TemporalLocalityLevel::EXCELLENT:
      baseScore = 90.0;
      break;
    case TemporalLocalityLevel::GOOD:
      baseScore = 70.0;
      break;
    case TemporalLocalityLevel::MODERATE:
      baseScore = 50.0;
      break;
    case TemporalLocalityLevel::POOR:
      baseScore = 20.0;
      break;
    default:
      baseScore = 10.0;
      break;
  }
  
  // Adjust for reuse distance
  if (reuseDistance != UINT_MAX) {
    if (reuseDistance <= 5) {
      baseScore *= 1.2; // Very close reuse, bonus
    } else if (reuseDistance <= 20) {
      baseScore *= 1.1; // Reasonably close reuse
    } else if (reuseDistance >= 200) {
      baseScore *= 0.8; // Distant reuse, penalty
    }
  }
  
  // Adjust for being in a loop
  if (isInLoop) {
    baseScore *= 1.1; // Loop context often means repeated reuse
  }
  
  // Normalize to 0-100 range
  return std::max(0.0, std::min(100.0, baseScore));
}

// Analyze data "keepaliveness" - how long data needs to stay in memory
double TemporalLocalityAnalyzer::analyzeDataKeepaliveness(Value *Ptr, Function &F) {
  errs() << "===== Function:analyzeDataKeepaliveness =====\n";
  if (!Ptr) return 1.0;
  
  // Find allocation instruction if applicable
  Instruction *AllocInst = nullptr;
  if (auto *Alloca = dyn_cast<AllocaInst>(Ptr)) {
    AllocInst = Alloca;
  } else if (auto *Call = dyn_cast<CallInst>(Ptr)) {
    Function *Callee = Call->getCalledFunction();
    if (Callee && (Callee->getName() == "malloc" || 
                  Callee->getName().starts_with("_Znw"))) { // C++ new
      AllocInst = Call;
    }
  }
  
  if (!AllocInst) return 1.0; // Can't determine allocation point
  
  // Find deallocation (free/delete) if applicable
  Instruction *DeallocInst = nullptr;
  
  for (User *U : Ptr->users()) {
    if (auto *Call = dyn_cast<CallInst>(U)) {
      Function *Callee = Call->getCalledFunction();
      if (Callee && (Callee->getName() == "free" || 
                    Callee->getName().starts_with("_ZdlPv"))) { // C++ delete
        DeallocInst = Call;
        break;
      }
    }
  }
  
  // If we found both allocation and deallocation, estimate liveness span
  if (AllocInst && DeallocInst) {
    // Simplified: just use instruction distance
    unsigned LivenessDistance = calculateInstructionDistance(AllocInst, DeallocInst);
    
    if (LivenessDistance != UINT_MAX) {
      // Normalize to factor (longer lifespan = higher factor)
      double factor = 1.0 + 0.2 * std::log10(static_cast<double>(LivenessDistance) + 1.0);
      return std::min(factor, 2.0); // Cap at 2.0x
    }
  }
  
  // Default factor for unknown lifespan
  return 1.0;
}

// Calculate overall temporal locality score 
double TemporalLocalityAnalyzer::calculateTemporalLocalityScore(Value *Ptr, Function &F) {
  errs() << "===== Function:calculateTemporalLocalityScore =====\n";
  // Perform full analysis
  TemporalLocalityInfo Info = analyzeTemporalLocality(Ptr, F);
  
  // Return calculated score
  return Info.temporalLocalityScore;
}

// Analyze load-store pair
// Analyze load-store pair
TemporalLocalityInfo TemporalLocalityAnalyzer::analyzeLoadStore(LoadInst *Load, StoreInst *Store) {
    errs() << "===== Function:analyzeLoadStore =====\n";
    TemporalLocalityInfo Result;
    
    if (!Load || !Store) return Result;
    
    // Check if this is a read-modify-write pattern
    Value *LoadPtr = Load->getPointerOperand();
    Value *StorePtr = Store->getPointerOperand();
    
    if (LoadPtr == StorePtr || 
        LoadPtr->stripPointerCasts() == StorePtr->stripPointerCasts()) {
      // Same memory location
      Value *StoredVal = Store->getValueOperand();
      
      // Check if stored value depends on loaded value
      bool DependsOnLoad = false;
      if (auto *BinOp = dyn_cast<BinaryOperator>(StoredVal)) {
        for (unsigned i = 0; i < BinOp->getNumOperands(); ++i) {
          if (BinOp->getOperand(i) == Load) {
            DependsOnLoad = true;
            break;
          }
        }
      }
      
      unsigned Distance = calculateInstructionDistance(Load, Store);
      
      if (DependsOnLoad && Distance < 10) {
        // Read-modify-write pattern with close proximity
        Result.level = TemporalLocalityLevel::EXCELLENT;
        Result.estimatedReuseDistance = Distance;
        Result.reuseFrequency = 1.0;
        Result.temporalLocalityScore = 95.0;
      } else if (Distance < 20) {
        // Close proximity but not direct dependency
        Result.level = TemporalLocalityLevel::GOOD;
        Result.estimatedReuseDistance = Distance;
        Result.reuseFrequency = 0.8;
        Result.temporalLocalityScore = 75.0;
      } else {
        // Further apart
        Result.level = TemporalLocalityLevel::MODERATE;
        Result.estimatedReuseDistance = Distance;
        Result.reuseFrequency = 0.5;
        Result.temporalLocalityScore = 50.0;
      }
    } else {
      // Different memory locations
      Result.level = TemporalLocalityLevel::POOR;
      Result.estimatedReuseDistance = UINT_MAX;
      Result.temporalLocalityScore = 10.0;
    }
    
    return Result;
  }
  
  // Analyze load-load pair
  TemporalLocalityInfo TemporalLocalityAnalyzer::analyzeLoadLoad(LoadInst *Load1, LoadInst *Load2) {
    errs() << "===== Function:analyzeLoadLoad =====\n";
    TemporalLocalityInfo Result;
    
    if (!Load1 || !Load2) return Result;
    
    Value *Ptr1 = Load1->getPointerOperand();
    Value *Ptr2 = Load2->getPointerOperand();
    
    if (Ptr1 == Ptr2 || 
        Ptr1->stripPointerCasts() == Ptr2->stripPointerCasts()) {
      // Same memory location
      unsigned Distance = calculateInstructionDistance(Load1, Load2);
      
      // Check if they're in same loop
      Loop *L1 = LI.getLoopFor(Load1->getParent());
      Loop *L2 = LI.getLoopFor(Load2->getParent());
      bool InSameLoop = (L1 && L2 && (L1 == L2 || L1->contains(L2) || L2->contains(L1)));
      
      if (Distance < 10 && InSameLoop) {
        // Close reuse within same loop
        Result.level = TemporalLocalityLevel::EXCELLENT;
        Result.estimatedReuseDistance = Distance;
        Result.reuseFrequency = 1.0;
        Result.temporalLocalityScore = 90.0;
      } else if (Distance < 50) {
        // Medium distance reuse
        Result.level = TemporalLocalityLevel::GOOD;
        Result.estimatedReuseDistance = Distance;
        Result.reuseFrequency = 0.7;
        Result.temporalLocalityScore = 70.0;
      } else {
        // Further reuse
        Result.level = TemporalLocalityLevel::MODERATE;
        Result.estimatedReuseDistance = Distance;
        Result.reuseFrequency = 0.4;
        Result.temporalLocalityScore = 40.0;
      }
    } else {
      // Different memory locations
      Result.level = TemporalLocalityLevel::POOR;
      Result.estimatedReuseDistance = UINT_MAX;
      Result.temporalLocalityScore = 10.0;
    }
    
    return Result;
  }
  
  // Analyze store-store pair
  TemporalLocalityInfo TemporalLocalityAnalyzer::analyzeStoreStore(StoreInst *Store1, StoreInst *Store2) {
    errs() << "===== Function:analyzeStoreStore =====\n";
    TemporalLocalityInfo Result;
    
    if (!Store1 || !Store2) return Result;
    
    Value *Ptr1 = Store1->getPointerOperand();
    Value *Ptr2 = Store2->getPointerOperand();
    
    if (Ptr1 == Ptr2 || 
        Ptr1->stripPointerCasts() == Ptr2->stripPointerCasts()) {
      // Same memory location - overwriting indicates poor reuse
      unsigned Distance = calculateInstructionDistance(Store1, Store2);
      
      // Check if they're in different iterations of same loop
      Loop *L1 = LI.getLoopFor(Store1->getParent());
      Loop *L2 = LI.getLoopFor(Store2->getParent());
      bool InSameLoop = (L1 && L2 && (L1 == L2 || L1->contains(L2) || L2->contains(L1)));
      
      if (InSameLoop && Distance > 20) {
        // Different loop iterations likely
        Result.level = TemporalLocalityLevel::MODERATE;
        Result.estimatedReuseDistance = Distance;
        Result.reuseFrequency = 0.3;
        Result.temporalLocalityScore = 30.0;
      } else {
        // Overwriting in close proximity - poor reuse
        Result.level = TemporalLocalityLevel::POOR;
        Result.estimatedReuseDistance = Distance;
        Result.reuseFrequency = 0.1;
        Result.temporalLocalityScore = 10.0;
      }
    } else {
      // Different memory locations
      Result.level = TemporalLocalityLevel::POOR;
      Result.estimatedReuseDistance = UINT_MAX;
      Result.temporalLocalityScore = 5.0;
    }
    
    return Result;
  }
  
  // Interprocedural analysis
  TemporalLocalityInfo TemporalLocalityAnalyzer::analyzeInterproceduralTemporalLocality(Value *Ptr, Module &M) {
    errs() << "===== Function:analyzeInterproceduralTemporalLocality =====\n";
    TemporalLocalityInfo Result;
    
    if (!Ptr) return Result;
    
    // Only meaningful for function arguments and globals
    bool IsGlobal = isa<GlobalValue>(Ptr);
    bool IsArgument = isa<Argument>(Ptr);
    
    if (!IsGlobal && !IsArgument) return Result;
    
    // For globals, check usage across multiple functions
    if (IsGlobal) {
      GlobalValue *GV = cast<GlobalValue>(Ptr);
      
      // Count functions that use this global
      unsigned UseCount = 0;
      bool HasLoopUses = false;
      bool HasFrequentReuses = false;
      
      for (auto &F : M) {
        bool UsedInFunction = false;
        
        for (auto &BB : F) {
          for (auto &I : BB) {
            for (unsigned i = 0; i < I.getNumOperands(); ++i) {
              if (I.getOperand(i) == GV || 
                  (isa<LoadInst>(I) && 
                   cast<LoadInst>(I).getPointerOperand()->stripPointerCasts() == GV) ||
                  (isa<StoreInst>(I) && 
                   cast<StoreInst>(I).getPointerOperand()->stripPointerCasts() == GV)) {
                UsedInFunction = true;
                
                // Check if inside a loop
                if (LI.getLoopFor(&BB)) {
                  HasLoopUses = true;
                }
                
                // Check for multiple uses in same function
                for (auto &OtherBB : F) {
                  if (&OtherBB != &BB) {
                    for (auto &OtherI : OtherBB) {
                      for (unsigned j = 0; j < OtherI.getNumOperands(); ++j) {
                        if (OtherI.getOperand(j) == GV || 
                            (isa<LoadInst>(OtherI) && 
                             cast<LoadInst>(OtherI).getPointerOperand()->stripPointerCasts() == GV) ||
                            (isa<StoreInst>(OtherI) && 
                             cast<StoreInst>(OtherI).getPointerOperand()->stripPointerCasts() == GV)) {
                          HasFrequentReuses = true;
                          break;
                        }
                      }
                      if (HasFrequentReuses) break;
                    }
                  }
                  if (HasFrequentReuses) break;
                }
              }
            }
          }
        }
        
        if (UsedInFunction) UseCount++;
      }
      
      // Heuristic score based on usage patterns
      if (UseCount <= 1) {
        // Used in only one function - use standard analysis
        Result.level = TemporalLocalityLevel::MODERATE;
        Result.temporalLocalityScore = 40.0;
      } else if (UseCount >= 5) {
        // Used across many functions - high importance
        Result.level = TemporalLocalityLevel::EXCELLENT;
        Result.temporalLocalityScore = 85.0;
      } else if (HasLoopUses && HasFrequentReuses) {
        // Used in loops with frequent reuse
        Result.level = TemporalLocalityLevel::GOOD;
        Result.temporalLocalityScore = 75.0;
      } else {
        // Moderate cross-function usage
        Result.level = TemporalLocalityLevel::GOOD;
        Result.temporalLocalityScore = 65.0;
      }
    }
    // For arguments, check usage within the function
    else if (IsArgument) {
      Argument *Arg = cast<Argument>(Ptr);
      Function *F = Arg->getParent();
      
      // Use intra-function analysis
      Result = analyzeTemporalLocality(Ptr, *F);
      
      // Look for calls to this function
      unsigned CallSiteCount = 0;
      for (auto &OF : M) {
        for (auto &BB : OF) {
          for (auto &I : BB) {
            if (auto *Call = dyn_cast<CallInst>(&I)) {
              if (Call->getCalledFunction() == F) {
                CallSiteCount++;
                break;
              }
            }
          }
        }
      }
      
      // Adjust score based on call frequency
      if (CallSiteCount > 3) {
        // Called from many places - likely important
        Result.temporalLocalityScore = std::min(100.0, Result.temporalLocalityScore * 1.2);
      }
    }
    
    return Result;
  }
  
  bool TemporalLocalityAnalyzer::hasTemporalLocalityHints(Instruction *I) {
    errs() << "===== Function:hasTemporalLocalityHints =====\n";
    if (!I) return false;
    
    // Check for prefetch instructions
    if (auto *Call = dyn_cast<CallInst>(I)) {
      Function *Callee = Call->getCalledFunction();
      if (Callee && (Callee->getName().contains("prefetch") || 
                    Callee->getIntrinsicID() == Intrinsic::prefetch)) {
        return true;
      }
    }
    
    // Check for cache hint metadata
    MDNode *CacheHint = I->getMetadata("cache");
    if (CacheHint) return true;
    
    // Check loop metadata for vectorization or unrolling
    if (auto *BI = dyn_cast<BranchInst>(I)) {
      if (MDNode *LoopID = BI->getMetadata("llvm.loop")) {
        for (unsigned i = 0; i < LoopID->getNumOperands(); ++i) {
          MDNode *MD = dyn_cast<MDNode>(LoopID->getOperand(i));
          if (!MD) continue;
          
          MDString *S = dyn_cast<MDString>(MD->getOperand(0));
          if (!S) continue;
          
          if (S->getString().equals("llvm.loop.vectorize.enable") || 
              S->getString().equals("llvm.loop.unroll.enable")) {
            return true;
          }
        }
      }
    }
    
    return false;
  }