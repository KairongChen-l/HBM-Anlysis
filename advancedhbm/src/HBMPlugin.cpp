#include "FunctionAnalysisPass.h"
#include "ModuleTransformPass.h"
#include "Options.h"
#include "WeightConfig.h" // Include the weight config header
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"

using namespace llvm;
using namespace MyHBM;

// 初始化所有的命令行选项
namespace MyHBM
{
    namespace Options
    {

        // 定义命令行选项
        cl::opt<double> HBMThreshold(
            "hbm-threshold",
            cl::desc("Score threshold for HBM usage"),
            cl::init(50.0));

        // These options are now defined in the WeightConfig namespace
        // They are declared and used from there
        
        cl::opt<std::string> HBMReportFile(
            "hbm-report-file",
            cl::desc("Path to write HBM analysis report file"),
            cl::init("report.json"));

        cl::opt<bool> AnalysisOnly(
            "hbm-analysis-only",
            cl::desc("Only perform analysis, do not transform code"),
            cl::init(false));


    } // namespace Options
} // namespace MyHBM

// 注册插件所需的所有分析和转换
void registerPlugin(llvm::PassBuilder &PB)
{
    // 注册函数级分析Pass
    PB.registerAnalysisRegistrationCallback(
        [](FunctionAnalysisManager &FAM)
        {
            FAM.registerPass([]
                             { return FunctionAnalysisPass(); });
            // 确保依赖的基础分析也被注册
            FAM.registerPass([]
                             { return LoopAnalysis(); });
            FAM.registerPass([]
                             { return ScalarEvolutionAnalysis(); });
            FAM.registerPass([]
                             { return MemorySSAAnalysis(); });
            FAM.registerPass([]
                             { return LoopAccessAnalysis(); });
        });

    // 注册模块级转换Pass
    PB.registerPipelineParsingCallback(
        [](StringRef Name, ModulePassManager &MPM,
           ArrayRef<PassBuilder::PipelineElement>)
        {
            if (Name == "hbm-transform")
            {
                MPM.addPass(ModuleTransformPass());
                return true;
            }
            return false;
        });

    
    /*把 Pass 插入默认优化 Pipeline 的末尾*/
    PB.registerOptimizerLastEPCallback(
        [](ModulePassManager &MPM, OptimizationLevel) {
             // 没设置 -hbm-analysis-only 才转换
            if (!MyHBM::Options::AnalysisOnly)
                MPM.addPass(ModuleTransformPass());
        });
}

// 初始化插件
void initializePlugin()
{
    Options::initializeOptions();
    WeightConfig::initializeWeights(); // Initialize the weight configuration
}

// 插件入口点 - 由LLVM调用
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo()
{
    return {
        .APIVersion = LLVM_PLUGIN_API_VERSION,
        .PluginName = "MyAdvancedHBMPlugin",
        .PluginVersion = LLVM_VERSION_STRING,
        .RegisterPassBuilderCallbacks = [](PassBuilder &PB)
        {
            initializePlugin();
            registerPlugin(PB);
        }};
}