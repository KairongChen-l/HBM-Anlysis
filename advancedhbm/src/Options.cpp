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

        // Initialize all options
        void initializeOptions()
        {
            // All options are initialized with their defaults via cl::init
            // Additional dynamic initialization could be added here if needed
        }

    } // namespace Options
} // namespace MyHBM