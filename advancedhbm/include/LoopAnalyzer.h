#ifndef MYHBM_LOOP_ANALYZER_H
#define MYHBM_LOOP_ANALYZER_H

#include "llvm/IR/Value.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "AnalysisTypes.h"

namespace MyHBM {

// 循环分析器
class LoopAnalyzer {
public:
  LoopAnalyzer(llvm::ScalarEvolution &SE, llvm::LoopInfo &LI)
    : SE(SE), LI(LI) {}

  // 获取循环的估计迭代次数
  uint64_t getLoopTripCount(llvm::Loop *L);
  
  // 分析嵌套循环的访存特性
  double analyzeNestedLoops(llvm::Loop *L, llvm::Value *Ptr);
  
  // 检查循环是否为内存密集型
  bool isMemoryIntensiveLoop(llvm::Loop *L);
  
  // 计算循环嵌套结构得分
  double computeLoopNestingScore(llvm::Loop *L);
  
  // 分析数据访问的局部性
  LocalityType analyzeDataLocality(llvm::Value *Ptr, llvm::Loop *L);
  
  // 分析循环中的交错访问模式
  InterleavedAccessInfo analyzeInterleavedAccess(llvm::Loop *L);

private:
  llvm::ScalarEvolution &SE;
  llvm::LoopInfo &LI;
};

} // namespace MyHBM

#endif // MYHBM_LOOP_ANALYZER_H