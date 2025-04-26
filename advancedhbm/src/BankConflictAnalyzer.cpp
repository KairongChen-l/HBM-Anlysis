#include "BankConflictAnalyzer.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Operator.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include <cmath>
#include <map>
#include <algorithm>
#include <numeric>

using namespace llvm;
using namespace MyHBM;

// Get the bank number for a given address based on HBM hardware
unsigned BankConflictAnalyzer::getBankNumber(uint64_t address) {
  errs() << "===== Function:getBankNumber =====\n";
  
  // Apply XOR banking function similar to real HBM
  // Actual mapping is hardware-specific, this is a simplified version
  
  // Extract bank bits (typically bits 3-9 for 128 byte row buffer)
  unsigned bankBits = (address >> 3) & ((1 << HBMConfig.bankXORBits) - 1);
  
  // Apply XOR folding of higher bits as often used in hardware
  unsigned higherBits = (address >> (3 + HBMConfig.bankXORBits)) & ((1 << HBMConfig.bankXORBits) - 1);
  
  // XOR the bank bits with higher bits for better distribution
  unsigned bankNumber = bankBits ^ higherBits;
  
  // Ensure it's within bounds
  return bankNumber % HBMConfig.numBanks;
}

// Get the channel number for a given address
unsigned BankConflictAnalyzer::getChannelNumber(uint64_t address) {
  errs() << "===== Function:getChannelNumber =====\n";
  
  // Channel selection is typically based on higher address bits
  // Often XORed with other bits for better distribution
  
  // Extract channel bits (implementation simplified)
  unsigned channelBits = (address >> (3 + HBMConfig.bankXORBits)) & ((1 << HBMConfig.channelBits) - 1);
  
  // Apply XOR with another chunk of address
  unsigned higherBits = (address >> (3 + HBMConfig.bankXORBits + HBMConfig.channelBits)) & 
                        ((1 << HBMConfig.channelBits) - 1);
  
  // XOR the bits for better distribution
  unsigned channelNumber = channelBits ^ higherBits;
  
  // Ensure it's within bounds
  return channelNumber % HBMConfig.numChannels;
}

// Main analysis method
BankConflictInfo BankConflictAnalyzer::analyzeBankConflicts(Value *Ptr, Loop *L) {
  errs() << "===== Function:analyzeBankConflicts =====\n";
  BankConflictInfo Result;
  
  if (!Ptr || !L) {
    Result.severity = BankConflictSeverity::NONE;
    Result.type = BankConflictType::NONE;
    return Result;
  }
  
  // Initialize bank histogram with zeros
  Result.bankHistogram.resize(HBMConfig.numBanks, 0);
  
  // First, try to determine address mapping function
  Result.hasBankingFunction = detectAddressMappingPattern(Ptr, Result);
  
  // If it's a GEP, analyze array index patterns
  if (auto *GEP = dyn_cast<GetElementPtrInst>(Ptr)) {
    analyzeArrayIndexPattern(GEP, L, Result);
  }
  
  // Use ScalarEvolution for stride pattern analysis
  if (SE.isSCEVable(Ptr->getType())) {
    const SCEV *PtrSCEV = SE.getSCEV(Ptr);
    
    // Analyze stride patterns
    analyzeStridePattern(PtrSCEV, L, Result);
    
    // More detailed SCEV-based analysis
    detectSCEVBasedConflicts(PtrSCEV, L, Result);
  }
  
  // Analyze address distribution across banks
  analyzeAddressDistribution(Ptr, L, Result);
  
  // Calculate final conflict score
  Result.conflictScore = calculateBankConflictScore(Result);
  
  // Estimate performance impact
  Result.performanceImpact = estimateConflictImpact(Result);
  
  return Result;
}

// Detect address mapping patterns
bool BankConflictAnalyzer::detectAddressMappingPattern(Value *Ptr, BankConflictInfo &Result) {
  errs() << "===== Function:detectAddressMappingPattern =====\n";
  
  // Try to determine if address bits are constant or variable
  if (!SE.isSCEVable(Ptr->getType())) 
    return false;
    
  const SCEV *PtrSCEV = SE.getSCEV(Ptr);
  
  // Check which address bits are constant
  std::bitset<16> constantBits;
  constantBits.set(); // Assume all constant initially
  
  for (unsigned i = 0; i < 16; ++i) {
    if (!isAddressBitConstant(PtrSCEV, i)) {
      constantBits.reset(i);
    }
  }
  
  // Check for bank conflict risk based on bit patterns
  
  // Case 1: Lower bank selection bits constant, higher bits vary
  // This suggests repeated access to same bank
  unsigned bankLowerBit = 3; // Typical for 8-byte access
  unsigned bankUpperBit = bankLowerBit + HBMConfig.bankXORBits - 1;
  
  bool lowerBankBitsConstant = true;
  for (unsigned i = bankLowerBit; i <= bankUpperBit; ++i) {
    if (!constantBits[i]) {
      lowerBankBitsConstant = false;
      break;
    }
  }
  
  bool higherBitsVary = false;
  for (unsigned i = bankUpperBit + 1; i < 16; ++i) {
    if (!constantBits[i]) {
      higherBitsVary = true;
      break;
    }
  }
  
  if (lowerBankBitsConstant && higherBitsVary) {
    // High risk of same-bank conflicts
    Result.type = BankConflictType::SAME_BANK_ACCESS;
    Result.severity = BankConflictSeverity::HIGH;
    Result.analysisDescription = "Bank selection bits constant while higher bits vary - high risk of same-bank conflicts";
    return true;
  }
  
  // Case 2: Strided access with conflict-prone stride
  if (auto *AR = dyn_cast<SCEVAddRecExpr>(PtrSCEV)) {
    if (AR->isAffine()) {
      const SCEV *Step = AR->getStepRecurrence(SE);
      if (auto *SC = dyn_cast<SCEVConstant>(Step)) {
        int64_t Stride = SC->getValue()->getSExtValue();
        
        // Check if stride is a multiple of bank count or related pattern
        if (isConflictingStride(Stride, HBMConfig.numBanks)) {
          Result.type = BankConflictType::STRIDED_CONFLICT;
          Result.severity = BankConflictSeverity::HIGH;
          Result.analysisDescription = "Strided access with conflict-prone stride value: " + 
                                      std::to_string(Stride);
          return true;
        }
      }
    }
  }
  
  // Case 3: Channel imbalance detection
  unsigned channelLowerBit = bankUpperBit + 1;
  unsigned channelUpperBit = channelLowerBit + HBMConfig.channelBits - 1;
  
  bool channelBitsConstant = true;
  for (unsigned i = channelLowerBit; i <= channelUpperBit; ++i) {
    if (!constantBits[i]) {
      channelBitsConstant = false;
      break;
    }
  }
  
  if (channelBitsConstant) {
    Result.type = BankConflictType::CHANNEL_IMBALANCE;
    Result.severity = BankConflictSeverity::MODERATE;
    Result.analysisDescription = "Channel selection bits constant - may cause channel imbalance";
    return true;
  }
  
  // No obvious pattern detected
  return false;
}

// Check if an address bit is constant
bool BankConflictAnalyzer::isAddressBitConstant(const SCEV *AddressSCEV, unsigned bitPosition) {
  errs() << "===== Function:isAddressBitConstant =====\n";
  
  // Extract the bit from the SCEV expression
  const SCEV *BitExtract = SE.getUDivExpr(
      SE.getURemExpr(
          SE.getUDivExpr(AddressSCEV, SE.getConstant(AddressSCEV->getType(), 1ULL << bitPosition)),
          SE.getConstant(AddressSCEV->getType(), 2)
      ),
      SE.getConstant(AddressSCEV->getType(), 1)
  );
  
  // If the resulting expression is a constant, the bit is constant
  return isa<SCEVConstant>(BitExtract);
}

// Check if a stride value is likely to cause bank conflicts
bool BankConflictAnalyzer::isConflictingStride(int64_t stride, unsigned numBanks) {
  errs() << "===== Function:isConflictingStride =====\n";
  
  // Common problematic strides:
  
  // Case 1: Stride is multiple of banks or near multiple
  if (stride % numBanks == 0 || (stride + 1) % numBanks == 0 || (stride - 1) % numBanks == 0) {
    return true;
  }
  
  // Case 2: Stride is power of 2 related to bank count (common in HBM)
  if (numBanks == 32 || numBanks == 16) {
    for (unsigned i = 4; i <= 12; ++i) {  // Common element sizes 16 bytes to 4KB
      uint64_t powerOf2 = 1ULL << i;
      if (stride == (int64_t)powerOf2 || 
          stride == (int64_t)(powerOf2 - 1) || 
          stride == (int64_t)(powerOf2 + 1)) {
        return true;
      }
    }
  }
  
  // Case 3: Stride causes bank conflicts in XOR-based banking
  if (numBanks == 32) {
    // These are just examples of known bad strides for common XOR mappings
    static const std::vector<int64_t> knownBadStrides = {
      8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096,  // Powers of 2
      7, 15, 31, 63, 127, 255, 511, 1023, 2047, 4095   // Powers of 2 minus 1
    };
    
    for (int64_t badStride : knownBadStrides) {
      if (stride == badStride || stride % badStride == 0) {
        return true;
      }
    }
  }
  
  return false;
}

// Analyze array index patterns
void BankConflictAnalyzer::analyzeArrayIndexPattern(GetElementPtrInst *GEP, Loop *L, BankConflictInfo &Result) {
  errs() << "===== Function:analyzeArrayIndexPattern =====\n";
  
  if (!GEP || !L) return;
  
  // Get element type and size
  Type *ElemTy = GEP->getSourceElementType();
  const DataLayout &DL = GEP->getModule()->getDataLayout();
  uint64_t ElemSize = DL.getTypeAllocSize(ElemTy);
  
  // Check array dimensions and access patterns
  bool hasLoopDepIndex = false;
  bool hasConstantIndex = false;
  bool hasNonlinearIndex = false;
  
  for (auto I = GEP->idx_begin(), E = GEP->idx_end(); I != E; ++I) {
    Value *Idx = *I;
    
    // Skip constant indices
    if (isa<ConstantInt>(Idx)) {
      hasConstantIndex = true;
      continue;
    }
    
    // Check if index depends on loop induction variable
    if (SE.isSCEVable(Idx->getType())) {
      const SCEV *IdxSCEV = SE.getSCEV(Idx);
      
      if (auto *AR = dyn_cast<SCEVAddRecExpr>(IdxSCEV)) {
        if (AR->getLoop() == L) {
          hasLoopDepIndex = true;
          
          // Check if stride might cause bank conflicts
          if (AR->isAffine()) {
            const SCEV *Step = AR->getStepRecurrence(SE);
            if (auto *SC = dyn_cast<SCEVConstant>(Step)) {
              int64_t Stride = SC->getValue()->getSExtValue();
              int64_t EffectiveStride = Stride * ElemSize;
              
              if (isConflictingStride(EffectiveStride, HBMConfig.numBanks)) {
                Result.type = BankConflictType::STRIDED_CONFLICT;
                Result.severity = BankConflictSeverity::HIGH;
                Result.analysisDescription = "Array index pattern creates conflict-prone stride: " + 
                                           std::to_string(EffectiveStride) + " bytes";
                Result.conflictRate = 0.8; // Estimated high conflict rate
                return;
              }
            }
          } else {
            // Non-affine recurrence
            hasNonlinearIndex = true;
          }
        }
      } else if (!SE.isLoopInvariant(IdxSCEV, L)) {
        // Loop-variant but not a simple recurrence
        hasNonlinearIndex = true;
      }
    }
  }
  
  // Analyze findings
  if (hasLoopDepIndex) {
    if (hasNonlinearIndex) {
      // Complex access pattern
      Result.type = BankConflictType::RANDOM_CONFLICT;
      Result.severity = BankConflictSeverity::MODERATE;
      Result.analysisDescription = "Complex array indexing may cause unpredictable bank access patterns";
      Result.conflictRate = 0.5; // Medium conflict rate
    } else if (hasConstantIndex && GEP->getNumIndices() > 1) {
      // Likely row/column access in multidimensional array
      // Could be either strided (column-major) or streaming (row-major)
      Result.type = BankConflictType::STRIDED_CONFLICT;
      Result.severity = BankConflictSeverity::MODERATE;
      Result.analysisDescription = "Multi-dimensional array access may cause strided conflicts";
      Result.conflictRate = 0.4; // Medium conflict rate
    }
  }
}

// Analyze stride patterns
void BankConflictAnalyzer::analyzeStridePattern(const SCEV *PtrSCEV, Loop *L, BankConflictInfo &Result) {
  errs() << "===== Function:analyzeStridePattern =====\n";
  
  if (!PtrSCEV || !L) return;
  
  // Check for affine recurrence (linear stride pattern)
  if (auto *AR = dyn_cast<SCEVAddRecExpr>(PtrSCEV)) {
    if (AR->getLoop() == L && AR->isAffine()) {
      const SCEV *Step = AR->getStepRecurrence(SE);
      
      if (auto *SC = dyn_cast<SCEVConstant>(Step)) {
        int64_t Stride = SC->getValue()->getSExtValue();
        
        // Check if stride is problematic for bank conflicts
        if (isConflictingStride(Stride, HBMConfig.numBanks)) {
          Result.type = BankConflictType::STRIDED_CONFLICT;
          Result.severity = BankConflictSeverity::HIGH;
          Result.analysisDescription = "Memory access stride of " + 
                                     std::to_string(Stride) + 
                                     " bytes likely causes bank conflicts";
          
          // Estimate conflict rate based on stride pattern
          if (Stride % HBMConfig.numBanks == 0) {
            // Stride is exact multiple - very bad
            Result.conflictRate = 0.95;
          } else if (Stride % (HBMConfig.numBanks / 2) == 0) {
            // Stride hits half of banks
            Result.conflictRate = 0.7;
          } else {
            // Other bad stride patterns
            Result.conflictRate = 0.5;
          }
          
          return;
        } else if (Stride == 1 || Stride == 2 || Stride == 4 || Stride == 8) {
          // Unit stride or small strides are generally good
          Result.type = BankConflictType::NONE;
          Result.severity = BankConflictSeverity::NONE;
          Result.analysisDescription = "Unit or small stride pattern - low risk of bank conflicts";
          Result.conflictRate = 0.05;
          return;
        }
      } else {
        // Variable stride
        Result.type = BankConflictType::RANDOM_CONFLICT;
        Result.severity = BankConflictSeverity::MODERATE;
        Result.analysisDescription = "Variable stride pattern may cause inconsistent bank conflicts";
        Result.conflictRate = 0.4;
        return;
      }
    } else if (!AR->isAffine()) {
      // Non-linear recurrence
      Result.type = BankConflictType::RANDOM_CONFLICT;
      Result.severity = BankConflictSeverity::MODERATE;
      Result.analysisDescription = "Non-linear memory access pattern";
      Result.conflictRate = 0.3;
      return;
    }
  }
}

// More detailed SCEV-based conflict detection
void BankConflictAnalyzer::detectSCEVBasedConflicts(const SCEV *PtrSCEV, Loop *L, BankConflictInfo &Result) {
  errs() << "===== Function:detectSCEVBasedConflicts =====\n";
  
  if (!PtrSCEV || !L) return;
  
  // For AddRec expressions, simulate the access pattern
  if (auto *AR = dyn_cast<SCEVAddRecExpr>(PtrSCEV)) {
    if (AR->getLoop() == L) {
      // Try to extract start address and step
      const SCEV *Start = AR->getStart();
      bool hasConstantStart = isa<SCEVConstant>(Start);
      
      if (AR->isAffine()) {
        const SCEV *Step = AR->getStepRecurrence(SE);
        bool hasConstantStep = isa<SCEVConstant>(Step);
        
        if (hasConstantStart && hasConstantStep) {
          // We have constant start and step - simulate access pattern
          uint64_t StartAddr = cast<SCEVConstant>(Start)->getValue()->getZExtValue();
          int64_t Stride = cast<SCEVConstant>(Step)->getValue()->getSExtValue();
          
          // Estimate trip count
          unsigned TripCount = 100; // Default
          if (auto CountMaybe = SE.getSmallConstantTripCount(L)) {
            TripCount = CountMaybe;
          }
          TripCount = std::min(TripCount, 1000u); // Limit simulation size
          
          // Simulate memory accesses and track bank distribution
          std::vector<unsigned> bankHits(HBMConfig.numBanks, 0);
          std::vector<unsigned> bankConflictCount(HBMConfig.numBanks, 0);
          std::vector<uint64_t> lastBankAccessIter(HBMConfig.numBanks, 0);
          
          for (unsigned i = 0; i < TripCount; ++i) {
            uint64_t Address = StartAddr + i * Stride;
            unsigned Bank = getBankNumber(Address);
            bankHits[Bank]++;
            
            // Check for temporal bank conflicts (back-to-back access to same bank)
            if (i > 0 && i - lastBankAccessIter[Bank] < 5) {
              bankConflictCount[Bank]++;
            }
            lastBankAccessIter[Bank] = i;
          }
          
          // Update bank histogram in result
          Result.bankHistogram = bankHits;
          
          // Calculate statistics for bank distribution
          unsigned maxHits = *std::max_element(bankHits.begin(), bankHits.end());
          unsigned minHits = *std::min_element(bankHits.begin(), bankHits.end());
          unsigned totalHits = std::accumulate(bankHits.begin(), bankHits.end(), 0u);
          unsigned totalConflicts = std::accumulate(bankConflictCount.begin(), bankConflictCount.end(), 0u);
          
          // Ideal distribution would have hits evenly distributed
          unsigned idealHitsPerBank = totalHits / HBMConfig.numBanks;
          double stdDev = calculateDistributionStdDev(bankHits);
          double normalizedStdDev = (idealHitsPerBank > 0) ? stdDev / idealHitsPerBank : 0.0;
          
          // Interpret results
          Result.affectedBanks = 0;
          for (unsigned hits : bankHits) {
            if (hits > 0) Result.affectedBanks++;
          }
          
          Result.totalAccessedBanks = Result.affectedBanks;
          Result.conflictRate = (double)totalConflicts / totalHits;
          
          // Determine severity based on distribution
          if (normalizedStdDev > 1.0 || maxHits > 3 * minHits) {
            // Highly unbalanced
            Result.severity = BankConflictSeverity::HIGH;
            Result.type = BankConflictType::STRIDED_CONFLICT;
            Result.analysisDescription = "Highly unbalanced bank access pattern detected";
          } else if (normalizedStdDev > 0.5 || maxHits > 2 * minHits) {
            // Moderately unbalanced
            Result.severity = BankConflictSeverity::MODERATE;
            Result.type = BankConflictType::STRIDED_CONFLICT;
            Result.analysisDescription = "Moderately unbalanced bank access pattern";
          } else if (Result.conflictRate > 0.2) {
            // Balanced but with conflicts
            Result.severity = BankConflictSeverity::LOW;
            Result.type = BankConflictType::STRIDED_CONFLICT;
            Result.analysisDescription = "Balanced bank distribution but with some conflicts";
          } else {
            // Good pattern
            Result.severity = BankConflictSeverity::NONE;
            Result.type = BankConflictType::NONE;
            Result.analysisDescription = "Well-distributed bank access pattern";
          }
          
          return;
        }
      }
    }
  }
}

// Analyze address distribution
void BankConflictAnalyzer::analyzeAddressDistribution(Value *Ptr, Loop *L, BankConflictInfo &Result) {
    errs() << "===== Function:analyzeAddressDistribution =====\n";
    
    if (!Ptr || !L) return;
    
    // Collect all memory accesses in the loop
    std::vector<Instruction*> MemAccesses;
    for (BasicBlock *BB : L->getBlocks()) {
      for (Instruction &I : *BB) {
        if (auto *LI = dyn_cast<LoadInst>(&I)) {
          Value *AccessPtr = LI->getPointerOperand();
          if (AccessPtr == Ptr || 
              (AccessPtr->stripPointerCasts() == Ptr->stripPointerCasts())) {
            MemAccesses.push_back(&I);
          }
        } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
          Value *AccessPtr = SI->getPointerOperand();
          if (AccessPtr == Ptr || 
              (AccessPtr->stripPointerCasts() == Ptr->stripPointerCasts())) {
            MemAccesses.push_back(&I);
          }
        }
      }
    }
    
    // Not enough accesses to analyze
    if (MemAccesses.size() < 2) return;
    
    // Check for vectorized/coalesced accesses
    unsigned vectorizedCount = 0;
    for (Instruction *I : MemAccesses) {
      if (isCoalescedAccess(I)) {
        vectorizedCount++;
      }
    }
    
    // Vectorized accesses are generally better for bank utilization
    if (vectorizedCount > MemAccesses.size() / 2) {
      Result.severity = BankConflictSeverity::LOW;
      Result.type = BankConflictType::NONE;
      Result.analysisDescription = "Mostly vectorized/coalesced memory accesses - good for bank utilization";
      Result.conflictRate = 0.1;
      return;
    }
    
    // For scalar accesses, we need more detailed analysis
    // This is just a fallback if we couldn't determine from SCEV
    
    if (Result.severity == BankConflictSeverity::NONE && 
        Result.type == BankConflictType::NONE) {
      // Use heuristics based on loop structure and access pattern
      
      // Check if this is a nested loop
      bool isNestedLoop = !L->getSubLoops().empty() || L->getParentLoop() != nullptr;
      
      // Check access density
      double accessDensity = (double)MemAccesses.size() / L->getBlocks().size();
      
      if (isNestedLoop && accessDensity > 2.0) {
        // High density nested loops often have bank conflicts
        Result.severity = BankConflictSeverity::MODERATE;
        Result.type = BankConflictType::STRIDED_CONFLICT;
        Result.analysisDescription = "Dense memory accesses in nested loop - moderate risk of bank conflicts";
        Result.conflictRate = 0.3;
      } else {
        // Default to low risk
        Result.severity = BankConflictSeverity::LOW;
        Result.type = BankConflictType::RANDOM_CONFLICT;
        Result.analysisDescription = "No specific bank conflict pattern detected";
        Result.conflictRate = 0.2;
      }
    }
  }
  
  // Check if instruction is a coalesced/vectorized access
  bool BankConflictAnalyzer::isCoalescedAccess(Instruction *I) {
    errs() << "===== Function:isCoalescedAccess =====\n";
    
    if (!I) return false;
    
    // Vector type operations
    if (I->getType()->isVectorTy()) return true;
    
    // Check for vector intrinsics
    if (auto *CI = dyn_cast<CallInst>(I)) {
      Function *F = CI->getCalledFunction();
      if (F && (F->getName().contains("llvm.vector") || 
                F->getName().contains("vload") || 
                F->getName().contains("vstore"))) {
        return true;
      }
    }
    
    // Check for aligned, cache-line sized access
    if (auto *LI = dyn_cast<LoadInst>(I)) {
      if (LI->getAlign().value() >= 32) return true; // Well-aligned load
    } else if (auto *SI = dyn_cast<StoreInst>(I)) {
      if (SI->getAlign().value() >= 32) return true; // Well-aligned store
    }
    
    return false;
  }
  
  // Calculate standard deviation of bank distribution
  double BankConflictAnalyzer::calculateDistributionStdDev(const std::vector<unsigned> &distribution) {
    errs() << "===== Function:calculateDistributionStdDev =====\n";
    
    if (distribution.empty()) return 0.0;
    
    // Calculate mean
    double sum = std::accumulate(distribution.begin(), distribution.end(), 0.0);
    double mean = sum / distribution.size();
    
    // Calculate variance
    double variance = 0.0;
    for (unsigned value : distribution) {
      variance += (value - mean) * (value - mean);
    }
    variance /= distribution.size();
    
    // Return standard deviation
    return std::sqrt(variance);
  }
  
  // Calculate bank conflict score
  double BankConflictAnalyzer::calculateBankConflictScore(const BankConflictInfo &Info) {
    errs() << "===== Function:calculateBankConflictScore =====\n";
    
    // Base score - lower is better for bank conflicts
    double score = 0.0;
    
    // Adjust based on severity
    switch (Info.severity) {
      case BankConflictSeverity::NONE:
        score = 0.0;
        break;
      case BankConflictSeverity::LOW:
        score = -5.0;
        break;
      case BankConflictSeverity::MODERATE:
        score = -15.0;
        break;
      case BankConflictSeverity::HIGH:
        score = -30.0;
        break;
      case BankConflictSeverity::SEVERE:
        score = -50.0;
        break;
    }
    
    // Adjust based on conflict type
    switch (Info.type) {
      case BankConflictType::NONE:
        // No adjustment
        break;
      case BankConflictType::SAME_BANK_ACCESS:
        // Worst case
        score -= 20.0;
        break;
      case BankConflictType::STRIDED_CONFLICT:
        // Very bad but can sometimes be mitigated
        score -= 15.0;
        break;
      case BankConflictType::RANDOM_CONFLICT:
        // Unpredictable but not necessarily always bad
        score -= 10.0;
        break;
      case BankConflictType::PARTIAL_ROW_ACCESS:
        // Inefficient but not severe
        score -= 5.0;
        break;
      case BankConflictType::CHANNEL_IMBALANCE:
        // Less severe than bank conflicts
        score -= 8.0;
        break;
    }
    
    // Adjust based on conflict rate
    score -= Info.conflictRate * 30.0;
    
    // Adjust based on bank distribution
    double bankUsageRatio = (double)Info.affectedBanks / HBMConfig.numBanks;
    if (bankUsageRatio < 0.25) {
      // Very poor bank utilization
      score -= 10.0;
    } else if (bankUsageRatio > 0.75) {
      // Good bank utilization
      score += 5.0;
    }
    
    // Limit the range
    return std::max(-100.0, std::min(0.0, score));
  }
  
  // Estimate performance impact of bank conflicts
  double BankConflictAnalyzer::estimateConflictImpact(const BankConflictInfo &Info) {
    errs() << "===== Function:estimateConflictImpact =====\n";
    
    // Base performance impact (1.0 = no impact, higher = worse)
    double impact = 1.0;
    
    // Adjust based on severity and conflict rate
    switch (Info.severity) {
      case BankConflictSeverity::NONE:
        impact = 1.0;
        break;
      case BankConflictSeverity::LOW:
        impact = 1.1 + 0.2 * Info.conflictRate;
        break;
      case BankConflictSeverity::MODERATE:
        impact = 1.3 + 0.5 * Info.conflictRate;
        break;
      case BankConflictSeverity::HIGH:
        impact = 1.5 + Info.conflictRate;
        break;
      case BankConflictSeverity::SEVERE:
        impact = 2.0 + 2.0 * Info.conflictRate;
        break;
    }
    
    // Adjust based on bank utilization
    double bankUtilization = (double)Info.affectedBanks / HBMConfig.numBanks;
    if (bankUtilization < 0.2) {
      // Very poor utilization increases impact
      impact *= 1.2;
    } else if (bankUtilization > 0.8) {
      // Good utilization may mitigate impact
      impact *= 0.9;
    }
    
    return impact;
  }
  
  // Function-level analysis
  BankConflictInfo BankConflictAnalyzer::analyzeFunctionBankConflicts(Value *Ptr, Function &F) {
    errs() << "===== Function:analyzeFunctionBankConflicts =====\n";
    
    BankConflictInfo Result;
    Result.severity = BankConflictSeverity::NONE;
    Result.type = BankConflictType::NONE;
    
    if (!Ptr) return Result;
    
    // Find loops that access this pointer
    std::map<Loop*, std::vector<Instruction*>> LoopAccesses;
    
    for (auto &BB : F) {
      Loop *L = LI.getLoopFor(&BB);
      
      for (auto &I : BB) {
        bool accessesPtr = false;
        Value *AccessPtr = nullptr;
        
        if (auto *LI = dyn_cast<LoadInst>(&I)) {
          AccessPtr = LI->getPointerOperand();
          if (AccessPtr == Ptr || 
              (AccessPtr->stripPointerCasts() == Ptr->stripPointerCasts())) {
            accessesPtr = true;
          }
        } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
          AccessPtr = SI->getPointerOperand();
          if (AccessPtr == Ptr || 
              (AccessPtr->stripPointerCasts() == Ptr->stripPointerCasts())) {
            accessesPtr = true;
          }
        }
        
        if (accessesPtr && L != nullptr) {
          LoopAccesses[L].push_back(&I);
        }
      }
    }
    
    // Analyze each loop and combine results
    std::vector<BankConflictInfo> LoopResults;
    
    for (auto &Entry : LoopAccesses) {
      Loop *L = Entry.first;
      
      // Only analyze loops with significant number of accesses
      if (Entry.second.size() >= 3) {
        BankConflictInfo LoopResult = analyzeBankConflicts(Ptr, L);
        LoopResults.push_back(LoopResult);
      }
    }
    
    // Combine results from all loops
    if (LoopResults.empty()) {
      // No loops with significant access
      Result.severity = BankConflictSeverity::NONE;
      Result.type = BankConflictType::NONE;
      Result.analysisDescription = "No significant loop-based memory access pattern detected";
      Result.conflictScore = 0.0;
      return Result;
    }
    
    // Find the worst conflict pattern
    BankConflictSeverity worstSeverity = BankConflictSeverity::NONE;
    BankConflictType worstType = BankConflictType::NONE;
    double worstScore = 0.0;
    double worstImpact = 1.0;
    
    for (const auto &LoopResult : LoopResults) {
      if (static_cast<int>(LoopResult.severity) > static_cast<int>(worstSeverity)) {
        worstSeverity = LoopResult.severity;
        worstType = LoopResult.type;
        worstScore = LoopResult.conflictScore;
        worstImpact = LoopResult.performanceImpact;
        Result.analysisDescription = LoopResult.analysisDescription;
      }
    }
    
    Result.severity = worstSeverity;
    Result.type = worstType;
    Result.conflictScore = worstScore;
    Result.performanceImpact = worstImpact;
    
    // Combine bank statistics
    for (const auto &LoopResult : LoopResults) {
      for (size_t i = 0; i < LoopResult.bankHistogram.size() && i < Result.bankHistogram.size(); ++i) {
        Result.bankHistogram[i] += LoopResult.bankHistogram[i];
      }
    }
    
    // Calculate combined statistics
    Result.affectedBanks = 0;
    for (unsigned hits : Result.bankHistogram) {
      if (hits > 0) Result.affectedBanks++;
    }
    Result.totalAccessedBanks = Result.affectedBanks;
    
    // Average conflict rate weighted by severity
    double totalWeight = 0.0;
    double weightedConflictRate = 0.0;
    
    for (const auto &LoopResult : LoopResults) {
      double weight = static_cast<double>(static_cast<int>(LoopResult.severity) + 1);
      totalWeight += weight;
      weightedConflictRate += LoopResult.conflictRate * weight;
    }
    
    Result.conflictRate = (totalWeight > 0) ? weightedConflictRate / totalWeight : 0.0;
    
    return Result;
  }
  
  // Generate conflict-free access transformation
  Value* BankConflictAnalyzer::generateConflictFreeAccess(IRBuilder<> &Builder, 
                                                       Value *OriginalPtr,
                                                       const BankConflictInfo &Info) {
    errs() << "===== Function:generateConflictFreeAccess =====\n";
    
    if (!OriginalPtr) return nullptr;
    
    // Only transform if there's a significant conflict
    if (Info.severity <= BankConflictSeverity::LOW) {
      return OriginalPtr; // No transformation needed
    }
    
    Module *M = nullptr;
    if (auto *I = dyn_cast<Instruction>(OriginalPtr)) {
      M = I->getModule();
    } else if (auto *GV = dyn_cast<GlobalValue>(OriginalPtr)) {
      M = GV->getParent();
    } else {
      return OriginalPtr; // Can't determine module
    }
    
    // Different transformations based on conflict type
    switch (Info.type) {
      case BankConflictType::STRIDED_CONFLICT: {
        // For strided conflicts, we can try to apply padding or index transformation
        
        // Approach 1: Apply XOR bank mapping to index (modifies access pattern to reduce conflicts)
        if (auto *GEP = dyn_cast<GetElementPtrInst>(OriginalPtr)) {
          // Check if it's a simple array access with a loop-dependent index
          if (GEP->getNumIndices() == 2 && 
              !isa<ConstantInt>(GEP->getOperand(2))) {
            
            // Get the index
            Value *Index = GEP->getOperand(2);
            
            // Create XOR transformation: index ^ (index / numBanks)
            // This helps avoid stride patterns that cause conflicts
            
            // Step 1: index / numBanks (using right shift for power of 2)
            unsigned bankShift = log2(HBMConfig.numBanks);
            Value *ShiftedIndex = Builder.CreateLShr(Index, bankShift);
            
            // Step 2: XOR the original index with the shifted index
            Value *XORIndex = Builder.CreateXor(Index, ShiftedIndex);
            
            // Step 3: Create new GEP with transformed index
            std::vector<Value*> Indices;
            for (unsigned i = 1; i < GEP->getNumOperands(); ++i) {
              if (i == 2) {
                Indices.push_back(XORIndex); // Replace the index
              } else {
                Indices.push_back(GEP->getOperand(i));
              }
            }
            
            // Create new GEP
            Value *NewPtr = Builder.CreateGEP(
                GEP->getSourceElementType(),
                GEP->getPointerOperand(),
                Indices,
                "xor_bank_gep"
            );
            
            return NewPtr;
          }
        }
        break;
      }
        
      case BankConflictType::SAME_BANK_ACCESS: {
        // For same-bank conflicts, we can try array padding or transposition
        
        // Simple array transposition approach:
        if (auto *GEP = dyn_cast<GetElementPtrInst>(OriginalPtr)) {
          if (GEP->getNumIndices() == 2 && 
              !isa<ConstantInt>(GEP->getOperand(1)) && 
              !isa<ConstantInt>(GEP->getOperand(2))) {
            
            // Get row and column indices
            Value *RowIdx = GEP->getOperand(1);
            Value *ColIdx = GEP->getOperand(2);
            
            // Swap row and column indices
            std::vector<Value*> Indices;
            Indices.push_back(GEP->getOperand(0)); // Array base
            Indices.push_back(ColIdx);             // Use column as row
            Indices.push_back(RowIdx);             // Use row as column
            
            // Create transposed GEP
            Value *NewPtr = Builder.CreateGEP(
                GEP->getSourceElementType(),
                GEP->getPointerOperand(),
                Indices,
                "transposed_gep"
            );
            
            return NewPtr;
          }
        }
        break;
      }
        
      case BankConflictType::CHANNEL_IMBALANCE: {
        // For channel imbalance, we can try to modify the higher bits of address
        
        // Convert to integer for bit manipulation
        Type *IntPtrTy = M->getDataLayout().getIntPtrType(OriginalPtr->getContext());
        Value *AddrInt = Builder.CreatePtrToInt(OriginalPtr, IntPtrTy);
        
        // XOR with a pattern to change channel bits
        // For a simple example, XOR with address * 67 (a prime number)
        Value *ShiftedAddr = Builder.CreateMul(AddrInt, 
                                             ConstantInt::get(IntPtrTy, 67));
        ShiftedAddr = Builder.CreateLShr(ShiftedAddr, 12); // Shift to channel bits
        Value *XORedAddr = Builder.CreateXor(AddrInt, ShiftedAddr);
        
        // Convert back to pointer
        Value *NewPtr = Builder.CreateIntToPtr(XORedAddr, OriginalPtr->getType());
        
        return NewPtr;
      }
        
      default:
        // For other or unknown conflict types, return original pointer
        return OriginalPtr;
    }
    
    // Default: return original pointer if no transformation applied
    return OriginalPtr;
  }