#include "Options.h"

using namespace llvm;

namespace MyHBM
{
    namespace Options
    {

        // 命令行选项的外部声明 - 实际定义在 HBMPlugin.cpp 中
        extern cl::opt<double> HBMThreshold;
        extern cl::opt<double> ParallelBonus;
        extern cl::opt<double> StreamBonus;
        extern cl::opt<double> VectorBonus;
        extern cl::opt<double> AccessBaseRead;
        extern cl::opt<double> AccessBaseWrite;
        extern cl::opt<double> BandwidthScale;
        extern cl::opt<bool> AnalysisOnly;
        extern cl::opt<std::string> HBMReportFile;
        //extern cl::opt<std::string> ExternalProfileFile;

        // 初始化所有选项
        void initializeOptions()
        {
            // 目前不需要额外初始化操作
            // 命令行选项已通过全局变量自动注册
        }

    } // namespace Options
} // namespace MyHBM