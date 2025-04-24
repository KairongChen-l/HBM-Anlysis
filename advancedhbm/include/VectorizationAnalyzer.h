#ifndef MYHBM_VECTORIZATION_ANALYZER_H
#define MYHBM_VECTORIZATION_ANALYZER_H

#include "llvm/IR/Instruction.h"
#include "llvm/IR/Type.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/LoopInfo.h"
#include <set>

namespace MyHBM
{

  // 向量化分析器
  class VectorizationAnalyzer
  {
  public:
    // 构造函数
    VectorizationAnalyzer(llvm::ScalarEvolution &SE) : SE(SE) {}

    // 检查指令是否使用向量类型或SIMD操作
    bool isVectorizedInstruction(llvm::Instruction *I);

    // 获取向量类型的宽度
    int getVectorWidth(llvm::Type *Ty);

    // 检测函数中是否有SIMD内部函数调用
    bool detectSIMDIntrinsics(llvm::Function &F);

    // 检查循环是否显示出向量化模式
    bool isVectorLoopPattern(llvm::Loop *L);

    // 递归检查值是否参与了向量操作
    bool hasVectorOperations(llvm::Value *V, std::set<llvm::Value *> &Visited);

  private:
    llvm::ScalarEvolution &SE;
  };

} // namespace MyHBM

#endif // MYHBM_VECTORIZATION_ANALYZER_H