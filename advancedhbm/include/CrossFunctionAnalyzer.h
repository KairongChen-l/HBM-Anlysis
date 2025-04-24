#ifndef MYHBM_CROSS_FUNCTION_ANALYZER_H
#define MYHBM_CROSS_FUNCTION_ANALYZER_H

#include "llvm/IR/Value.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "AnalysisTypes.h"
#include <set>
#include <vector>

namespace MyHBM
{

    // 跨函数分析器
    class CrossFunctionAnalyzer
    {
    public:
        CrossFunctionAnalyzer() = default;

        // 分析跨函数使用情况
        CrossFunctionInfo analyzeCrossFunctionUsage(llvm::Value *AllocPtr, llvm::Module &M);

        // 追踪指针传递到的函数
        bool trackPointerToFunction(
            llvm::Value *Ptr,
            llvm::Function *F,
            std::set<llvm::Function *> &VisitedFuncs,
            std::vector<llvm::Function *> &TargetFuncs);

        // 判断一个函数是否是热函数
        bool isHotFunction(llvm::Function *F);

        // 辅助函数：检查一个指针是否派生自另一个指针
        bool isPtrDerivedFrom(llvm::Value *Derived, llvm::Value *Base);
    };

} // namespace MyHBM

#endif // MYHBM_CROSS_FUNCTION_ANALYZER_H