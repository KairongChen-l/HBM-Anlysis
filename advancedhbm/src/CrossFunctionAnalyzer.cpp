#include "CrossFunctionAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"
#include <queue>
#include <unordered_set>
#include <algorithm>

#define DEBUG_TYPE "cross-function-analyzer"
using namespace llvm;
using namespace MyHBM;

// 分析跨函数使用情况
CrossFunctionInfo CrossFunctionAnalyzer::analyzeCrossFunctionUsage(Value *AllocPtr, Module &M)
{
    CrossFunctionInfo Result;
    if (!AllocPtr)
    {
        LLVM_DEBUG(dbgs() << "Null allocation pointer provided to analyzeCrossFunctionUsage\n");
        return Result;
    }
    Result.analyzedCrossFn = true;

    // 获取包含分配指令的函数
    Function *AllocFunc = nullptr;
    if (auto *I = dyn_cast<Instruction>(AllocPtr))
    {
        AllocFunc = I->getFunction();
    }

    if (!AllocFunc)
    {
        LLVM_DEBUG(dbgs() << "Could not determine allocation function for: "
                          << *AllocPtr << "\n");
        return Result;
    }

    LLVM_DEBUG(dbgs() << "Analyzing cross-function usage for allocation in function: "
                      << AllocFunc->getName() << "\n");
    // 1. 找出指针传递到的所有函数（被调用函数）
    std::set<Function *> VisitedFuncs;
    trackPointerToFunction(AllocPtr, AllocFunc, VisitedFuncs, Result.calledFunctions);

    // 2. 找出调用者函数（向上追溯）
    std::set<Function *> CallerSet; // 用于避免重复
    for (auto &F : M)
    {
        for (auto &BB : F)
        {
            for (auto &I : BB)
            {
                if (auto *Call = dyn_cast<CallInst>(&I))
                {
                    // 检查是否调用包含分配的函数
                    if (Call->getCalledFunction() == AllocFunc && !CallerSet.count(&F))
                    {
                        Result.callerFunctions.push_back(&F);
                        CallerSet.insert(&F);
                        // 可以进一步递归上溯调用链...
                    }
                }
            }
        }
    }

    // 3. 评估跨函数的影响

    // 检查是否传递给了外部函数
    for (Function *F : Result.calledFunctions)
    {
        if (F->isDeclaration())
        {
            Result.isPropagatedToExternalFunc = true;
            break;
        }
    }

    // 检查是否被热函数使用
    for (Function *F : Result.calledFunctions)
    {
        if (isHotFunction(F))
        {
            Result.isUsedInHotFunction = true;
            break;
        }
    }

    // 计算跨函数分数
    if (Result.calledFunctions.empty())
    {
        // 仅限本地使用
        Result.crossFuncScore = 5.0;
    }
    else if (Result.isPropagatedToExternalFunc)
    {
        // 传递给外部函数，增加不确定性
        Result.crossFuncScore = 2.0;
    }
    else if (Result.isUsedInHotFunction)
    {
        // 传递给热函数，可能更需要HBM
        Result.crossFuncScore = 15.0;
    }
    else
    {
        // 传递给其他内部函数
        Result.crossFuncScore = 8.0 + Result.calledFunctions.size() * 1.5;
    }

    return Result;
}
// 追踪指针传递到的函数
bool CrossFunctionAnalyzer::trackPointerToFunction(Value *Ptr,
                                                   Function *F,
                                                   std::set<Function *> &VisitedFuncs,
                                                   std::vector<Function *> &TargetFuncs)
{
    if (!Ptr || !F || VisitedFuncs.count(F))
        return false;

    VisitedFuncs.insert(F);
    bool Found = false;

    // 追踪指针在函数内的使用
    for (auto &BB : *F)
    {
        for (auto &I : BB)
        {
            // 检查是否将指针作为参数传递给其他函数
            if (auto *Call = dyn_cast<CallInst>(&I))
            {
                Function *Callee = Call->getCalledFunction();
                if (!Callee)
                    continue;

                // 检查每个参数
                for (unsigned i = 0; i < Call->arg_size(); ++i)
                {
                    Value *Arg = Call->getArgOperand(i);

                    // 如果参数是指针或其衍生（例如GEP结果）
                    if (Arg == Ptr || isPtrDerivedFrom(Arg, Ptr))
                    {
                        if (std::find(TargetFuncs.begin(), TargetFuncs.end(), Callee) == TargetFuncs.end())
                        {
                            TargetFuncs.push_back(Callee);
                        }
                        Found = true;

                        // 递归追踪到被调用函数
                        if (!Callee->isDeclaration())
                        {
                            // 获取对应的形参
                            if (i < Callee->arg_size())
                            {
                                Argument *FormalArg = Callee->getArg(i);
                                trackPointerToFunction(FormalArg, Callee, VisitedFuncs, TargetFuncs);
                            }
                        }
                    }
                }
            }
        }
    }

    return Found;
}

// 判断一个函数是否是热函数
bool CrossFunctionAnalyzer::isHotFunction(Function *F)
{
    if (!F)
        return false;

    // 检查函数属性
    if (F->hasFnAttribute("hot"))
        return true;

    // 检查函数名提示
    if (F->getName().contains("hot") ||
        F->getName().contains("main") ||
        F->getName().contains("kernel"))
        return true;

    // 检查Profile数据
    if (MDNode *ProfMD = F->getMetadata("prof.count"))
    {
        if (ProfMD->getNumOperands() > 0)
        {
            if (auto *CountMD = dyn_cast<ConstantAsMetadata>(ProfMD->getOperand(0)))
            {
                if (auto *Count = dyn_cast<ConstantInt>(CountMD->getValue()))
                {
                    // 假设阈值为1000
                    return Count->getZExtValue() > 1000;
                }
            }
        }
    }

    return false;
}

// 辅助函数：检查一个指针是否派生自另一个指针
bool CrossFunctionAnalyzer::isPtrDerivedFrom(Value *Derived, Value *Base)
{
    // Check null pointers first
    if (!Derived || !Base)
        return false;
        
    if (Derived == Base)
        return true;

    if (auto *GEP = dyn_cast<GetElementPtrInst>(Derived))
    {
        Value *PtrOp = GEP->getPointerOperand();
        if (!PtrOp)
            return false;
        return isPtrDerivedFrom(PtrOp, Base);
    }
    else if (auto *BC = dyn_cast<BitCastInst>(Derived))
    {
        Value *Op = BC->getOperand(0);
        if (!Op)
            return false;
        return isPtrDerivedFrom(Op, Base);
    }
    else if (auto *ASCI = dyn_cast<AddrSpaceCastInst>(Derived))
    {
        Value *PtrOp = ASCI->getPointerOperand();
        if (!PtrOp)
            return false;
        return isPtrDerivedFrom(PtrOp, Base);
    }

    return false;
}

