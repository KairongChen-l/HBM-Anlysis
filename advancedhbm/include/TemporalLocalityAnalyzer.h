#ifndef TEMPORALLOCALITYANALYZER_H
#define TEMPORALLOCALITYANALYZER_H
#include "AnalysisTypes.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <map>
#include <set>
#include <vector>

namespace MyHBM
{
  class TemporalLocalityAnalyzer
  {
  private:
    llvm::ScalarEvolution &SE;
    llvm::LoopInfo &LI;

    // Maps value to its last access instruction
    using AccessMap = llvm::DenseMap<llvm::Value *, llvm::Instruction *>;

    // Helper methods
    unsigned calculateInstructionDistance(llvm::Instruction *I1, llvm::Instruction *I2);
    bool detectLoopCarriedReuse(llvm::Value *Ptr, llvm::Loop *L);
    std::vector<llvm::Instruction *> findAllMemoryAccesses(llvm::Value *Ptr, llvm::Function &F);
    llvm::SmallVector<std::pair<llvm::Instruction *, llvm::Instruction *>, 8>
    identifyReusePatterns(const std::vector<llvm::Instruction *> &Accesses);
    bool hasTemporalLocalityHints(llvm::Instruction *I);
    double scoreTemporalLocality(TemporalLocalityLevel level, unsigned reuseDistance, bool isInLoop);
    double analyzeDataKeepaliveness(llvm::Value *Ptr, llvm::Function &F);

  public:
    TemporalLocalityAnalyzer(llvm::ScalarEvolution &SE, llvm::LoopInfo &LI)
        : SE(SE), LI(LI) {}

    // Main analysis methods
    TemporalLocalityInfo analyzeTemporalLocality(llvm::Value *Ptr, llvm::Function &F);
    TemporalLocalityLevel analyzeReusePattern(llvm::Value *Ptr, llvm::Loop *L);
    unsigned estimateReuseDistance(llvm::Value *Ptr, llvm::Loop *L);
    double calculateTemporalLocalityScore(llvm::Value *Ptr, llvm::Function &F);

    // Analysis for specific instruction patterns
    TemporalLocalityInfo analyzeLoadStore(llvm::LoadInst *Load, llvm::StoreInst *Store);
    TemporalLocalityInfo analyzeLoadLoad(llvm::LoadInst *Load1, llvm::LoadInst *Load2);
    TemporalLocalityInfo analyzeStoreStore(llvm::StoreInst *Store1, llvm::StoreInst *Store2);

    // Interprocedural analysis
    TemporalLocalityInfo analyzeInterproceduralTemporalLocality(llvm::Value *Ptr, llvm::Module &M);
  };

} // namespace MyHBM

#endif // TEMPORALLOCALITYANALYZER_H