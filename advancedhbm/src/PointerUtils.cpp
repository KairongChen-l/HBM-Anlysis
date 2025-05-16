#include "PointerUtils.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Operator.h"
#include <queue>

using namespace llvm;
using namespace MyHBM;

namespace MyHBM
{
    namespace PointerUtils
    {

        // 解析指针的基地址
        Value *resolveBasePointer(Value *V)
        {
            // 使用 SmallPtrSet 进行访问记录，这比 std::set 更高效
            SmallPtrSet<Value *, 16> Visited;
            // 使用 SmallVector 作为工作列表，适合这种短期存储的场景
            SmallVector<Value *, 8> Worklist;

            // 增加一个最大遍历数
            const unsigned MaxIterations = 10000;
            unsigned IterCount = 0;
            Worklist.push_back(V);

            while (!Worklist.empty() && IterCount++ < MaxIterations)
            {
                Value *Cur = Worklist.pop_back_val();

                // 如果已访问过，跳过
                if (!Visited.insert(Cur).second)
                    continue;

                // 检查是否为分配函数调用
                if (auto *CI = dyn_cast<CallInst>(Cur))
                {
                    Function *Callee = CI->getCalledFunction();
                    // 检查直接调用
                    if (Callee)
                    {
                        StringRef Name = Callee->getName();
                        // 检查各种常见的内存分配函数
                        if (Name == "malloc" ||
                            Name == "calloc" ||
                            Name == "realloc" ||
                            Name.starts_with("_Znwm") || // C++ new
                            Name.starts_with("_Znam") || // C++ new[]
                            Name.contains("alloc"))      // 其他可能的分配函数
                            return CI;
                    }
                    // 无法解析的调用，可能是间接调用
                    else if (CI->isIndirectCall())
                    {
                        // 尝试通过类型启发式判断是否为分配函数
                        Type *RetTy = CI->getType();
                        if (RetTy->isPointerTy() && CI->arg_size() > 0)
                        {
                            // 第一个参数通常是大小
                            Value *FirstArg = CI->getArgOperand(0);
                            if (FirstArg->getType()->isIntegerTy())
                                return CI; // 可能是分配函数
                        }
                    }
                }

                // 处理各种指针操作指令
                if (auto *GEP = dyn_cast<GetElementPtrInst>(Cur))
                    Worklist.push_back(GEP->getPointerOperand());
                else if (auto *BC = dyn_cast<BitCastOperator>(Cur))
                    Worklist.push_back(BC->getOperand(0));
                else if (auto *ASCI = dyn_cast<AddrSpaceCastInst>(Cur))
                    Worklist.push_back(ASCI->getPointerOperand());
                else if (auto *PN = dyn_cast<PHINode>(Cur))
                {
                    for (Value *Incoming : PN->incoming_values())
                        Worklist.push_back(Incoming);
                }
                else if (auto *SI = dyn_cast<SelectInst>(Cur))
                {
                    Worklist.push_back(SI->getTrueValue());
                    Worklist.push_back(SI->getFalseValue());
                }
                else if (auto *LI = dyn_cast<LoadInst>(Cur))
                    Worklist.push_back(LI->getPointerOperand());
                else if (auto *Gep = dyn_cast<GEPOperator>(Cur))
                    Worklist.push_back(Gep->getPointerOperand());
                else if (auto *BC = dyn_cast<BitCastInst>(Cur))
                    Worklist.push_back(BC->getOperand(0));
            }
            //因为遍历数过大导致退出，因为太大可能导致内存溢出
            if (IterCount >= MaxIterations)
            {
                errs() << "Warning: Maximum iteration count reached in resolveBasePointer."
                       << " Possible cycle in pointer chain.\n";
            }
            // 无法找到基地址
            return nullptr;
        }

        // 检查值是否可能从内存加载
        bool isMayLoadFromMemory(Value *V)
        {
            if (!V)
                return false;

            if (isa<LoadInst>(V))
                return true;
            if (isa<CallInst>(V) || isa<InvokeInst>(V))
                return true;

            if (auto *I = dyn_cast<Instruction>(V))
            {
                for (Use &U : I->operands())
                {
                    if (isMayLoadFromMemory(U.get()))
                        return true;
                }
            }

            return false;
        }

        // 检查指针是否被函数调用访问
        bool isPointerAccessedByCall(CallInst *Call, Value *Ptr, AAResults &AA)
        {
            if (!Call || !Ptr)
                return false;

            // 遍历所有参数
            for (unsigned i = 0; i < Call->arg_size(); ++i)
            {
                Value *Arg = Call->getArgOperand(i);
                if (!Arg->getType()->isPointerTy())
                    continue;

                // 检查参数是否可能指向与Ptr相同的内存
                AliasResult AR = AA.alias(Arg, Ptr);
                if (AR != AliasResult::NoAlias)
                    return true;
            }

            return false;
        }

        // 检查是否为线程本地存储
        bool isThreadLocalStorage(Value *Ptr)
        {
            if (!Ptr)
                return false;

            // 检查是否为线程本地变量
            if (auto *GV = dyn_cast<GlobalVariable>(Ptr))
                return GV->isThreadLocal();

            // 检查是否为局部变量（栈上分配，通常是线程本地的）
            if (isa<AllocaInst>(Ptr))
                return true;

            // 检查是否显式标记为线程本地
            if (auto *I = dyn_cast<Instruction>(Ptr))
            {
                if (I->getMetadata("thread_local"))
                    return true;
            }

            return false;
        }

        // 尝试从Value中提取常量大小
        std::optional<uint64_t> getConstantAllocSize(Value *V, std::set<Value *> &Visited)
        {
            // 检查空指针和防止循环
            if (!V || !Visited.insert(V).second)
                return std::nullopt;

            // 处理直接常量
            if (auto *CI = dyn_cast<ConstantInt>(V))
            {
                if (CI->isNegative())
                    return std::nullopt;

                return CI->getZExtValue();
            }

            // 处理常量表达式
            if (auto *CE = dyn_cast<ConstantExpr>(V))
            {
                auto op0 = getConstantAllocSize(CE->getOperand(0), Visited);
                auto op1 = CE->getNumOperands() > 1 ? getConstantAllocSize(CE->getOperand(1), Visited) : std::optional<uint64_t>(0);

                if (CE->getNumOperands() <= 1 || !op0)
                    return op0; // 对于单操作数指令，直接返回op0

                if (!op1)
                    return std::nullopt;

                switch (CE->getOpcode())
                {
                case Instruction::Add:
                    return *op0 + *op1;
                case Instruction::Sub:
                    return *op0 > *op1 ? std::optional(*op0 - *op1) : std::nullopt;
                case Instruction::Mul:
                {
                    // 检查乘法溢出
                    uint64_t result;
                    if (__builtin_mul_overflow(*op0, *op1, &result))
                        return std::nullopt;
                    return result;
                }
                case Instruction::UDiv:
                    return *op1 != 0 ? std::optional(*op0 / *op1) : std::nullopt;
                case Instruction::URem:
                    return *op1 != 0 ? std::optional(*op0 % *op1) : std::nullopt;
                case Instruction::Shl:
                {
                    // 移位操作数应小于位宽
                    if (*op1 >= 64)
                        return std::nullopt;
                    return *op0 << *op1;
                }
                case Instruction::LShr:
                    if (*op1 >= 64)
                        return 0;
                    return *op0 >> *op1;
                case Instruction::And:
                    return *op0 & *op1;
                case Instruction::Or:
                    return *op0 | *op1;
                case Instruction::Xor:
                    return *op0 ^ *op1;
                case Instruction::Trunc:
                    // 处理截断操作
                    return *op0 & ((1ULL << CE->getType()->getIntegerBitWidth()) - 1);
                case Instruction::ZExt:
                case Instruction::SExt:
                    // 处理扩展操作
                    return *op0;
                default:
                    break;
                }
            }

            // 处理指令
            if (auto *I = dyn_cast<Instruction>(V))
            {
                // 处理PHI节点
                if (auto *PHI = dyn_cast<PHINode>(I))
                {
                    std::optional<uint64_t> Result;

                    // 检查所有PHI分支是否有相同的常量值
                    for (unsigned i = 0, e = PHI->getNumIncomingValues(); i != e; ++i)
                    {
                        auto IncomingValue = getConstantAllocSize(PHI->getIncomingValue(i), Visited);
                        if (!IncomingValue)
                            return std::nullopt;

                        if (!Result)
                            Result = IncomingValue;
                        else if (*Result != *IncomingValue)
                            return std::nullopt; // PHI中的值不一致
                    }

                    return Result;
                }

                // 处理Select指令
                if (auto *Select = dyn_cast<SelectInst>(I))
                {
                    if (auto *Cond = dyn_cast<ConstantInt>(Select->getCondition()))
                    {
                        Value *SelectedOp = Cond->isOne() ? Select->getTrueValue() : Select->getFalseValue();
                        return getConstantAllocSize(SelectedOp, Visited);
                    }

                    // 如果条件不是常量，检查两个分支是否返回相同的值
                    auto TrueResult = getConstantAllocSize(Select->getTrueValue(), Visited);
                    auto FalseResult = getConstantAllocSize(Select->getFalseValue(), Visited);

                    if (!TrueResult || !FalseResult)
                        return std::nullopt;

                    if (*TrueResult == *FalseResult)
                        return TrueResult;

                    return std::nullopt;
                }

                // 处理GEP指令
                if (auto *GEP = dyn_cast<GetElementPtrInst>(I))
                {
                    // 获取源指针类型
                    Type *SourceTy = GEP->getSourceElementType();
                    if (SourceTy && SourceTy->isSized())
                    {
                        const DataLayout &DL = I->getModule()->getDataLayout();
                        uint64_t ElemSize = DL.getTypeAllocSize(SourceTy);

                        // 只处理简单情况：常量索引的数组访问
                        if (GEP->hasAllConstantIndices() && GEP->getNumIndices() == 1)
                        {
                            if (auto *CI = dyn_cast<ConstantInt>(GEP->getOperand(1)))
                                return ElemSize * CI->getZExtValue();
                        }
                    }
                }

                // 处理算术指令
                unsigned Opcode = I->getOpcode();
                if (I->getNumOperands() >= 2 &&
                    (Opcode == Instruction::Add ||
                     Opcode == Instruction::Sub ||
                     Opcode == Instruction::Mul ||
                     Opcode == Instruction::UDiv ||
                     Opcode == Instruction::Shl ||
                     Opcode == Instruction::And))
                {
                    auto op0 = getConstantAllocSize(I->getOperand(0), Visited);
                    auto op1 = getConstantAllocSize(I->getOperand(1), Visited);

                    if (!op0 || !op1)
                        return std::nullopt;

                    switch (Opcode)
                    {
                    case Instruction::Add:
                        return *op0 + *op1;
                    case Instruction::Sub:
                        return *op0 > *op1 ? std::optional(*op0 - *op1) : std::nullopt;
                    case Instruction::Mul:
                    {
                        uint64_t result;
                        if (__builtin_mul_overflow(*op0, *op1, &result))
                            return std::nullopt;
                        return result;
                    }
                    case Instruction::UDiv:
                        return *op1 != 0 ? std::optional(*op0 / *op1) : std::nullopt;
                    case Instruction::Shl:
                        if (*op1 >= 64)
                            return std::nullopt;
                        return *op0 << *op1;
                    case Instruction::And:
                        return *op0 & *op1;
                    default:
                        break;
                    }
                }

                // 处理类型转换指令
                if (Opcode == Instruction::Trunc ||
                    Opcode == Instruction::ZExt ||
                    Opcode == Instruction::SExt)
                {
                    auto op = getConstantAllocSize(I->getOperand(0), Visited);
                    if (!op)
                        return std::nullopt;

                    if (Opcode == Instruction::Trunc)
                    {
                        // 获取截断后的位宽
                        unsigned BitWidth = cast<IntegerType>(I->getType())->getBitWidth();
                        return *op & ((1ULL << BitWidth) - 1);
                    }

                    return op; // 对于扩展操作，值不变
                }
            }

            // 处理全局变量的初始化器
            if (auto *GV = dyn_cast<GlobalVariable>(V))
            {
                if (GV->hasInitializer())
                {
                    return getConstantAllocSize(GV->getInitializer(), Visited);
                }
            }

            return std::nullopt;
        }

        // 简化包装方法
        uint64_t getConstantAllocSize(Value *V)
        {
            std::set<Value *> Visited;
            auto result = getConstantAllocSize(V, Visited);

// 增加日志记录，帮助调试
#ifdef DEBUG_ALLOC_SIZE
            if (!result)
            {
                dbgs() << "Failed to get constant size for: ";
                V->print(dbgs());
                dbgs() << "\n";
            }
#endif

            return result.value_or(0);
        }

    } // namespace PointerUtils
} // namespace MyHBM