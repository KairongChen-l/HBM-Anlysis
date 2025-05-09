#ifndef MYHBM_OPTIONS_H
#define MYHBM_OPTIONS_H

#include "llvm/Support/CommandLine.h"

namespace MyHBM
{
    namespace Options
    {

        // 全局选项定义
        extern llvm::cl::opt<double> HBMThreshold;
        // extern llvm::cl::opt<double> ParallelBonus;
        // extern llvm::cl::opt<double> StreamBonus;
        // extern llvm::cl::opt<double> VectorBonus;
        // extern llvm::cl::opt<double> AccessBaseRead;
        // extern llvm::cl::opt<double> AccessBaseWrite;
        // extern llvm::cl::opt<double> BandwidthScale;
        extern llvm::cl::opt<bool> AnalysisOnly;
        // 报告和配置文件选项
        extern llvm::cl::opt<std::string> HBMReportFile;
        //extern llvm::cl::opt<std::string> ExternalProfileFile;

        // 初始化所有选项 - 在插件加载时调用
        void initializeOptions();

    } // namespace Options
} // namespace MyHBM

#endif // MYHBM_OPTIONS_H