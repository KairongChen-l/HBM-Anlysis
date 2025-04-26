#ifndef BANKCONFLICTANALYZER_H
#define BANKCONFLICTANALYZER_H

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

    // Represents the bank conflict pattern severity and type
    enum class BankConflictSeverity
    {
        NONE,     // No conflicts detected
        LOW,      // Occasional conflicts
        MODERATE, // Regular conflicts but manageable
        HIGH,     // Frequent conflicts with substantial impact
        SEVERE    // Critical conflicts, severe performance impact
    };

    // Specific types of bank conflicts
    enum class BankConflictType
    {
        NONE,               // No conflicts
        SAME_BANK_ACCESS,   // Multiple accesses to same bank
        STRIDED_CONFLICT,   // Regular access pattern causing conflicts
        RANDOM_CONFLICT,    // Unpredictable access pattern with conflicts
        PARTIAL_ROW_ACCESS, // Accessing parts of a row inefficiently
        CHANNEL_IMBALANCE   // Uneven distribution across channels
    };

    // Detailed bank conflict information
    struct BankConflictInfo
    {
        BankConflictSeverity severity = BankConflictSeverity::NONE;
        BankConflictType type = BankConflictType::NONE;
        unsigned affectedBanks = 0;          // Number of banks with conflicts
        unsigned totalAccessedBanks = 0;     // Total number of banks accessed
        double conflictRate = 0.0;           // Estimated percentage of conflicting accesses
        double performanceImpact = 0.0;      // Estimated slowdown factor (1.0 = no impact)
        bool hasBankingFunction = false;     // Whether address mapping function was determined
        double conflictScore = 0.0;          // Score for HBM suitability (negative = worse)
        std::vector<unsigned> bankHistogram; // Distribution of accesses across banks

        // For reporting
        std::string analysisDescription;

        BankConflictInfo()
        {
            // Initialize bank histogram with zeros for all banks
            bankHistogram.resize(32, 0); // Assuming 32 banks in HBM2
        }
    };

    // Parameters for different HBM configurations
    struct HBMConfiguration
    {
        unsigned numBanks;    // Number of banks
        unsigned numChannels; // Number of channels
        unsigned rowSize;     // Size of a row in bytes
        unsigned bankXORBits; // Bits used for XOR banking function
        unsigned channelBits; // Bits used for channel selection
        unsigned addressMask; // Address bits that matter for conflicts

        HBMConfiguration() : numBanks(32),
                             numChannels(8),
                             rowSize(1024),
                             bankXORBits(7),
                             channelBits(3),
                             addressMask(0x7FFF) {} // 15 bits for bank+row addressing

        // HBM2 configuration
        static HBMConfiguration HBM2()
        {
            HBMConfiguration cfg;
            cfg.numBanks = 32;
            cfg.numChannels = 8;
            cfg.rowSize = 1024;
            cfg.bankXORBits = 7;
            cfg.channelBits = 3;
            cfg.addressMask = 0x7FFF;
            return cfg;
        }

        // HBM3 configuration (adjust as specifications evolve)
        static HBMConfiguration HBM3()
        {
            HBMConfiguration cfg;
            cfg.numBanks = 64;
            cfg.numChannels = 16;
            cfg.rowSize = 1024;
            cfg.bankXORBits = 8;
            cfg.channelBits = 4;
            cfg.addressMask = 0xFFFF;
            return cfg;
        }
    };

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