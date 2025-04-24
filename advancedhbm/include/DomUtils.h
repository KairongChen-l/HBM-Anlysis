#ifndef MYHBM_DOM_UTILS_H
#define MYHBM_DOM_UTILS_H

#include "llvm/IR/BasicBlock.h"

namespace MyHBM
{
    namespace DomUtils
    {

        // 判断A是否支配B
        bool dominates(llvm::BasicBlock *A, llvm::BasicBlock *B);

        // 检查从From到To是否可达
        bool isPotentiallyReachableFromTo(llvm::BasicBlock *From, llvm::BasicBlock *To,
                                          void *domTree, void *postDomTree, bool exact);

    } // namespace DomUtils
} // namespace MyHBM

#endif // MYHBM_DOM_UTILS_H