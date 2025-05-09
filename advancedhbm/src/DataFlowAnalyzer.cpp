#include "DataFlowAnalyzer.h"
#include "CrossFunctionAnalyzer.h"
#include "WeightConfig.h" // Include the weight config header
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include <queue>
#include <unordered_map>
#include <algorithm>

using namespace llvm;
using namespace MyHBM;

#define DEBUG_TYPE "data-flow-analyzer"

// 分析数据流
DataFlowInfo DataFlowAnalyzer::analyzeDataFlow(Value *AllocPtr, Function &F)
{
  errs() << "===== Function:analyzeDataFlow =====\n";
  DataFlowInfo Result;
  if (!AllocPtr)
  {
    LLVM_DEBUG(dbgs() << "DataFlowAnalyzer: null allocation pointer\n");
    return Result;
  }

  // 1. 找出数据流的所有使用点
  std::vector<Instruction *> UseInsts;
  std::set<BasicBlock *> UseBlocks;

  // 收集直接使用和衍生使用
  std::queue<Value *> WorkList;
  std::set<Value *> Visited;
  WorkList.push(AllocPtr);

  CrossFunctionAnalyzer CrossFnAn;

  while (!WorkList.empty())
  {
    Value *V = WorkList.front();
    WorkList.pop();

    if (!Visited.insert(V).second)
      continue;

    for (User *U : V->users())
    {
      if (auto *I = dyn_cast<Instruction>(U))
      {
        if (I->getFunction() == &F)
        {
          UseInsts.push_back(I);
          UseBlocks.insert(I->getParent());

          // 继续跟踪衍生值
          if (isa<GetElementPtrInst>(I) || isa<BitCastInst>(I) ||
              isa<LoadInst>(I) || isa<PHINode>(I) || isa<SelectInst>(I))
          {
            WorkList.push(I);
          }
        }
      }
    }
  }

  // 2. 找出可能的阶段转换点 (phase transition points)
  std::set<BasicBlock *> TransitionBlocks = findPhaseTransitionPoints(AllocPtr, F);

  // 3. 基于使用模式推断生命周期阶段
  // 假设分配指令为 ALLOCATION 阶段
  if (auto *AllocInst = dyn_cast<Instruction>(AllocPtr))
  {
    Result.phaseMap[AllocInst] = DataFlowInfo::LifetimePhase::ALLOCATION;
  }

  // 找出初始化阶段（分配后的写入）
  bool foundInit = false;
  for (Instruction *I : UseInsts)
  {
    if (auto *SI = dyn_cast<StoreInst>(I))
    {
      // 判断是否是对分配内存的写入
      if (CrossFnAn.isPtrDerivedFrom(SI->getPointerOperand(), AllocPtr))
      {
        // 检查该存储是否靠近分配点
        if (isInstructionNear(SI, AllocPtr, 20))
        {
          Result.phaseMap[SI] = DataFlowInfo::LifetimePhase::INITIALIZATION;
          foundInit = true;
        }
      }
    }

    // 处理memset/memcpy等初始化函数
    if (auto *Call = dyn_cast<CallInst>(I))
    {
      Function *Callee = Call->getCalledFunction();
      if (Callee)
      {
        StringRef Name = Callee->getName();
        if (Name.contains("memset") || Name.contains("memcpy") || Name.contains("memmove"))
        {
          if (Call->arg_size() > 0 && CrossFnAn.isPtrDerivedFrom(Call->getArgOperand(0), AllocPtr))
          {
            Result.phaseMap[Call] = DataFlowInfo::LifetimePhase::INITIALIZATION;
            foundInit = true;
          }
        }
      }
    }
  }
  Result.hasInitPhase = foundInit;

  // 识别活跃使用阶段 (ACTIVE_USE)
  unsigned activeUseCount = 0;
  for (Instruction *I : UseInsts)
  {
    // 如果已经分类，则跳过
    if (Result.phaseMap.count(I))
      continue;

    if (auto *LI = dyn_cast<LoadInst>(I))
    {
      if (CrossFnAn.isPtrDerivedFrom(LI->getPointerOperand(), AllocPtr))
      {
        Result.phaseMap[LI] = DataFlowInfo::LifetimePhase::ACTIVE_USE;
        activeUseCount++;
      }
    }
    else if (auto *SI = dyn_cast<StoreInst>(I))
    {
      if (CrossFnAn.isPtrDerivedFrom(SI->getPointerOperand(), AllocPtr))
      {
        // 初始化后的写入视为活跃使用
        if (!isInstructionNear(SI, AllocPtr, 20))
        {
          Result.phaseMap[SI] = DataFlowInfo::LifetimePhase::ACTIVE_USE;
          activeUseCount++;
        }
      }
    }
    else if (auto *Call = dyn_cast<CallInst>(I))
    {
      // 指针作为参数传递给函数调用
      for (unsigned i = 0; i < Call->arg_size(); ++i)
      {
        if (CrossFnAn.isPtrDerivedFrom(Call->getArgOperand(i), AllocPtr))
        {
          Result.phaseMap[Call] = DataFlowInfo::LifetimePhase::ACTIVE_USE;
          activeUseCount++;
          break;
        }
      }
    }
  }

  // 识别只读阶段 (READ_ONLY)
  // 假设分配的内存在某个点之后只被读取不被写入
  bool enteredReadOnly = false;
  for (BasicBlock *BB : TransitionBlocks)
  {
    // 检查该基本块后的所有使用是否只是读取
    bool onlyReads = true;
    for (Instruction *I : UseInsts)
    {
      if (dominates(BB, I->getParent()))
      {
        if (auto *SI = dyn_cast<StoreInst>(I))
        {
          if (CrossFnAn.isPtrDerivedFrom(SI->getPointerOperand(), AllocPtr))
          {
            onlyReads = false;
            break;
          }
        }
      }
    }

    if (onlyReads)
    {
      enteredReadOnly = true;
      // 标记该基本块后的所有读取为只读阶段
      for (Instruction *I : UseInsts)
      {
        if (dominates(BB, I->getParent()))
        {
          if (auto *LI = dyn_cast<LoadInst>(I))
          {
            if (CrossFnAn.isPtrDerivedFrom(LI->getPointerOperand(), AllocPtr))
            {
              Result.phaseMap[LI] = DataFlowInfo::LifetimePhase::READ_ONLY;
            }
          }
        }
      }
    }
  }
  Result.hasReadOnlyPhase = enteredReadOnly;

  // 识别释放阶段 (DEALLOCATION)
  for (Instruction *I : UseInsts)
  {
    if (auto *Call = dyn_cast<CallInst>(I))
    {
      Function *Callee = Call->getCalledFunction();
      if (Callee)
      {
        StringRef Name = Callee->getName();
        if (Name == "free" || Name.starts_with("_Zd"))
        {
          if (Call->arg_size() >= 1 &&
              CrossFnAn.isPtrDerivedFrom(Call->getArgOperand(0), AllocPtr))
          {
            Result.phaseMap[Call] = DataFlowInfo::LifetimePhase::DEALLOCATION;
          }
        }
      }
    }
  }

  // 识别休眠阶段 (DORMANT)
  // 如果指针在某段时间没有被访问
  std::set<BasicBlock *> DormantCandidates;
  for (BasicBlock &BB : F)
  {
    // 如果基本块不包含指针的任何使用
    if (UseBlocks.count(&BB) == 0)
    {
      // 并且基本块被执行的路径上离使用点有一定距离
      bool isPotentialDormant = true;
      for (BasicBlock *UB : UseBlocks)
      {
        if (isPotentiallyReachableFromTo(&BB, UB, nullptr, nullptr, true))
        {
          if (getApproximateBlockDistance(&BB, UB) < 5)
          {
            isPotentialDormant = false;
            break;
          }
        }
      }

      if (isPotentialDormant)
      {
        DormantCandidates.insert(&BB);
      }
    }
  }

  Result.hasDormantPhase = !DormantCandidates.empty();

  // 计算每个阶段的平均使用次数
  std::map<DataFlowInfo::LifetimePhase, unsigned> PhaseUseCounts;
  for (auto &Pair : Result.phaseMap)
  {
    PhaseUseCounts[Pair.second]++;
  }

  unsigned totalPhases = 0;
  unsigned totalUses = 0;
  for (auto &Pair : PhaseUseCounts)
  {
    if (Pair.first != DataFlowInfo::LifetimePhase::ALLOCATION &&
        Pair.first != DataFlowInfo::LifetimePhase::DEALLOCATION)
    {
      totalPhases++;
      totalUses += Pair.second;
    }
  }

  Result.avgUsesPerPhase = totalPhases > 0 ? double(totalUses) / totalPhases : 0.0;

  // 计算数据流分数
  using namespace WeightConfig; // Using our weight configuration
  // 计算数据流分数
  Result.dataFlowScore = 0.0;

  // 有初始化阶段和活跃使用的数据更可能是热点
  if (Result.hasInitPhase && activeUseCount > 0)
  {
    Result.dataFlowScore += InitPhaseBonus;
  }

  // 有只读阶段的数据可能适合一次性加载到HBM
  if (Result.hasReadOnlyPhase)
  {
    Result.dataFlowScore += ReadOnlyPhaseBonus;
  }

  // 没有休眠阶段的数据可能更活跃
  if (!Result.hasDormantPhase)
  {
    Result.dataFlowScore += NoDormantPhaseBonus;
  }

  // 根据使用密度调整分数
  Result.dataFlowScore += std::min(20.0, Result.avgUsesPerPhase * 2.0);

  return Result;
}

// 找出可能的阶段转换点
std::set<BasicBlock *> DataFlowAnalyzer::findPhaseTransitionPoints(Value *Ptr, Function &F)
{
  errs() << "===== Function:findPhaseTransitionPoints =====\n";
  std::set<BasicBlock *> Result;

  // 找出循环出口点
  DominatorTree DT(F);
  LoopInfo LI(DT);
  for (auto &L : LI)
  {
    SmallVector<BasicBlock *, 4> ExitBlocks;
    L->getExitBlocks(ExitBlocks);
    for (BasicBlock *ExitBB : ExitBlocks)
    {
      Result.insert(ExitBB);
    }
  }

  // 找出包含条件分支的基本块
  for (auto &BB : F)
  {
    if (auto *BI = dyn_cast<BranchInst>(BB.getTerminator()))
    {
      if (BI->isConditional())
      {
        // 检查条件是否与指针相关
        Value *Cond = BI->getCondition();
        if (isPtrValueDependent(Cond, Ptr))
        {
          Result.insert(&BB);
        }
      }
    }
  }

  return Result;
}

// 判断一个条件是否依赖于指针
bool DataFlowAnalyzer::isPtrValueDependent(Value *Cond, Value *Ptr)
{
  errs() << "===== Function:isPtrValueDependent =====\n";
  if (!Cond || !Ptr)
    return false;

  std::set<Value *> Visited;
  std::queue<Value *> WorkList;
  WorkList.push(Cond);

  CrossFunctionAnalyzer CrossFnAn;

  while (!WorkList.empty())
  {
    Value *V = WorkList.front();
    WorkList.pop();

    if (!Visited.insert(V).second)
      continue;

    if (auto *LI = dyn_cast<LoadInst>(V))
    {
      if (CrossFnAn.isPtrDerivedFrom(LI->getPointerOperand(), Ptr))
      {
        return true;
      }
    }

    if (auto *I = dyn_cast<Instruction>(V))
    {
      for (Use &U : I->operands())
      {
        WorkList.push(U.get());
      }
    }
  }

  return false;
}

// 判断两个指令是否相近
bool DataFlowAnalyzer::isInstructionNear(Instruction *I1, Value *I2, unsigned threshold)
{
  errs() << "===== Function:isInstructionNear =====\n";
  if (auto *I2Inst = dyn_cast<Instruction>(I2))
  {
    if (I1->getParent() == I2Inst->getParent())
    {
      // 如果在同一基本块，检查指令间距
      BasicBlock *BB = I1->getParent();
      unsigned distance = 0;
      bool foundFirst = false;

      for (auto &I : *BB)
      {
        if (&I == I1 || &I == I2Inst)
        {
          if (!foundFirst)
          {
            foundFirst = true;
          }
          else
          {
            return distance < threshold;
          }
        }

        if (foundFirst)
        {
          distance++;
        }
      }
    }
  }

  return false;
}

// 计算两个基本块的近似距离
unsigned DataFlowAnalyzer::getApproximateBlockDistance(BasicBlock *BB1, BasicBlock *BB2)
{
  errs() << "===== Function:getApproximateBlockDistance =====\n";
  if (BB1 == BB2)
    return 0;

  std::set<BasicBlock *> Visited;
  std::queue<std::pair<BasicBlock *, unsigned>> WorkList;
  WorkList.push({BB1, 0});

  while (!WorkList.empty())
  {
    auto [BB, distance] = WorkList.front();
    WorkList.pop();

    if (!Visited.insert(BB).second)
      continue;

    if (BB == BB2)
      return distance;

    for (auto *Succ : successors(BB))
    {
      WorkList.push({Succ, distance + 1});
    }
  }

  return UINT_MAX; // 无法到达
}

// 判断A是否支配B
bool DataFlowAnalyzer::dominates(BasicBlock *A, BasicBlock *B)
{
  errs() << "===== Function:dominates =====\n";
  if (A == B)
    return true;

  // 创建支配树
  DominatorTree DT(*A->getParent());
  return DT.dominates(A, B);
}

// 检查从From到To是否可达
bool DataFlowAnalyzer::isPotentiallyReachableFromTo(BasicBlock *From, BasicBlock *To,
                                                    void *domTree, void *postDomTree, bool exact)
{
  errs() << "===== Function:isPotentiallyReachableFromTo =====\n";
  if (From == To)
    return true;

  // 简单实现：检查是否有路径从From到To
  std::set<BasicBlock *> Visited;
  std::queue<BasicBlock *> WorkList;
  WorkList.push(From);

  while (!WorkList.empty())
  {
    BasicBlock *BB = WorkList.front();
    WorkList.pop();

    if (!Visited.insert(BB).second)
      continue;

    if (BB == To)
      return true;

    for (auto *Succ : successors(BB))
    {
      WorkList.push(Succ);
    }
  }

  return false;
}