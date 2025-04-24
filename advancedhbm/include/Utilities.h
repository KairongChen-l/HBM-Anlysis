#ifndef MYHBM_UTILITIES_H
#define MYHBM_UTILITIES_H

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include <string>

namespace MyHBM
{
    namespace Utilities
    {

        // 获取指令的源代码位置
        std::string getSourceLocation(llvm::Instruction *I);

        // 获取访问大小
        uint64_t getAccessSize(llvm::Instruction *I);

        // 检查值是否为必然正数
        bool isGuaranteedPositive(llvm::Value *V);

    } // namespace Utilities
} // namespace MyHBM
#endif // MYHBM_UTILITIES_H