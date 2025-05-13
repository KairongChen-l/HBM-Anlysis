#include "ParallelismAnalyzer.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Instructions.h" // 为GetElementPtrInst提供支持
#include "llvm/IR/Type.h"         // 为Type类和相关方法提供支持
#include "llvm/IR/DerivedTypes.h" // 为PointerType和其他衍生类型提供支持
#include "llvm/IR/DataLayout.h"   // 为DataLayout提供支持
#include "llvm/IR/Value.h"        // 为Value类提供支持
#include "llvm/Support/Casting.h" // 为dyn_cast提供支持
#include <queue>
#include <unordered_set>

using namespace llvm;
using namespace MyHBM;

// 分析指针的线程访问模式
ThreadAccessPattern ParallelismAnalyzer::analyzeThreadAccess(Value *Ptr, Instruction *I)
{   
    // errs() << "===== Function:analyzeThreadAccess =====\n";
    if (!Ptr || !I)
        return ThreadAccessPattern::UNKNOWN;

    Function *F = I->getFunction();
    if (!F || !detectParallelRuntime(*F))
        return ThreadAccessPattern::UNKNOWN; // 非并行函数

    // 是否是原子访问
    if (isAtomicAccess(I))
        return ThreadAccessPattern::ATOMIC_ACCESS;

    // 检查是否通过线程ID索引
    if (isThreadDependentAccess(Ptr))
        return ThreadAccessPattern::PARTITIONED;

    // 检查是否只读共享
    bool isWrite = false;
    if (isa<StoreInst>(I))
        isWrite = true;
    else if (auto *Call = dyn_cast<CallInst>(I))
    {
        // 检查调用是否可能写入内存
        if (Call->mayWriteToMemory())
            isWrite = true;
    }

    if (!isWrite)
    {
        // 只读访问通常是安全的
        return ThreadAccessPattern::SHARED_READONLY;
    }

    // 检查是否存在伪共享
    const DataLayout &DL = I->getModule()->getDataLayout();
    if (detectFalseSharing(Ptr, DL))
        return ThreadAccessPattern::FALSE_SHARING;

    // 默认为共享写入访问（可能有冲突）
    return ThreadAccessPattern::SHARED_WRITE;
}

// 检测是否为OpenMP并行执行
bool ParallelismAnalyzer::isOpenMPParallel(Function &F)
{   
    // errs() << "===== Function:isOpenMPParallel =====\n";
    // 检查函数名称或属性
    if (F.getName().contains("_omp_") ||
        F.hasFnAttribute("omp") ||
        F.getSection().contains("omp"))
        return true;

    // 检查是否调用OpenMP运行时函数
    for (auto &BB : F)
    {
        for (auto &I : BB)
        {
            if (auto *Call = dyn_cast<CallInst>(&I))
            {
                Function *Callee = Call->getCalledFunction();
                if (Callee)
                {
                    StringRef Name = Callee->getName();
                    if (Name.starts_with("__kmpc_") ||
                        Name.starts_with("omp_") ||
                        Name.contains("gomp"))
                        return true;
                }
            }
        }
    }

    // 检查OpenMP元数据
    for (auto &BB : F)
    {
        for (auto &I : BB)
        {
            if (I.getMetadata("llvm.loop.parallel_accesses"))
                return true;
        }
    }

    return false;
}

// 检测是否为CUDA并行执行
bool ParallelismAnalyzer::isCUDAParallel(Function &F)
{   
    // errs() << "===== Function:isCUDAParallel =====\n";
    // 检查函数是否有CUDA属性
    if (F.getName().starts_with("_Z") &&
        (F.getName().contains("cuda") ||
         F.hasFnAttribute("kernel") ||
         F.getSection().contains("cuda")))
        return true;

    // 检查是否调用CUDA运行时函数
    for (auto &BB : F)
    {
        for (auto &I : BB)
        {
            if (auto *Call = dyn_cast<CallInst>(&I))
            {
                Function *Callee = Call->getCalledFunction();
                if (Callee)
                {
                    StringRef Name = Callee->getName();
                    if (Name.starts_with("cuda") ||
                        Name.contains("kernel") ||
                        Name.contains("nvvm"))
                        return true;
                }
            }
        }
    }

    return false;
}

// 检测是否为TBB并行执行
bool ParallelismAnalyzer::isTBBParallel(Function &F)
{   
    // errs() << "===== Function:isTBBParallel =====\n";
    // 检查是否调用TBB函数
    for (auto &BB : F)
    {
        for (auto &I : BB)
        {
            if (auto *Call = dyn_cast<CallInst>(&I))
            {
                Function *Callee = Call->getCalledFunction();
                if (Callee)
                {
                    StringRef Name = Callee->getName();
                    if (Name.contains("tbb") &&
                        (Name.contains("parallel") ||
                         Name.contains("task") ||
                         Name.contains("flow")))
                        return true;
                }
            }
        }
    }

    return false;
}

// 估计并行执行的线程数
unsigned ParallelismAnalyzer::estimateParallelThreads(Function &F)
{   
    // errs() << "===== Function:estimateParallelThreads =====\n";
    // 默认并行度
    unsigned DefaultThreads = 4;

    // 检查显式线程数设置
    for (auto &BB : F)
    {
        for (auto &I : BB)
        {
            if (auto *Call = dyn_cast<CallInst>(&I))
            {
                Function *Callee = Call->getCalledFunction();
                if (!Callee)
                    continue;

                StringRef Name = Callee->getName();
                // OpenMP线程数设置
                if (Name == "omp_set_num_threads" && Call->arg_size() > 0)
                {
                    if (auto *CI = dyn_cast<ConstantInt>(Call->getArgOperand(0)))
                        return CI->getZExtValue();
                }
                // CUDA内核启动
                else if (Name.contains("cudaLaunch") && Call->arg_size() > 1)
                {
                    // 尝试提取CUDA网格和块大小
                    // 注意：这是一个近似分析，实际情况可能更复杂
                    return 32; // 典型CUDA warp大小
                }
                // TBB并行
                else if (Name.contains("tbb::parallel_for") && Call->arg_size() > 1)
                {
                    return 8; // 典型TBB默认并行度
                }
            }
        }
    }

    // 检查函数属性中是否指定了线程数
    if (auto *AttrNode = F.getMetadata("parallel.threads"))
    {
        if (AttrNode->getNumOperands() > 0)
        {
            if (auto *ThreadsMD = dyn_cast<ConstantAsMetadata>(AttrNode->getOperand(0)))
            {
                if (auto *CI = dyn_cast<ConstantInt>(ThreadsMD->getValue()))
                    return CI->getZExtValue();
            }
        }
    }

    // 根据并行类型估计默认线程数
    if (isOpenMPParallel(F))
        return 16; // 使用硬件核心数的估计
    if (isCUDAParallel(F))
        return 128; // 典型CUDA并行度
    if (isTBBParallel(F))
        return 8; // 典型TBB并行度

    return DefaultThreads;
}

// 检查是否为原子访问
bool ParallelismAnalyzer::isAtomicAccess(Instruction *I)
{   
    // errs() << "===== Function:isAtomicAccess =====\n";
    if (!I)
        return false;

    // 显式原子指令
    if (isa<AtomicRMWInst>(I) || isa<AtomicCmpXchgInst>(I))
        return true;

    // 检查Load/Store是否为原子操作
    if (auto *Load = dyn_cast<LoadInst>(I))
        return Load->isAtomic();
    if (auto *Store = dyn_cast<StoreInst>(I))
        return Store->isAtomic();

    // 检查是否调用原子操作函数
    if (auto *Call = dyn_cast<CallInst>(I))
    {
        Function *Callee = Call->getCalledFunction();
        if (Callee)
        {
            StringRef Name = Callee->getName();
            return Name.contains("atomic") ||
                   Name.contains("mutex") ||
                   Name.contains("lock") ||
                   Name.contains("sync");
        }
    }

    return false;
}

// 检查是否有并行循环元数据
bool ParallelismAnalyzer::hasParallelLoopMetadata(Loop *L)
{   
    // errs() << "===== Function:hasParallelLoopMetadata =====\n";
    if (!L || !L->getHeader())
        return false;

    Instruction *Term = L->getHeader()->getTerminator();
    if (!Term)
        return false;

    if (MDNode *LoopID = Term->getMetadata("llvm.loop.parallel_accesses"))
        return true;

    if (MDNode *LoopID = Term->getMetadata("llvm.loop"))
    {
        for (unsigned i = 0, e = LoopID->getNumOperands(); i < e; ++i)
        {
            MDNode *MD = dyn_cast<MDNode>(LoopID->getOperand(i));
            if (!MD)
                continue;

            for (unsigned j = 0, je = MD->getNumOperands(); j < je; ++j)
            {
                if (auto *Str = dyn_cast<MDString>(MD->getOperand(j)))
                {
                    if (Str->getString().contains("parallel"))
                        return true;
                }
            }
        }
    }

    return false;
}

bool ParallelismAnalyzer::detectFalseSharing(Value *Ptr, const DataLayout &DL)
{   
    // errs() << "===== Function:detectFalseSharing =====\n";
    if (!Ptr)
        return false;

    // 如果是 GEP 指令，分析其索引模式
    if (auto *GEP = dyn_cast<GetElementPtrInst>(Ptr))
    {
        Value *BasePtr = GEP->getPointerOperand();

        // LLVM 18: 不再从 pointer type 中获取 element type，直接从 GEP 拿
        Type *BaseTy = GEP->getSourceElementType();

        if (!BaseTy)
            return false;

        // 检查是否为数组或结构体
        if (BaseTy->isArrayTy() || BaseTy->isStructTy())
        {
            // 计算访问的近似内存范围
            unsigned TypeSize = DL.getTypeAllocSize(BaseTy);
            unsigned CacheLineSize = 64; // 典型缓存行大小

            // 如果类型大小小于缓存行，并且多个线程访问同一缓存行的不同部分，可能存在伪共享
            if (TypeSize < CacheLineSize)
            {
                // 检查 GEP 的索引是否与线程 ID 相关
                for (auto I = GEP->idx_begin(), E = GEP->idx_end(); I != E; ++I)
                {
                    if (isThreadIDRelated(*I))
                    {
                        return true; //  判断为可能伪共享
                    }
                }
            }
        }
    }

    return false;
}

// 检测函数是否使用并行运行时
bool ParallelismAnalyzer::detectParallelRuntime(Function &F)
{   
    // errs() << "===== Function:detectParallelRuntime =====\n";
    const std::unordered_set<std::string> ParallelEntrypoints = {
        "__kmpc_fork_call", "__kmpc_for_static_init_4", "__kmpc_for_static_init_8",
        "__kmpc_for_static_init_16", "__kmpc_for_static_init_32", "__kmpc_for_static_init_64",
        "__kmpc_push_num_threads", "__kmpc_barrier",
        "pthread_create", "pthread_join", "pthread_mutex_lock", "pthread_mutex_unlock",
        "_ZSt13__thread_call", "std::thread",
        "tbb::task_group::run", "tbb::parallel_for", "tbb::parallel_invoke",
        "clEnqueueNDRangeKernel", "cudaLaunch", "cudaMemcpyAsync"};

    for (auto &BB : F)
    {
        for (auto &I : BB)
        {
            if (auto *CI = dyn_cast<CallInst>(&I))
            {
                Function *Callee = CI->getCalledFunction();
                // 直接调用
                if (Callee && ParallelEntrypoints.count(Callee->getName().str()))
                    return true;
                // 间接调用保守判断，可以做适当拓展
                if (!Callee && CI->getCalledOperand())
                    return true;
            }
        }
    }
    if (F.hasFnAttribute("omp_target_thread_limit") || F.hasFnAttribute("omp_target_parallel"))
        return true;
    return false;
}

// 检查是否为线程依赖的访问（通过线程ID进行索引）
bool ParallelismAnalyzer::isThreadDependentAccess(Value *Ptr)
{   
    // errs() << "===== Function:isThreadDependentAccess =====\n";
    if (!Ptr)
        return false;

    // 如果是GEP指令，检查其索引是否依赖线程ID
    if (auto *GEP = dyn_cast<GetElementPtrInst>(Ptr))
    {
        for (auto I = GEP->idx_begin(), E = GEP->idx_end(); I != E; ++I)
        {
            Value *Idx = *I;

            // 检查索引是否依赖线程ID
            if (isThreadIDRelated(Idx))
                return true;

            // 递归检查复杂索引表达式
            if (auto *IdxInst = dyn_cast<Instruction>(Idx))
            {
                // 递归检查二元操作的操作数
                if (auto *BinOp = dyn_cast<BinaryOperator>(IdxInst))
                {
                    if (isThreadIDRelated(BinOp->getOperand(0)) ||
                        isThreadIDRelated(BinOp->getOperand(1)))
                        return true;
                }
            }
        }
    }

    return false;
}

// 检查ID是否与线程ID相关
bool ParallelismAnalyzer::isThreadIDRelated(Value *V)
{   
    // errs() << "===== Function:isThreadIDRelated =====\n";
    if (!V)
        return false;

    std::queue<Value *> Q;
    std::unordered_set<Value *> Visited;
    Q.push(V);

    while (!Q.empty())
    {
        Value *Cur = Q.front();
        Q.pop();
        if (!Visited.insert(Cur).second)
            continue;

        if (auto *CI = dyn_cast<CallInst>(Cur))
        {
            Function *F = CI->getCalledFunction();
            if (F && (F->getName().contains("omp_get_thread_num") ||
                      F->getName().contains("pthread_self") ||
                      F->getName().contains("threadIdx") ||
                      F->getName().contains("get_local_id")))
            {
                return true;
            }
        }
        // 检查操作数是否包含线程ID
        if (auto *I = dyn_cast<Instruction>(Cur))
        {
            for (Use &U : I->operands())
                Q.push(U.get());
        }
    }
    return false;
}

// 辅助函数：检查一个指针是否派生自另一个指针
bool ParallelismAnalyzer::isPtrDerivedFrom(Value *Derived, Value *Base) {
    // Check for null input pointers
    if (!Derived || !Base) {
        return false;
    }
    
    // Check for direct equality
    if (Derived == Base) {
        return true;
    }
    
    if (auto *GEP = dyn_cast<GetElementPtrInst>(Derived)) {
        Value *PtrOp = GEP->getPointerOperand();
        // Add null check for the pointer operand
        if (!PtrOp) {
            return false;
        }
        return isPtrDerivedFrom(PtrOp, Base);
    }
    else if (auto *BC = dyn_cast<BitCastInst>(Derived)) {
        Value *Op = BC->getOperand(0);
        // Add null check for the operand
        if (!Op) {
            return false;
        }
        return isPtrDerivedFrom(Op, Base);
    }
    else if (auto *ASCI = dyn_cast<AddrSpaceCastInst>(Derived)) {
        Value *PtrOp = ASCI->getPointerOperand();
        // Add null check for the pointer operand
        if (!PtrOp) {
            return false;
        }
        return isPtrDerivedFrom(PtrOp, Base);
    }
    
    return false;
}