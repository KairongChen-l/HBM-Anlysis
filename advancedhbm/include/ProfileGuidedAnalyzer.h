#ifndef MYHBM_PROFILE_GUIDED_ANALYZER_H
#define MYHBM_PROFILE_GUIDED_ANALYZER_H

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "AnalysisTypes.h"
#include "MallocRecord.h"
#include <vector>

namespace MyHBM
{

    // Profile引导分析器
    class ProfileGuidedAnalyzer
    {
    public:
        ProfileGuidedAnalyzer() = default;

        // 分析Profile数据
        ProfileGuidedInfo analyzeProfileData(llvm::CallInst *MallocCall, llvm::Function &F);

        // 使用Profile数据调整分数
        double adjustScoreWithProfile(double staticScore, const ProfileGuidedInfo &PGI);

        // 计算自适应阈值
        AdaptiveThresholdInfo computeAdaptiveThreshold(
            llvm::Module &M,
            const std::vector<MallocRecord> &AllMallocs);

        // 计算多维度评分
        MultiDimensionalScore computeMultiDimensionalScore(const MallocRecord &MR);
    };

} // namespace MyHBM

#endif // MYHBM_PROFILE_GUIDED_ANALYZER_H