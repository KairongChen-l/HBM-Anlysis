#include "Options.h"
#include "WeightConfig.h"

using namespace llvm;

namespace MyHBM
{
    namespace Options
    {

        // Command line options defined in HBMPlugin.cpp
        extern cl::opt<double> HBMThreshold;
        extern cl::opt<bool> AnalysisOnly;
        extern cl::opt<std::string> HBMReportFile;
        cl::opt<uint64_t> HBMCapacity(
            "hbm-capacity",
            cl::desc("Available HBM capacity in bytes"),
            cl::init(1ULL << 30)); // Default 1GB
        

        cl::opt<bool> IgnoreSmallAllocations(
            "hbm-ignore-small-allocs",
            cl::desc("Ignore allocations smaller than a threshold"),
            cl::init(true));
        
        
        // Initialize all options
        void initializeOptions()
        {
            // All options are initialized with their defaults via cl::init
            // Additional dynamic initialization could be added here if needed
        }

    } // namespace Options
} // namespace MyHBM