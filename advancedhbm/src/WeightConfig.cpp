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
            cl::init(0.25));

        cl::opt<double> AccessBaseWrite(
            "hbm-access-base-write",
            cl::desc("Base write score"),
            cl::init(0.5));

        cl::opt<double> StreamBonus(
            "hbm-stream-bonus",
            cl::desc("Extra score for streaming usage"),
            cl::init(75.0));

        cl::opt<double> VectorBonus(
            "hbm-vector-bonus",
            cl::desc("Extra score for vectorized usage"),
            cl::init(5.0));

        cl::opt<double> ParallelBonus(
            "hbm-parallel-bonus",
            cl::desc("Extra score for parallel usage"),
            cl::init(5.0));

        cl::opt<double> BandwidthScale(
            "hbm-bandwidth-scale",
            cl::desc("Scaling factor for estimated bandwidth usage"),
            cl::init(2.0));

        // Initialize any dynamic weight configurations
        void initializeWeights()
        {
            // Now includes runtime validation that weights are in reasonable ranges
            if (StreamBonus.getValue() < 10.0)
                errs() << "Warning: StreamBonus value " << StreamBonus << " seems low. Recommended range is 50-100.\n";

            if (AccessBaseWrite.getValue() < AccessBaseRead.getValue())
                errs() << "Warning: Write access base score should typically be higher than read access.\n";

        }

    } // namespace WeightConfig
} // namespace MyHBM