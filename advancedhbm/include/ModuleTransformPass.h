#ifndef MYHBM_MODULE_TRANSFORM_PASS_H
#define MYHBM_MODULE_TRANSFORM_PASS_H

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/JSON.h"
#include "MallocRecord.h"
#include <string>

namespace MyHBM
{
    // Forward declaration
    struct AdaptiveThresholdInfo;

    class ModuleTransformPass : public llvm::PassInfoMixin<ModuleTransformPass>
    {
    public:
        ModuleTransformPass() = default;

        // 主要转换入口点
        llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);

    private:
        // 从外部Profile文件加载性能数据
        void loadExternalProfile(llvm::Module &M, llvm::SmallVectorImpl<MallocRecord *> &AllMallocs);

        // 处理分析结果，执行转换（替换malloc调用为HBM版本）
        void processMallocRecords(llvm::Module &M, llvm::SmallVectorImpl<MallocRecord *> &AllMallocs);

        // 生成JSON分析报告
        void generateReport(const llvm::Module &M, llvm::ArrayRef<MallocRecord *> AllMallocs, bool JSONOutput);

        // 获取调用指令的源代码位置
        std::string getSourceLocation(llvm::CallInst *CI);

        // 创建JSON对象
        llvm::json::Object createMallocRecordJSON(const MallocRecord *MR, bool includeExtendedInfo = true);

        // 默认HBM容量常量
        static constexpr uint64_t DefaultHBMCapacity = (1ULL << 30) * 4; // 4GB
    };

} // namespace MyHBM

#endif // MYHBM_MODULE_TRANSFORM_PASS_H