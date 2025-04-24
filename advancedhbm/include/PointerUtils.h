#ifndef MYHBM_POINTER_UTILS_H
#define MYHBM_POINTER_UTILS_H

#include "llvm/IR/Value.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include <set>
#include <optional>

namespace MyHBM
{
    namespace PointerUtils
    {

        // 解析基础指针 (解析 GEP、BitCast 等)
        llvm::Value *resolveBasePointer(llvm::Value *V);

        // 检查指针是否可能从内存加载
        bool isMayLoadFromMemory(llvm::Value *V);

        // 检查指针是否被函数调用访问
        bool isPointerAccessedByCall(llvm::CallInst *Call, llvm::Value *Ptr, llvm::AAResults &AA);

        // 检查是否为线程本地存储
        bool isThreadLocalStorage(llvm::Value *Ptr);

        // 从Value中提取常量大小
        std::optional<uint64_t> getConstantAllocSize(llvm::Value *V, std::set<llvm::Value *> &Visited);

        // 方便的包装方法
        uint64_t getConstantAllocSize(llvm::Value *V);

    } // namespace PointerUtils
} // namespace MyHBM

#endif // MYHBM_POINTER_UTILS_H