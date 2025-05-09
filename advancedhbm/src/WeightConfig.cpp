#include "WeightConfig.h"

using namespace llvm;

namespace MyHBM
{
    namespace WeightConfig
    {

        // Command-line configurable weights
        cl::opt<double> AccessBaseRead(
            "hbm-access-base-read",
            cl::desc("Base read score"),
            cl::init(5.0));

        cl::opt<double> AccessBaseWrite(
            "hbm-access-base-write",
            cl::desc("Base write score"),
            cl::init(8.0));

        cl::opt<double> StreamBonus(
            "hbm-stream-bonus",
            cl::desc("Extra score for streaming usage"),
            cl::init(10.0));

        cl::opt<double> VectorBonus(
            "hbm-vector-bonus",
            cl::desc("Extra score for vectorized usage"),
            cl::init(5.0));

        cl::opt<double> ParallelBonus(
            "hbm-parallel-bonus",
            cl::desc("Extra score for parallel usage"),
            cl::init(20.0));

        cl::opt<double> BandwidthScale(
            "hbm-bandwidth-scale",
            cl::desc("Scaling factor for estimated bandwidth usage"),
            cl::init(1.0));

        // Initialize any dynamic weight configurations
        void initializeWeights()
        {
            // Currently all weights are initialized with their defaults
            // This function can be expanded later if needed for dynamic adjustments
        }

    } // namespace WeightConfig
} // namespace MyHBM