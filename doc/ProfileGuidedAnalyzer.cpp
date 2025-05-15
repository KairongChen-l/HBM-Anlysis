#include "ProfileGuidedAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Metadata.h"
#include <cmath>

using namespace llvm;
using namespace MyHBM;

// 分析Profile数据
ProfileGuidedInfo ProfileGuidedAnalyzer::analyzeProfileData(CallInst *MallocCall, Function &F)
{
  ProfileGuidedInfo Result;
  if (!MallocCall)
    return Result;

  // 检查是否有Profile元数据
  bool hasMetadata = false;
  if (MDNode *ProfMD = MallocCall->getMetadata("prof.memusage"))
  {
    hasMetadata = true;
    Result.hasProfileData = true;

    // 提取内存使用频率数据
    if (ProfMD->getNumOperands() > 0)
    {
      if (auto *Op = dyn_cast<ConstantAsMetadata>(ProfMD->getOperand(0)))
      {
        if (auto *CInt = dyn_cast<ConstantInt>(Op->getValue()))
        {
          uint64_t usage = CInt->getZExtValue();
          Result.dynamicWeight = std::log2(double(usage) + 1.0) / 20.0; // 归一化
        }
      }
    }
  }

  // 检查基于块频率的额外Profile信息
  if (MDNode *BlockFreqMD = F.getMetadata("prof.block.frequency"))
  {
    Result.hasProfileData = true;

    // 找到malloc所在的基本块
    BasicBlock *MallocBB = MallocCall->getParent();

    // 检查该基本块是否是热点
    if (F.getEntryCount().has_value())
    {
      auto EntryCount = F.getEntryCount().value();

      // 如果有基本块执行计数
      if (BlockFreqMD->getNumOperands() > 0)
      {
        for (unsigned i = 0; i < BlockFreqMD->getNumOperands(); ++i)
        {
          auto *BlockFreqPair = dyn_cast<MDNode>(BlockFreqMD->getOperand(i));
          if (!BlockFreqPair || BlockFreqPair->getNumOperands() < 2)
            continue;

          // 提取基本块ID和频率
          if (auto *BBMD = dyn_cast<ValueAsMetadata>(BlockFreqPair->getOperand(0)))
          {
            if (auto *BB = dyn_cast<BasicBlock>(BBMD->getValue()))
            {
              if (BB == MallocBB)
              {
                if (auto *FreqMD = dyn_cast<ConstantAsMetadata>(BlockFreqPair->getOperand(1)))
                {
                  if (auto *Freq = dyn_cast<ConstantInt>(FreqMD->getValue()))
                  {
                    // 计算该基本块的热度相对于入口块
                    uint64_t BBCount = Freq->getZExtValue();
                    double relativeHeat = double(BBCount) / double(EntryCount.getCount());

                    Result.hotspotHints.push_back(
                        {"block_relative_heat", relativeHeat});

                    // 根据热度调整动态权重
                    Result.dynamicWeight = std::max(Result.dynamicWeight, relativeHeat * 0.5);
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  // 分析主循环出现的频率
  for (auto &BB : F)
  {
    for (auto &I : BB)
    {
      if (auto *CI = dyn_cast<CallInst>(&I))
      {
        // 检查是否有与特定循环相关的Profile注解
        if (CI->getMetadata("prof.loop.iterations"))
        {
          // 找到迭代次数很多的循环
          MDNode *LoopMD = CI->getMetadata("prof.loop.iterations");
          if (LoopMD->getNumOperands() > 0)
          {
            if (auto *IterMD = dyn_cast<ConstantAsMetadata>(LoopMD->getOperand(0)))
            {
              if (auto *Iters = dyn_cast<ConstantInt>(IterMD->getValue()))
              {
                uint64_t iterations = Iters->getZExtValue();

                // 高迭代次数的循环是潜在热点
                if (iterations > 1000)
                {
                  Result.hotspotHints.push_back(
                      {"high_iteration_loop", double(iterations) / 10000.0});
                }
              }
            }
          }
        }
      }
    }
  }

  // 基于静态vs动态结果的一致性计算信心度
  if (Result.hasProfileData)
  {
    // 假设我们之前计算了静态分数，范围是0-100
    // 我们检查静态和动态结果是否一致

    // 例如，如果MallocCall周围有很多LoadInst/StoreInst，
    // 但动态Profile显示使用频率低，则降低信心
    unsigned staticMemOpCount = 0;
    BasicBlock *BB = MallocCall->getParent();
    for (auto &I : *BB)
    {
      if (isa<LoadInst>(I) || isa<StoreInst>(I))
      {
        staticMemOpCount++;
      }
    }

    if (staticMemOpCount > 5 && Result.dynamicWeight < 0.2)
    {
      // 静态分析显示内存操作多，但动态访问少
      Result.staticConfidence = 0.5; // 降低信心
    }
    else if (staticMemOpCount < 3 && Result.dynamicWeight > 0.5)
    {
      // 静态分析显示内存操作少，但动态访问多
      Result.staticConfidence = 0.6; // 适当降低信心
    }
    else
    {
      // 静态和动态分析基本一致
      Result.staticConfidence = 0.8; // 较高信心
    }
  }
  else
  {
    // 没有Profile数据时，默认中等信心
    Result.staticConfidence = 0.7;
  }

  return Result;
}

// 使用Profile数据调整分数
double ProfileGuidedAnalyzer::adjustScoreWithProfile(double staticScore, const ProfileGuidedInfo &PGI)
{
  if (!PGI.hasProfileData)
  {
    return staticScore; // 没有Profile数据时不调整
  }

  double adjustedScore = staticScore;

  // 根据信心度混合静态和动态分数
  if (PGI.dynamicWeight > 0.0)
  {
    // 使用动态权重估算动态分数 (0-100范围)
    double dynamicScore = PGI.dynamicWeight * 100.0;

    // 混合静态和动态分数，基于信心度
    adjustedScore = staticScore * PGI.staticConfidence +
                    dynamicScore * (1.0 - PGI.staticConfidence);
  }

  // 应用额外的热点提示
  for (const auto &hint : PGI.hotspotHints)
  {
    if (hint.first == "block_relative_heat" && hint.second > 0.5)
    {
      // 如果基本块是热点，提高分数
      adjustedScore *= (1.0 + hint.second * 0.3);
    }
    else if (hint.first == "high_iteration_loop" && hint.second > 0.1)
    {
      // 如果在高迭代次数循环中，提高分数
      adjustedScore *= (1.0 + hint.second * 0.5);
    }
  }

  return adjustedScore;
}

// 计算自适应阈值
AdaptiveThresholdInfo ProfileGuidedAnalyzer::computeAdaptiveThreshold(
    Module &M,
    const std::vector<MallocRecord> &AllMallocs)
{

  AdaptiveThresholdInfo Result;

  // 默认基础阈值
  Result.baseThreshold = 50.0; // MyHBMOptions::HBMThreshold;
  Result.adjustedThreshold = Result.baseThreshold;

  // 没有数据时返回默认值
  if (AllMallocs.empty())
  {
    Result.adjustmentReason = "Using default threshold due to no malloc records";
    return Result;
  }

  // 1. 分析分数分布
  std::vector<double> Scores;
  double TotalScore = 0.0;
  double MaxScore = 0.0;
  uint64_t TotalSize = 0;

  for (const auto &MR : AllMallocs)
  {
    Scores.push_back(MR.Score);
    TotalScore += MR.Score;
    MaxScore = std::max(MaxScore, MR.Score);
    TotalSize += MR.AllocSize;
  }

  // 计算分数的平均值和标准差
  double MeanScore = TotalScore / Scores.size();

  double Variance = 0.0;
  for (double score : Scores)
  {
    Variance += (score - MeanScore) * (score - MeanScore);
  }
  Variance /= Scores.size();
  double StdDev = std::sqrt(Variance);

  // 2. 根据程序特性调整阈值

  // 检查HBM容量限制
  uint64_t HBMCapacity = 1ULL << 30; // 默认1GB
  double SizeRatio = (double)TotalSize / HBMCapacity;

  if (SizeRatio > 0.8)
  {
    // 总分配大小接近或超过HBM容量，提高阈值
    double increaseFactor = std::min(1.5, 0.5 + SizeRatio);
    Result.adjustedThreshold = Result.baseThreshold * increaseFactor;
    Result.adjustmentReason = "Increased threshold due to large total allocation size";
    return Result;
  }

  // 检查分数分布
  if (StdDev < 10.0 && MeanScore > 30.0)
  {
    // 所有分数比较接近，且平均较高，降低阈值
    Result.adjustedThreshold = std::max(30.0, MeanScore - StdDev);
    Result.adjustmentReason = "Decreased threshold due to clustered high scores";
  }
  else if (StdDev > 30.0)
  {
    // 分数分布很分散，使用统计方法找出合适的阈值
    // 例如，使用平均值+0.5*标准差
    Result.adjustedThreshold = MeanScore + 0.5 * StdDev;
    Result.adjustmentReason = "Adjusted threshold based on score distribution";
  }
  else if (MaxScore < 60.0)
  {
    // 最高分都不太高，适当降低阈值
    Result.adjustedThreshold = std::max(30.0, MaxScore * 0.8);
    Result.adjustmentReason = "Decreased threshold due to overall low scores";
  }

  // 3. 检查模块特性
  bool HasParallelCode = false;
  bool HasVectorizedCode = false;

  for (auto &F : M)
  {
    // 检查是否有并行或向量化特征
    if (F.hasFnAttribute("parallel") || F.getName().contains("parallel"))
    {
      HasParallelCode = true;
    }
    if (F.hasFnAttribute("vector") || F.getName().contains("simd"))
    {
      HasVectorizedCode = true;
    }
  }

  // 根据程序特性最后调整
  if (HasParallelCode && HasVectorizedCode)
  {
    // 并行+向量化代码更可能从HBM受益，降低阈值
    Result.adjustedThreshold *= 0.8;
    Result.adjustmentReason += ", further decreased for parallel+vectorized code";
  }
  else if (!HasParallelCode && !HasVectorizedCode)
  {
    // 既不并行也不向量化，提高阈值
    Result.adjustedThreshold *= 1.2;
    Result.adjustmentReason += ", increased for sequential scalar code";
  }

  // 确保阈值在合理范围内
  Result.adjustedThreshold = std::max(20.0, std::min(80.0, Result.adjustedThreshold));

  return Result;
}

// 计算多维度评分
MultiDimensionalScore ProfileGuidedAnalyzer::computeMultiDimensionalScore(const MallocRecord &MR)
{
  MultiDimensionalScore Result;

  // 1. 带宽需求得分 - 基于流式/向量化/并行特性
  double bandwidthBase = 0.0;

  // 加权考虑各种带宽影响因素
  if (MR.IsStreamAccess)
    bandwidthBase += 30.0;
  if (MR.IsVectorized)
    bandwidthBase += 20.0;
  if (MR.IsParallel)
  {
    if (MR.IsThreadPartitioned)
    {
      bandwidthBase += 25.0; // 良好的并行分区
    }
    else if (MR.MayConflict)
    {
      bandwidthBase += 10.0; // 有冲突的并行
    }
    else
    {
      bandwidthBase += 15.0; // 一般并行
    }
  }

  // 考虑循环深度和迭代次数
  double loopFactor = 1.0;
  if (MR.LoopDepth > 0)
  {
    loopFactor += 0.2 * MR.LoopDepth;

    // 考虑循环迭代次数
    if (MR.TripCount > 1)
    {
      loopFactor += 0.3 * std::log2(double(MR.TripCount));
    }
  }

  // 考虑动态访问计数
  double dynamicFactor = 1.0;
  if (MR.DynamicAccessCount > 0)
  {
    dynamicFactor += 0.2 * std::log2(double(MR.DynamicAccessCount + 1) / 1000.0);
  }

  // 计算最终带宽得分
  Result.bandwidthScore = bandwidthBase * loopFactor * dynamicFactor;

  // 2. 延迟敏感度得分 - 基于依赖关系和访问模式
  double latencyBase = 0.0;

  // 非规则访问通常更受延迟影响
  if (!MR.IsStreamAccess)
  {
    latencyBase += 20.0;
  }

  // 复杂的内存访问模式可能表明延迟敏感
  latencyBase += MR.ChaosPenalty * 3.0;

  // MemorySSA结构复杂性可能表明延迟敏感
  latencyBase += MR.SSAPenalty * 4.0;

  // 计算最终延迟得分
  Result.latencyScore = latencyBase * loopFactor;

  // 3. 利用率得分 - 评估HBM带宽利用效率
  double utilizationBase = 50.0; // 默认中等效率

  // 流式访问通常能更好地利用HBM带宽
  if (MR.IsStreamAccess)
  {
    utilizationBase += 20.0;
  }

  // 向量化访问可以提高带宽利用率
  if (MR.IsVectorized)
  {
    utilizationBase += 15.0;
  }

  // 随机访问会降低带宽利用率
  if (MR.ChaosPenalty > 0)
  {
    utilizationBase -= MR.ChaosPenalty * 5.0;
  }

  // 计算最终利用率得分
  Result.utilizationScore = utilizationBase;

  // 4. 大小效率得分 - 考虑分配大小与访问频率的比例
  double sizeEfficiencyBase = 50.0; // 默认中等效率

  if (MR.AllocSize > 0 && MR.DynamicAccessCount > 0)
  {
    // 计算每字节的访问次数
    double accessesPerByte = double(MR.DynamicAccessCount) / MR.AllocSize;

    // 访问密度高的分配效率更高
    if (accessesPerByte > 10.0)
    {
      sizeEfficiencyBase += 30.0;
    }
    else if (accessesPerByte > 1.0)
    {
      sizeEfficiencyBase += 20.0;
    }
    else if (accessesPerByte > 0.1)
    {
      sizeEfficiencyBase += 10.0;
    }
    else
    {
      // 访问密度低，效率不高
      sizeEfficiencyBase -= 10.0;
    }
  }

  // 分配大小过大会降低效率
  if (MR.AllocSize > 100 * 1024 * 1024)
  { // 100MB
    sizeEfficiencyBase -= 20.0;
  }
  else if (MR.AllocSize > 10 * 1024 * 1024)
  { // 10MB
    sizeEfficiencyBase -= 10.0;
  }

  // 计算最终大小效率得分
  Result.sizeEfficiencyScore = sizeEfficiencyBase;

  // 5. 计算最终综合得分
  // 使用加权平均
  Result.finalScore =
      0.4 * Result.bandwidthScore +     // 带宽是最重要的因素
      0.2 * Result.latencyScore +       // 延迟次之
      0.3 * Result.utilizationScore +   // 利用率也很重要
      0.1 * Result.sizeEfficiencyScore; // 大小效率影响相对较小

  return Result;
}