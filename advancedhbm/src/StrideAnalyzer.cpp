#include "StrideAnalyzer.h"
#include "LoopUtils.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/LoopInfo.h"

using namespace llvm;
using namespace MyHBM;

// 分析GEP指令的步长类型
StrideType StrideAnalyzer::analyzeGEPStride(GetElementPtrInst *GEP)
{
    // errs() << "===== Function:analyzeGEPStride =====\n";
    if (!GEP)
        return StrideType::UNKNOWN;

    StrideType Result = StrideType::CONSTANT; // 默认假设是常量步长

    // 基地址必须是固定的
    Value *BasePtr = GEP->getPointerOperand();
    if (!isa<Argument>(BasePtr) && !isa<AllocaInst>(BasePtr) && !isa<GlobalValue>(BasePtr) &&
        !isa<CallInst>(BasePtr) && !isa<BitCastInst>(BasePtr) && !isa<LoadInst>(BasePtr))
    {
        // 添加更多可能的固定基址类型
        if (auto *BitCast = dyn_cast<BitCastInst>(BasePtr))
        {
            Value *SrcOp = BitCast->getOperand(0);
            if (isa<Argument>(SrcOp) || isa<AllocaInst>(SrcOp) || isa<GlobalValue>(SrcOp))
            {
                // 通过 BitCast 指向固定基址，可以继续分析
            }
            else
            {
                return StrideType::UNKNOWN; // 基地址不固定，无法确定步长
            }
        }
        else
        {
            return StrideType::UNKNOWN; // 基地址不固定，无法确定步长
        }
    }

    // 分析索引的类型
    bool HasVariableIndex = false;
    bool HasLinearIndex = false;
    bool HasComplexIndex = false;

    // 获取指向类型的大小信息
    Type *ElemTy = GEP->getSourceElementType();
    const DataLayout &DL = GEP->getModule()->getDataLayout();
    uint64_t ElemSize = DL.getTypeAllocSize(ElemTy);

    // 分析每个索引
    for (auto Idx = GEP->idx_begin(), E = GEP->idx_end(); Idx != E; ++Idx)
    {
        Value *IdxVal = *Idx;

        // 跳过常量索引
        if (isa<ConstantInt>(IdxVal))
            continue;

        HasVariableIndex = true;

        // 使用 ScalarEvolution 分析索引的变化模式
        if (SE.isSCEVable(IdxVal->getType()))
        {
            const SCEV *IdxSCEV = SE.getSCEV(IdxVal);

            // 检查是否为线性变化 (i = i + k)
            if (auto *AR = dyn_cast<SCEVAddRecExpr>(IdxSCEV))
            {
                if (AR->isAffine())
                {
                    const SCEV *Step = AR->getStepRecurrence(SE);
                    // 更详细地分析步长
                    if (auto *SC = dyn_cast<SCEVConstant>(Step))
                    {
                        int64_t StepVal = SC->getValue()->getSExtValue();
                        if (StepVal == 1 || StepVal == -1)
                        {
                            // 单位步长，最常见的线性访问模式
                            HasLinearIndex = true;
                        }
                        else if (StepVal != 0)
                        {
                            // 非单位步长，仍是线性但可能是跳跃式访问
                            HasLinearIndex = true;
                            if (std::abs(StepVal) > 8)
                            {
                                // 大步长通常意味着更不规则的访问
                                HasComplexIndex = true;
                            }
                        }
                    }
                    else
                    {
                        // 步长不是简单常量
                        HasComplexIndex = true;
                    }
                    continue;
                }
                else
                {
                    // 非仿射递归表达式，如二次项变化 (i = i^2 + k)
                    HasComplexIndex = true;
                    continue;
                }
            }

            // 不是递归表达式，检查是否在其他循环中有可计算的规律
            if (SE.hasComputableLoopEvolution(IdxSCEV, nullptr))
            {
                HasComplexIndex = true;
                continue;
            }

            // 检查更复杂的 SCEV 表达式
            if (isa<SCEVUDivExpr>(IdxSCEV) || isa<SCEVAddExpr>(IdxSCEV) ||
                isa<SCEVMulExpr>(IdxSCEV) || isa<SCEVSMaxExpr>(IdxSCEV) ||
                isa<SCEVSMinExpr>(IdxSCEV))
            {
                HasComplexIndex = true;
                continue;
            }
        }

        // 无法通过SCEV确定规律，检查指令本身的特性
        if (Instruction *IdxInst = dyn_cast<Instruction>(IdxVal))
        {
            // 检查基本运算
            if (IdxInst->getOpcode() == Instruction::Add ||
                IdxInst->getOpcode() == Instruction::Sub)
            {
                // 简单的加减操作
                // 检查是否有常量操作数，可能表示线性变化
                for (unsigned i = 0; i < IdxInst->getNumOperands(); ++i)
                {
                    if (isa<ConstantInt>(IdxInst->getOperand(i)))
                    {
                        HasLinearIndex = true;
                        break;
                    }
                }
                if (!HasLinearIndex)
                {
                    HasComplexIndex = true;
                }
                continue;
            }
            else if (IdxInst->getOpcode() == Instruction::Mul)
            {
                // 乘法操作可能导致非线性变化
                // 检查是否是常数乘法，仍可能是线性的
                for (unsigned i = 0; i < IdxInst->getNumOperands(); ++i)
                {
                    if (isa<ConstantInt>(IdxInst->getOperand(i)))
                    {
                        HasLinearIndex = true;
                        break;
                    }
                }
                if (!HasLinearIndex)
                {
                    HasComplexIndex = true;
                }
                continue;
            }
            else if (IdxInst->getOpcode() == Instruction::Shl ||
                     IdxInst->getOpcode() == Instruction::LShr ||
                     IdxInst->getOpcode() == Instruction::AShr)
            {
                // 移位操作可能是乘以或除以2的幂
                if (auto *CI = dyn_cast<ConstantInt>(IdxInst->getOperand(1)))
                {
                    HasLinearIndex = true;
                }
                else
                {
                    HasComplexIndex = true;
                }
                continue;
            }
            else if (IdxInst->getOpcode() == Instruction::Select)
            {
                // Select 可能表示条件执行路径，通常很复杂
                HasComplexIndex = true;
                continue;
            }
            else if (isa<CallInst>(IdxInst) || isa<InvokeInst>(IdxInst))
            {
                // 函数调用通常难以预测
                // 除非是一些简单的数学内置函数
                if (auto *CI = dyn_cast<CallInst>(IdxInst))
                {
                    Function *Callee = CI->getCalledFunction();
                    if (Callee && Callee->isIntrinsic())
                    {
                        // 一些内置数学函数可能有规律
                        HasComplexIndex = true;
                        continue;
                    }
                }
                return StrideType::IRREGULAR;
            }
        }

        // 其他无法分析的索引模式，视为不规则
        return StrideType::IRREGULAR;
    }

    // 根据分析结果确定最终类型
    if (!HasVariableIndex)
        return StrideType::CONSTANT;
    if (HasComplexIndex)
        return StrideType::COMPLEX;
    if (HasLinearIndex)
        return StrideType::LINEAR;

    // 默认情况，理论上不应该到达这里
    return StrideType::UNKNOWN;
}

// 分析是否为流式访问模式
bool StrideAnalyzer::isStreamingAccess(Value *Ptr, Loop *L, AAResults &AA)
{   
    // errs() << "===== Function:isStreamingAccess =====\n";
    if (!Ptr || !L || !SE.isSCEVable(Ptr->getType()))
        return false;

    // 获取指针的SCEV表达式
    const SCEV *PtrSCEV = SE.getSCEV(Ptr);

    // 分析是否为仿射加递归表达式（线性变化）
    if (const auto *AR = dyn_cast<SCEVAddRecExpr>(PtrSCEV))
    {
        if (AR->isAffine() && AR->getLoop() == L)
        {
            // 获取步长
            const SCEV *Step = AR->getStepRecurrence(SE);

            // 检查步长是否为常量
            if (const auto *ConstStep = dyn_cast<SCEVConstant>(Step))
            {
                // 获取步长值
                int64_t StrideVal = ConstStep->getValue()->getSExtValue();

                // stride-1 是最理想的连续访问模式
                if (StrideVal == 1 || StrideVal == -1)
                    return true;

                // 检查步长是否与类型大小匹配（可能是按元素访问）
                Type *ElemTy = nullptr;

                // 从上下文中推断元素类型
                if (auto *LI = dyn_cast<LoadInst>(Ptr))
                {
                    ElemTy = LI->getType();
                }
                else if (auto *SI = dyn_cast<StoreInst>(Ptr))
                {
                    ElemTy = SI->getValueOperand()->getType();
                }
                else if (auto *GEP = dyn_cast<GetElementPtrInst>(Ptr))
                {
                    ElemTy = GEP->getSourceElementType();
                }

                if (ElemTy)
                {
                    // 确保有 Module 信息
                    const Module *M = nullptr;
                    if (const auto *Inst = dyn_cast<Instruction>(Ptr))
                    {
                        M = Inst->getModule();
                    }
                    else if (const auto *GV = dyn_cast<GlobalValue>(Ptr))
                    {
                        M = GV->getParent();
                    }
                    else if (const auto *Arg = dyn_cast<Argument>(Ptr))
                    {
                        if (Arg->getParent())
                            M = Arg->getParent()->getParent();
                    }

                    if (M)
                    {
                        const DataLayout &DL = M->getDataLayout();
                        uint64_t ElemSize = DL.getTypeAllocSize(ElemTy);

                        // 防止溢出或除零
                        if (ElemSize > 0)
                        {
                            int64_t AbsStride = std::abs(StrideVal);

                            // 步长等于元素大小，是按元素顺序访问
                            if (AbsStride == static_cast<int64_t>(ElemSize))
                                return true;

                            // 步长是元素大小的倍数，可能是跳跃访问但仍保持规则性
                            if (AbsStride % static_cast<int64_t>(ElemSize) == 0 &&
                                AbsStride < 1024 * static_cast<int64_t>(ElemSize))
                                return true;

                            // 步长是元素大小的约数，可能是结构体中的字段访问
                            if (static_cast<int64_t>(ElemSize) % AbsStride == 0 &&
                                AbsStride <= 8)
                                return true;
                        }
                    }
                }

                // 其他固定步长，如果不是太大，也可以算作流式
                // 使用更灵活的上限判断
                uint64_t CacheLineSize = 64; // 假设缓存行大小为64字节
                if (std::abs(StrideVal) <= static_cast<int64_t>(CacheLineSize * 16))
                    return true;
            }
            else
            {
                // 步长不是常量，但如果是循环不变量且在合理范围内，仍可能是流式
                if (SE.isLoopInvariant(Step, L))
                {
                    // 对步长值进行进一步分析
                    // 例如，检查步长是否由常数组成的简单表达式
                    if (auto *AddExpr = dyn_cast<SCEVAddExpr>(Step))
                    {
                        bool AllConstantOperands = true;
                        for (unsigned i = 0; i < AddExpr->getNumOperands(); ++i)
                        {
                            if (!isa<SCEVConstant>(AddExpr->getOperand(i)))
                            {
                                AllConstantOperands = false;
                                break;
                            }
                        }
                        if (AllConstantOperands)
                            return true;
                    }

                    if (auto *MulExpr = dyn_cast<SCEVMulExpr>(Step))
                    {
                        bool AllConstantOperands = true;
                        for (unsigned i = 0; i < MulExpr->getNumOperands(); ++i)
                        {
                            if (!isa<SCEVConstant>(MulExpr->getOperand(i)))
                            {
                                AllConstantOperands = false;
                                break;
                            }
                        }
                        if (AllConstantOperands)
                            return true;
                    }
                }
            }
        }
    }

    // 对于不直接表现为加递归的指针，检查是否通过GEP访问
    if (const auto *GEP = dyn_cast<GetElementPtrInst>(Ptr))
    {
        StrideType GEPStrideType = analyzeGEPStride(const_cast<GetElementPtrInst *>(GEP));

        // 常量或线性步长通常是流式访问
        if (GEPStrideType == StrideType::CONSTANT || GEPStrideType == StrideType::LINEAR)
            return true;

        // 对于复杂步长，进一步分析
        if (GEPStrideType == StrideType::COMPLEX)
        {
            bool AllIndicesInvariantOrLinear = true;

            for (auto I = GEP->idx_begin(), E = GEP->idx_end(); I != E; ++I)
            {
                Value *Idx = *I;

                // 跳过常量索引
                if (isa<ConstantInt>(Idx))
                    continue;

                if (SE.isSCEVable(Idx->getType()))
                {
                    const SCEV *IdxSCEV = SE.getSCEV(Idx);

                    // 循环不变量或线性变化
                    if (SE.isLoopInvariant(IdxSCEV, L) ||
                        (isa<SCEVAddRecExpr>(IdxSCEV) &&
                         cast<SCEVAddRecExpr>(IdxSCEV)->isAffine() &&
                         cast<SCEVAddRecExpr>(IdxSCEV)->getLoop() == L))
                    {
                        continue;
                    }
                }

                AllIndicesInvariantOrLinear = false;
                break;
            }

            if (AllIndicesInvariantOrLinear)
                return true;
        }
    }

    // 如果循环被标记为可向量化，这通常意味着访问是规则的
    if (L->getLoopID() && LoopUtils::isLoopMarkedVectorizable(L))
        return true;

    // 检查是否在小范围内重复访问同一地址（时间局部性好）
    SmallPtrSet<Value *, 8> AccessedAddresses;
    SmallVector<Instruction *, 16> MemAccesses;

    // 收集循环中的内存访问
    for (auto *BB : L->getBlocks())
    {
        for (auto &I : *BB)
        {
            if (auto *LI = dyn_cast<LoadInst>(&I))
            {
                if (AA.alias(LI->getPointerOperand(), 1, Ptr, 1) != AliasResult::NoAlias)
                {
                    MemAccesses.push_back(&I);
                    AccessedAddresses.insert(LI->getPointerOperand());
                }
            }
            else if (auto *SI = dyn_cast<StoreInst>(&I))
            {
                if (AA.alias(SI->getPointerOperand(), 1, Ptr, 1) != AliasResult::NoAlias)
                {
                    MemAccesses.push_back(&I);
                    AccessedAddresses.insert(SI->getPointerOperand());
                }
            }
        }
    }

    // 如果访问次数远大于地址数量，说明有较好的时间局部性
    if (MemAccesses.size() >= 3 * AccessedAddresses.size() && AccessedAddresses.size() <= 4)
    {
        return true;
    }

    return false;
}