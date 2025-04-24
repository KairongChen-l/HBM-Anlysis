#ifndef MYHBM_LOOP_UTILS_H
#define MYHBM_LOOP_UTILS_H

#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"

namespace MyHBM
{

    
    namespace LoopUtils
    {

        // 检查循环是否被标记为可向量化
        bool isLoopMarkedVectorizable(const llvm::Loop *L);
        
        // 获取循环迭代次数
        uint64_t getLoopTripCount(llvm::Loop *L, llvm::ScalarEvolution &SE);

    } // namespace LoopUtils
} // namespace MyHBM

#endif // MYHBM_LOOP_UTILS_H
