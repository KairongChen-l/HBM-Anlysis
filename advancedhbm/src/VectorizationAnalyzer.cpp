#include "VectorizationAnalyzer.h"
#include "LoopUtils.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
using namespace llvm;
using namespace MyHBM;

// 检查指令是否使用向量类型或SIMD操作
bool VectorizationAnalyzer::isVectorizedInstruction(Instruction *I)
{   
    // errs() << "===== Function:isVectorizedInstruction =====\n";
    if (!I)
        return false;

    // 检查直接的向量类型操作
    if (I->getType()->isVectorTy())
        return true;

    // 检查操作数是否为向量类型
    for (unsigned i = 0; i < I->getNumOperands(); ++i)
    {
        if (I->getOperand(i)->getType()->isVectorTy())
            return true;
    }

    // 检查是否为向量内部函数调用
    if (auto *Call = dyn_cast<CallInst>(I))
    {
        Function *Callee = Call->getCalledFunction();
        if (Callee)
        {
            StringRef Name = Callee->getName();
            // 检查LLVM向量内部函数
            if (Name.starts_with("llvm.vector") ||
                Name.contains("simd") ||
                Name.contains("vector") ||
                Name.starts_with("llvm.x86.sse") ||
                Name.starts_with("llvm.x86.avx") ||
                Name.starts_with("llvm.x86.mmx") ||
                Name.starts_with("llvm.arm.neon"))
                return true;
        }
    }

    // 检查常见向量指令模式
    unsigned Opcode = I->getOpcode();
    if (Opcode == Instruction::ExtractElement ||
        Opcode == Instruction::InsertElement ||
        Opcode == Instruction::ShuffleVector)
        return true;

    return false;
}

// 获取向量类型的宽度
int VectorizationAnalyzer::getVectorWidth(Type *Ty)
{
    // errs() << "===== Function:getVectorWidth =====\n";
    if (!Ty || !Ty->isVectorTy())
        return 0;
        
    // 使用 ElementCount 来获取向量宽度
    ElementCount EC = cast<VectorType>(Ty)->getElementCount();
    
    // 检查是否为固定长度向量
    if (EC.isScalable()) {
        // 可伸缩向量返回最小元素数量，标记为负数表示可伸缩
        return -EC.getKnownMinValue();
    } else {
        // 固定长度向量返回确切元素数量
        return EC.getKnownMinValue();
    }
}

// 检测函数中是否有SIMD内部函数调用
// 检测函数中是否有SIMD内部函数调用
bool VectorizationAnalyzer::detectSIMDIntrinsics(Function &F)
{
    // errs() << "===== Function:detectSIMDIntrinsics =====\n";
    for (auto &BB : F)
    {
        for (auto &I : BB)
        {
            // 检查内联函数
            if (auto *II = dyn_cast<IntrinsicInst>(&I)) {
                Intrinsic::ID ID = II->getIntrinsicID();      
                // 检查特定向量内联函数
                switch (ID) {
                    case Intrinsic::vector_reduce_add:
                    case Intrinsic::vector_reduce_mul:
                    case Intrinsic::vector_reduce_and:
                    case Intrinsic::vector_reduce_or:
                    case Intrinsic::vector_reduce_xor:
                    case Intrinsic::vector_reduce_smax:
                    case Intrinsic::vector_reduce_smin:
                    case Intrinsic::vector_reduce_umax:
                    case Intrinsic::vector_reduce_umin:
                    case Intrinsic::vector_reduce_fmax:
                    case Intrinsic::vector_reduce_fmin:
                    case Intrinsic::vector_reduce_fadd:
                    case Intrinsic::vector_reduce_fmul:
                    case Intrinsic::masked_load:
                    case Intrinsic::masked_store:
                    case Intrinsic::masked_gather:
                    case Intrinsic::masked_scatter:
                        return true;
                    default:
                        break;
                }
            }
            
            // 检查常规函数调用
            if (auto *Call = dyn_cast<CallInst>(&I))
            {
                Function *Callee = Call->getCalledFunction();
                if (!Callee)
                    continue;

                StringRef Name = Callee->getName();
                // 检查常见SIMD内部函数
                if (Name.starts_with("llvm.x86.sse") ||
                    Name.starts_with("llvm.x86.avx") ||
                    Name.starts_with("llvm.x86.mmx") ||
                    Name.starts_with("llvm.arm.neon") ||
                    Name.starts_with("_mm_") ||    // Intel SSE
                    Name.starts_with("_mm256_") || // Intel AVX
                    Name.starts_with("_mm512_") || // Intel AVX-512
                    Name.starts_with("vec_"))      // PowerPC Altivec
                    return true;
            }
            
            // 检查向量类型的指令
            if (I.getType()->isVectorTy())
                return true;
                
            // 检查使用向量类型操作数的指令
            for (unsigned i = 0; i < I.getNumOperands(); ++i) {
                if (I.getOperand(i)->getType()->isVectorTy())
                    return true;
            }
        }
    }
    return false;
}

// 检查循环是否显示出向量化模式
bool VectorizationAnalyzer::isVectorLoopPattern(Loop *L)
{
    // errs() << "===== Function:isVectorLoopPattern =====\n";
    if (!L)
        return false;

    BasicBlock *Header = L->getHeader();
    if (!Header)
        return false;

    // 1. 检查循环是否被向量化注解标记
    if (LoopUtils::isLoopMarkedVectorizable(L))
        return true;

    // 2. 循环访问步长检查 - 连续步长通常更容易向量化
    bool HasConsecutiveAccess = false;
    for (auto *BB : L->getBlocks())
    {
        for (auto &I : *BB)
        {
            if (auto *Load = dyn_cast<LoadInst>(&I))
            {
                if (SE.isSCEVable(Load->getPointerOperand()->getType()))
                {
                    const SCEV *PtrSCEV = SE.getSCEV(Load->getPointerOperand());
                    if (auto *AR = dyn_cast<SCEVAddRecExpr>(PtrSCEV))
                    {
                        if (AR->isAffine() &&
                            isa<SCEVConstant>(AR->getStepRecurrence(SE)))
                        {
                            HasConsecutiveAccess = true;
                        }
                    }
                }
            }
            else if (auto *Store = dyn_cast<StoreInst>(&I))
            {
                if (SE.isSCEVable(Store->getPointerOperand()->getType()))
                {
                    const SCEV *PtrSCEV = SE.getSCEV(Store->getPointerOperand());
                    if (auto *AR = dyn_cast<SCEVAddRecExpr>(PtrSCEV))
                    {
                        if (AR->isAffine() &&
                            isa<SCEVConstant>(AR->getStepRecurrence(SE)))
                        {
                            HasConsecutiveAccess = true;
                        }
                    }
                }
            }
        }
    }

    // 3. 循环体大小 - 小循环体更容易向量化
    unsigned LoopSize = 0;
    for (auto *BB : L->getBlocks())
    {
        LoopSize += std::distance(BB->begin(), BB->end());
    }
    bool IsSmallLoopBody = LoopSize < 50; // 经验阈值，可调整

    // 4. 检查循环内是否有影响向量化的分支
    bool HasBranches = false;
    for (auto *BB : L->getBlocks())
    {
        if (BB != L->getHeader() && BB != L->getExitingBlock() &&
            isa<BranchInst>(BB->getTerminator()) &&
            cast<BranchInst>(BB->getTerminator())->isConditional())
        {
            HasBranches = true;
            break;
        }
    }

    // 5. 循环内的归约操作检查 - 典型的可向量化模式
    bool HasReduction = false;
    for (auto *BB : L->getBlocks())
    {
        for (auto &I : *BB)
        {
            if (I.getOpcode() == Instruction::Add ||
                I.getOpcode() == Instruction::FAdd ||
                I.getOpcode() == Instruction::Mul ||
                I.getOpcode() == Instruction::FMul)
            {
                for (auto &Op : I.operands())
                {
                    if (auto *Inst = dyn_cast<Instruction>(Op.get()))
                    {
                        if (Inst->getParent() == BB &&
                            Inst->getOpcode() == I.getOpcode())
                        {
                            HasReduction = true;
                            break;
                        }
                    }
                }
                if (HasReduction)
                    break;
            }
        }
        if (HasReduction)
            break;
    }

    // 综合评估是否是向量化友好的循环
    return (HasConsecutiveAccess && IsSmallLoopBody && !HasBranches) || HasReduction;
}

// 递归检查值是否参与了向量操作
bool VectorizationAnalyzer::hasVectorOperations(Value *V, std::set<Value *> &Visited)
{   
    // errs() << "===== Function:hasVectorOperations =====\n";
    if (!V || !Visited.insert(V).second)
        return false;

    // 检查值是否为向量类型
    if (V->getType()->isVectorTy())
        return true;

    // 检查指令是否为向量指令
    if (auto *I = dyn_cast<Instruction>(V))
    {
        if (isVectorizedInstruction(I))
            return true;

        // 递归检查所有使用这个值的指令
        for (auto *User : V->users())
        {
            if (hasVectorOperations(User, Visited))
                return true;
        }

        // 递归检查所有操作数
        for (unsigned i = 0; i < I->getNumOperands(); ++i)
        {
            if (hasVectorOperations(I->getOperand(i), Visited))
                return true;
        }
    }

    return false;
}
