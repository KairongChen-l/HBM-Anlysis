#ifndef MYHBM_BANKCONFLICTANALYZER_H
#define MYHBM_BANKCONFLICTANALYZER_H

#include "AnalysisTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstIterator.h"
#include <unordered_map>
#include <vector>
#include <bitset>

namespace MyHBM
{

    class BankConflictAnalyzer
    {
    private:
        llvm::ScalarEvolution &SE;
        llvm::LoopInfo &LI;
        HBMConfiguration HBMConfig;

        // Get the bank number for a given address
        unsigned getBankNumber(uint64_t address);

        // Get the channel number for a given address
        unsigned getChannelNumber(uint64_t address);

        // Calculate bank and channel distribution for a memory access pattern
        void analyzeAddressDistribution(llvm::Value *Ptr, llvm::Loop *L, BankConflictInfo &Result);

        // Check if an address bit is constant or varies
        bool isAddressBitConstant(const llvm::SCEV *AddressSCEV, unsigned bitPosition);

        // Detect XOR-based or other bank mapping patterns
        bool detectAddressMappingPattern(llvm::Value *Ptr, BankConflictInfo &Result);

        // Analyze index patterns for arrays
        void analyzeArrayIndexPattern(llvm::GetElementPtrInst *GEP, llvm::Loop *L, BankConflictInfo &Result);

        // Analyze stride patterns and their impact on bank conflicts
        void analyzeStridePattern(const llvm::SCEV *PtrSCEV, llvm::Loop *L, BankConflictInfo &Result);

        // Calculate bank conflict score based on the analysis
        double calculateBankConflictScore(const BankConflictInfo &Info);

        // Helper to calculate standard deviation of bank distribution
        double calculateDistributionStdDev(const std::vector<unsigned> &distribution);

        // Generate an improved memory access pattern (for optimization)
        llvm::Value *transformAccessPattern(llvm::IRBuilder<> &Builder,
                                            llvm::Value *OriginalPtr,
                                            const BankConflictInfo &Info);

        // Detect potential bank conflicts based on SCEV analysis
        void detectSCEVBasedConflicts(const llvm::SCEV *PtrSCEV, llvm::Loop *L, BankConflictInfo &Result);

        // Check for coalesced/vectorized accesses
        bool isCoalescedAccess(llvm::Instruction *I);

    public:
        BankConflictAnalyzer(llvm::ScalarEvolution &SE, llvm::LoopInfo &LI)
            : SE(SE), LI(LI), HBMConfig(HBMConfiguration::HBM2()) {}

        // Set a specific HBM configuration
        void setHBMConfiguration(const HBMConfiguration &Config)
        {
            HBMConfig = Config;
        }

        // Main analysis method: analyze memory access pattern for potential bank conflicts
        BankConflictInfo analyzeBankConflicts(llvm::Value *Ptr, llvm::Loop *L);

        // Analyze conflicts across a function for a given pointer
        BankConflictInfo analyzeFunctionBankConflicts(llvm::Value *Ptr, llvm::Function &F);

        // Estimate performance impact of detected conflicts
        double estimateConflictImpact(const BankConflictInfo &Info);

        // Suggest address mapping transformations to reduce conflicts
        llvm::Value *generateConflictFreeAccess(llvm::IRBuilder<> &Builder,
                                                llvm::Value *OriginalPtr,
                                                const BankConflictInfo &Info);

        // Static helper to determine if a stride value likely causes bank conflicts
        static bool isConflictingStride(int64_t stride, unsigned numBanks);
    };

} // namespace MyHBM

#endif // BANKCONFLICTANALYZER_H