#include "DependencyChainAnalyzer.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include <algorithm>
#include <queue>
#include <limits>
#include "llvm/Analysis/ScalarEvolutionExpressions.h" // Include for SCEVAddRecExpr

using namespace llvm;
using namespace MyHBM;

// Clear all data structures
void DependencyChainAnalyzer::clear() {
  errs() << "===== Function:clear =====\n";
  // Free all allocated DependencyNodes
  for (auto &Entry : NodeMap) {
    delete Entry.second;
  }
  
  NodeMap.clear();
  MemoryAccessMap.clear();
}

// Check if an instruction is a memory operation
bool DependencyChainAnalyzer::isMemoryOperation(Instruction *I) {
  errs() << "===== Function:isMemoryOperation =====\n";
  if (!I) return false;
  
  if (isa<LoadInst>(I) || isa<StoreInst>(I) || isa<AtomicRMWInst>(I) || 
      isa<AtomicCmpXchgInst>(I) || isa<MemIntrinsic>(I)) {
    return true;
  }
  
  // Check if it's a call that may access memory
  if (auto *Call = dyn_cast<CallInst>(I)) {
    Function *F = Call->getCalledFunction();
    // If it's an indirect call or has side effects, assume it accesses memory
    if (!F || !F->doesNotAccessMemory()) {
      return true;
    }
  }
  
  return false;
}

// Build dependency graph for the function
void DependencyChainAnalyzer::buildDependencyGraph() {
  errs() << "===== Function:buildDependencyGraph =====\n";
  // Clear previous data
  clear();
  
  // Create nodes for all instructions
  for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    Instruction *Inst = &*I;
    NodeMap[Inst] = new DependencyNode(Inst);
    
    // Mark memory operations
    if (isMemoryOperation(Inst)) {
      NodeMap[Inst]->IsMemoryOp = true;
      
      // Track memory accesses by pointer
      if (auto *LI = dyn_cast<LoadInst>(Inst)) {
        Value *Ptr = LI->getPointerOperand();
        Value *BasePtr = traceToOriginalPointer(Ptr);
        MemoryAccessMap[BasePtr].push_back(Inst);
      } 
      else if (auto *SI = dyn_cast<StoreInst>(Inst)) {
        Value *Ptr = SI->getPointerOperand();
        Value *BasePtr = traceToOriginalPointer(Ptr);
        MemoryAccessMap[BasePtr].push_back(Inst);
      }
    }
    
    // Estimate instruction latency
    NodeMap[Inst]->EstimatedLatency = estimateInstructionLatency(Inst);
  }
  
  // Add dependencies between instructions
  for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    Instruction *Inst = &*I;
    DependencyNode *Node = NodeMap[Inst];
    
    // Data dependencies (through operands)
    for (Use &U : Inst->operands()) {
      if (auto *OpInst = dyn_cast<Instruction>(U.get())) {
        if (NodeMap.count(OpInst)) {
          DependencyNode *DepNode = NodeMap[OpInst];
          Node->Deps.push_back(DepNode);
          DepNode->Users.push_back(Node);
        }
      }
    }
    
    // Memory dependencies (basic approximation)
    if (auto *LI = dyn_cast<LoadInst>(Inst)) {
      Value *Ptr = LI->getPointerOperand();
      
      // Look for previous stores to the same location
      for (auto &BI : *LI->getParent()) {
        if (&BI == Inst) break; // Stop when we reach the current instruction
        
        if (auto *SI = dyn_cast<StoreInst>(&BI)) {
          Value *StorePtr = SI->getPointerOperand();
          // Simple alias check (can be expanded with AA)
          if (Ptr == StorePtr || Ptr->stripPointerCasts() == StorePtr->stripPointerCasts()) {
            DependencyNode *DepNode = NodeMap[&BI];
            Node->Deps.push_back(DepNode);
            DepNode->Users.push_back(Node);
          }
        }
      }
    }
    else if (auto *SI = dyn_cast<StoreInst>(Inst)) {
      Value *Ptr = SI->getPointerOperand();
      
      // Look for previous loads/stores to the same location
      for (auto &BI : *SI->getParent()) {
        if (&BI == Inst) break;
        
        if (auto *PrevLI = dyn_cast<LoadInst>(&BI)) {
          Value *LoadPtr = PrevLI->getPointerOperand();
          if (Ptr == LoadPtr || Ptr->stripPointerCasts() == LoadPtr->stripPointerCasts()) {
            DependencyNode *DepNode = NodeMap[&BI];
            Node->Deps.push_back(DepNode);
            DepNode->Users.push_back(Node);
          }
        }
        else if (auto *PrevSI = dyn_cast<StoreInst>(&BI)) {
          Value *StorePtr = PrevSI->getPointerOperand();
          if (Ptr == StorePtr || Ptr->stripPointerCasts() == StorePtr->stripPointerCasts()) {
            DependencyNode *DepNode = NodeMap[&BI];
            Node->Deps.push_back(DepNode);
            DepNode->Users.push_back(Node);
          }
        }
      }
    }
    
    // Control dependencies (through branch instructions)
    if (isa<BranchInst>(Inst) || isa<SwitchInst>(Inst)) {
      for (auto *Succ : successors(Inst->getParent())) {
        for (auto &SuccI : *Succ) {
          if (NodeMap.count(&SuccI)) {
            DependencyNode *SuccNode = NodeMap[&SuccI];
            SuccNode->Deps.push_back(Node);
            Node->Users.push_back(SuccNode);
          }
        }
      }
    }
  }
  
  // Calculate critical path latencies
  calculateCriticalPathLatencies();
  
  // Calculate memory latency sensitivity
  calculateMemoryLatencySensitivity();
}

// Calculate critical path latencies for all nodes
void DependencyChainAnalyzer::calculateCriticalPathLatencies() {
  errs() << "===== Function:calculateCriticalPathLatencies =====\n";
  // Find all nodes with no dependencies (entry points)
  std::vector<DependencyNode*> EntryNodes;
  for (auto &Entry : NodeMap) {
    if (Entry.second->Deps.empty()) {
      EntryNodes.push_back(Entry.second);
    }
  }
  
  // Topological sort of nodes
  std::vector<DependencyNode*> SortedNodes;
  std::set<DependencyNode*> Visited;
  
  std::function<void(DependencyNode*)> TopoSort = [&](DependencyNode *Node) {
    if (Visited.count(Node)) return;
    Visited.insert(Node);
    
    for (DependencyNode *Dep : Node->Deps) {
      TopoSort(Dep);
    }
    
    SortedNodes.push_back(Node);
  };
  
  for (DependencyNode *Node : EntryNodes) {
    TopoSort(Node);
  }
  
  // Process nodes in topological order
  for (DependencyNode *Node : SortedNodes) {
    // Initial value is the node's own latency
    Node->CriticalPathLatency = Node->EstimatedLatency;
    
    // Check all dependencies and take the maximum path
    for (DependencyNode *Dep : Node->Deps) {
      double PathLatency = Dep->CriticalPathLatency + Node->EstimatedLatency;
      if (PathLatency > Node->CriticalPathLatency) {
        Node->CriticalPathLatency = PathLatency;
      }
    }
  }
  
  // Mark instructions on critical paths
  // Find nodes with no users (exit points)
  std::vector<DependencyNode*> ExitNodes;
  for (auto &Entry : NodeMap) {
    if (Entry.second->Users.empty()) {
      ExitNodes.push_back(Entry.second);
    }
  }
  
  // Find the node with the highest critical path latency
  DependencyNode *CriticalNode = nullptr;
  double MaxLatency = 0.0;
  
  for (DependencyNode *Node : ExitNodes) {
    if (Node->CriticalPathLatency > MaxLatency) {
      MaxLatency = Node->CriticalPathLatency;
      CriticalNode = Node;
    }
  }
  
  // Mark all nodes on the critical path
  if (CriticalNode) {
    // Start from the critical exit node and backtrack
    std::set<DependencyNode*> MarkedNodes;
    std::queue<DependencyNode*> WorkList;
    WorkList.push(CriticalNode);
    
    while (!WorkList.empty()) {
      DependencyNode *Node = WorkList.front();
      WorkList.pop();
      
      if (MarkedNodes.count(Node)) continue;
      
      Node->IsInCriticalPath = true;
      MarkedNodes.insert(Node);
      
      // Find the dependency with the highest critical path
      DependencyNode *CriticalDep = nullptr;
      double MaxDepLatency = 0.0;
      
      for (DependencyNode *Dep : Node->Deps) {
        if (Dep->CriticalPathLatency > MaxDepLatency) {
          MaxDepLatency = Dep->CriticalPathLatency;
          CriticalDep = Dep;
        }
      }
      
      if (CriticalDep) {
        WorkList.push(CriticalDep);
      }
    }
  }
}

// Calculate memory latency sensitivity for each node
void DependencyChainAnalyzer::calculateMemoryLatencySensitivity() {
  errs() << "===== Function:calculateMemoryLatencySensitivity =====\n";
  // For each node, calculate how sensitive it is to memory latency
  for (auto &Entry : NodeMap) {
    DependencyNode *Node = Entry.second;
    
    // Memory operations are directly sensitive
    if (Node->IsMemoryOp) {
      if (auto *LI = dyn_cast<LoadInst>(Node->I)) {
        // Loads are highly sensitive to latency
        Node->MemoryLatencySensitivity = 1.0;
      } 
      else if (auto *SI = dyn_cast<StoreInst>(Node->I)) {
        // Stores are less sensitive to latency (but still sensitive)
        Node->MemoryLatencySensitivity = 0.6;
      }
      else {
        // Other memory operations (atomic, intrinsics, etc.)
        Node->MemoryLatencySensitivity = 0.8;
      }
    }
    // Non-memory operations sensitivity depends on dependencies
    else {
      // Calculate how much this instruction depends on memory operations
      double totalDependencyWeight = 0.0;
      double memoryDependencyWeight = 0.0;
      
      // BFS to find memory dependencies
      std::set<DependencyNode*> Visited;
      std::queue<std::pair<DependencyNode*, double>> WorkList;
      WorkList.push({Node, 1.0});
      
      while (!WorkList.empty()) {
        auto [CurrentNode, Weight] = WorkList.front();
        WorkList.pop();
        
        if (Visited.count(CurrentNode)) continue;
        Visited.insert(CurrentNode);
        
        totalDependencyWeight += Weight;
        
        if (CurrentNode->IsMemoryOp) {
          memoryDependencyWeight += Weight;
        }
        
        // Add dependencies with reduced weight
        double nextWeight = Weight * 0.8; // Decay factor
        if (nextWeight >= 0.05) { // Threshold to avoid too many small dependencies
          for (DependencyNode *Dep : CurrentNode->Deps) {
            WorkList.push({Dep, nextWeight});
          }
        }
      }
      
      // Calculate sensitivity ratio
      if (totalDependencyWeight > 0) {
        Node->MemoryLatencySensitivity = memoryDependencyWeight / totalDependencyWeight;
      }
    }
    
    // If on critical path, increase sensitivity
    if (Node->IsInCriticalPath) {
      Node->MemoryLatencySensitivity = std::min(1.0, Node->MemoryLatencySensitivity * 1.5);
    }
  }
}

// Estimate instruction latency (in abstract units)
double DependencyChainAnalyzer::estimateInstructionLatency(Instruction *I) {
  errs() << "===== Function:estimateInstructionLatency =====\n";
  if (!I) return 0.0;
  
  // These latencies are approximate and architecture-dependent
  // Values based on typical CPU instruction latencies, but simplified
  
  switch (I->getOpcode()) {
    // Memory operations
    case Instruction::Load:
      return 4.0; // Load from memory (avg case, can be much higher)
    case Instruction::Store:
      return 1.0; // Store to memory (often doesn't block)
      
    // Simple ALU operations
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
      return 0.5; // Fast integer operations
      
    // More complex ALU operations
    case Instruction::Mul:
      return 3.0; // Integer multiplication
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::URem:
    case Instruction::SRem:
      return 15.0; // Integer division (very slow)
      
    // Floating point operations
    case Instruction::FAdd:
    case Instruction::FSub:
      return 3.0; // FP addition/subtraction
    case Instruction::FMul:
      return 5.0; // FP multiplication
    case Instruction::FDiv:
    case Instruction::FRem:
      return 15.0; // FP division
      
    // Branches and calls
    case Instruction::Br:
      return 0.5; // Branch instruction
    case Instruction::Switch:
      return 1.0; // Switch statement
    case Instruction::Call:
      // Function calls vary widely - use a moderate default
      return 10.0;
      
    // Vector operations
    case Instruction::ExtractElement:
    case Instruction::InsertElement:
      return 1.0;
    case Instruction::ShuffleVector:
      return 2.0;
      
    // Special cases
    case Instruction::Select:
      return 1.0;
    case Instruction::PHI:
      return 0.0; // PHI nodes are just placeholders
    case Instruction::BitCast:
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::IntToPtr:
    case Instruction::PtrToInt:
      return 0.0; // Type conversions usually free
      
    // Atomic operations
    case Instruction::AtomicRMW:
    case Instruction::AtomicCmpXchg:
      return 20.0; // Atomic operations can be very expensive
      
    // Memory intrinsics
    case Intrinsic::memcpy:
      // For memory intrinsics, this is very approximate
      // Would need to adjust based on size
      return 10.0;
      
    default:
      return 1.0; // Default for unknown instructions
  }
}

// Trace a value back to its original pointer
Value* DependencyChainAnalyzer::traceToOriginalPointer(Value *V) {
  errs() << "===== Function:traceToOriginalPointer =====\n";
  if (!V) return nullptr;
  
  // Set to prevent cycles
  std::set<Value*> Visited;
  Value *Current = V;
  
  while (Current && !Visited.count(Current)) {
    Visited.insert(Current);
    
    if (isa<AllocaInst>(Current) || isa<GlobalValue>(Current) || 
        isa<Argument>(Current)) {
      // Found a source pointer
      return Current;
    }
    
    if (auto *GEP = dyn_cast<GetElementPtrInst>(Current)) {
      Current = GEP->getPointerOperand();
    }
    else if (auto *BC = dyn_cast<BitCastInst>(Current)) {
      Current = BC->getOperand(0);
    }
    else if (auto *LI = dyn_cast<LoadInst>(Current)) {
      // Loading a pointer from memory - can't trace further
      return Current;
    }
    else if (auto *Call = dyn_cast<CallInst>(Current)) {
      // Function returning a pointer - can't trace further
      return Current;
    }
    else if (auto *PHI = dyn_cast<PHINode>(Current)) {
      // For PHI nodes, we'd need to check all incoming values
      // For simplicity, just stop here
      return Current;
    }
    else {
      // Can't trace further
      break;
    }
  }
  
  return V; // Return original value if can't trace
}

// Get all memory accesses for a specific pointer
std::vector<Instruction*> DependencyChainAnalyzer::getMemoryAccesses(Value *Ptr) {
  errs() << "===== Function:getMemoryAccesses =====\n";
  std::vector<Instruction*> Result;
  
  if (!Ptr) return Result;
  
  // Check if we already have the accesses mapped
  Value *BasePtr = traceToOriginalPointer(Ptr);
  if (MemoryAccessMap.count(BasePtr)) {
    return MemoryAccessMap[BasePtr];
  }
  
  // Otherwise, find all uses
  std::set<Value*> Visited;
  std::queue<Value*> WorkList;
  WorkList.push(Ptr);
  
  while (!WorkList.empty()) {
    Value *V = WorkList.front();
    WorkList.pop();
    
    if (Visited.count(V)) continue;
    Visited.insert(V);
    
    for (User *U : V->users()) {
      if (auto *I = dyn_cast<Instruction>(U)) {
        if (auto *LI = dyn_cast<LoadInst>(I)) {
          Result.push_back(LI);
        }
        else if (auto *SI = dyn_cast<StoreInst>(I)) {
          if (SI->getPointerOperand() == V) {
            Result.push_back(SI);
          }
        }
        else if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
          WorkList.push(GEP);
        }
        else if (auto *BC = dyn_cast<BitCastInst>(I)) {
          WorkList.push(BC);
        }
      }
    }
  }
  
  // Cache the result for future queries
  MemoryAccessMap[BasePtr] = Result;
  
  return Result;
}

// Check if an instruction is on a critical path
bool DependencyChainAnalyzer::isOnCriticalPath(Instruction *I) {
  errs() << "===== Function:isOnCriticalPath =====\n";
  if (!I) return false;
  
  // Make sure the dependency graph is built
  if (NodeMap.empty()) {
    buildDependencyGraph();
  }
  
  auto It = NodeMap.find(I);
  return It != NodeMap.end() && It->second->IsInCriticalPath;
}

// Check if an instruction is in a critical path with given threshold
bool DependencyChainAnalyzer::isInCriticalPath(Instruction *I, double threshold) {
  errs() << "===== Function:isInCriticalPath =====\n";
  if (!I) return false;
  
  auto It = NodeMap.find(I);
  if (It == NodeMap.end()) return false;
  
  // Check if it's already marked as critical
  if (It->second->IsInCriticalPath) return true;
  
  // Or if it's close enough to the critical path
  double maxPathLatency = 0.0;
  for (auto &Entry : NodeMap) {
    maxPathLatency = std::max(maxPathLatency, Entry.second->CriticalPathLatency);
  }
  
  // If this instruction's path latency is within threshold% of max, consider it critical
  return It->second->CriticalPathLatency >= maxPathLatency * threshold;
}

// Find all critical paths in the dependency graph
std::vector<CriticalPath> DependencyChainAnalyzer::findAllCriticalPaths() {
    errs() << "===== Function:findAllCriticalPaths =====\n";
    std::vector<CriticalPath> Result;
    
    // Make sure the dependency graph is built
    if (NodeMap.empty()) {
      buildDependencyGraph();
    }
    
    // Find exit nodes (nodes with no users)
    std::vector<DependencyNode*> ExitNodes;
    for (auto &Entry : NodeMap) {
      if (Entry.second->Users.empty()) {
        ExitNodes.push_back(Entry.second);
      }
    }
    
    // Sort exit nodes by critical path latency
    std::sort(ExitNodes.begin(), ExitNodes.end(), 
             [](DependencyNode *A, DependencyNode *B) {
               return A->CriticalPathLatency > B->CriticalPathLatency;
             });
    
    // Take top N exit nodes (or fewer if there aren't that many)
    const unsigned MaxPaths = 5;
    unsigned NumPaths = std::min(MaxPaths, static_cast<unsigned>(ExitNodes.size()));
    
    for (unsigned i = 0; i < NumPaths; ++i) {
      DependencyNode *ExitNode = ExitNodes[i];
      CriticalPath Path;
      
      // Start from exit node and backtrack along the critical path
      DependencyNode *Current = ExitNode;
      Path.Instructions.push_back(Current->I);
      Path.TotalLatency = Current->EstimatedLatency;
      
      if (Current->IsMemoryOp) {
        Path.MemoryDependencies++;
      }
      
      while (!Current->Deps.empty()) {
        // Find dependency with highest critical path latency
        DependencyNode *CriticalDep = nullptr;
        double MaxLatency = 0.0;
        
        for (DependencyNode *Dep : Current->Deps) {
          if (Dep->CriticalPathLatency > MaxLatency) {
            MaxLatency = Dep->CriticalPathLatency;
            CriticalDep = Dep;
          }
        }
        
        if (!CriticalDep) break;
        
        // Add to path
        Current = CriticalDep;
        Path.Instructions.push_back(Current->I);
        Path.TotalLatency += Current->EstimatedLatency;
        
        if (Current->IsMemoryOp) {
          Path.MemoryDependencies++;
        }
      }
      
      // Calculate memory dependency ratio
      if (!Path.Instructions.empty()) {
        Path.MemoryDependencyRatio = static_cast<double>(Path.MemoryDependencies) / 
                                    Path.Instructions.size();
      }
      
      // Calculate latency sensitivity score
      Path.LatencySensitivityScore = Path.MemoryDependencyRatio * Path.TotalLatency;
      
      // Reverse the path to get chronological order
      std::reverse(Path.Instructions.begin(), Path.Instructions.end());
      
      Result.push_back(Path);
    }
    
    return Result;
  }
  
  // Main analysis method for a pointer
  DependencyInfo DependencyChainAnalyzer::analyzeDependencies(Value *Ptr) {
    errs() << "===== Function:analyzeDependencies =====\n";
    DependencyInfo Result;
    
    if (!Ptr) return Result;
    
    // Make sure the dependency graph is built
    if (NodeMap.empty()) {
      buildDependencyGraph();
    }
    
    // Get all memory accesses for this pointer
    std::vector<Instruction*> MemAccesses = getMemoryAccesses(Ptr);
    
    if (MemAccesses.empty()) {
      Result.Analysis = "No memory accesses found for this pointer in the function";
      return Result;
    }
    
    // Find all critical paths
    std::vector<CriticalPath> AllPaths = findAllCriticalPaths();
    
    // Check which critical paths involve memory accesses to this pointer
    for (const CriticalPath &Path : AllPaths) {
      bool InvolvesPointer = false;
      
      for (Instruction *I : Path.Instructions) {
        if (std::find(MemAccesses.begin(), MemAccesses.end(), I) != MemAccesses.end()) {
          InvolvesPointer = true;
          Result.CriticalMemoryOps.push_back(I);
        }
      }
      
      if (InvolvesPointer) {
        Result.CriticalPaths.push_back(Path);
        
        // Update maximum critical path latency
        Result.CriticalPathLatency = std::max(Result.CriticalPathLatency, Path.TotalLatency);
        
        // Update memory ratio in longest path
        if (Path.TotalLatency == Result.CriticalPathLatency) {
          Result.LongestPathMemoryRatio = Path.MemoryDependencyRatio;
        }
      }
    }
    
    // No critical paths involve this pointer
    if (Result.CriticalPaths.empty()) {
      Result.Analysis = "Pointer is not involved in any critical execution paths";
      Result.LatencySensitivityScore = 0.1; // Very low latency sensitivity
      Result.BandwidthSensitivityScore = calculateBandwidthSensitivityScore(Ptr);
      Result.IsLatencyBound = false;
      Result.HBMBenefitScore = Result.BandwidthSensitivityScore;
      return Result;
    }
    
    // Calculate latency sensitivity score
    Result.LatencySensitivityScore = calculateLatencySensitivityScore(Ptr);
    
    // Calculate bandwidth sensitivity score
    Result.BandwidthSensitivityScore = calculateBandwidthSensitivityScore(Ptr);
    
    // Determine if latency bound or bandwidth bound
    Result.IsLatencyBound = Result.LatencySensitivityScore > Result.BandwidthSensitivityScore;
    
    // Calculate HBM benefit score
    // HBM can help with both latency and bandwidth, but more with bandwidth
    Result.HBMBenefitScore = Result.BandwidthSensitivityScore * 0.7 + 
                            Result.LatencySensitivityScore * 0.3;
    
    // Generate detailed analysis
    std::string Analysis;
    Analysis = "Memory allocation is ";
    
    if (Result.IsLatencyBound) {
      Analysis += "primarily latency-sensitive. ";
      Analysis += "Critical path latency: " + std::to_string(Result.CriticalPathLatency) + 
                  " units with " + std::to_string(Result.LongestPathMemoryRatio * 100) + 
                  "% memory operations. ";
      
      if (Result.LongestPathMemoryRatio > 0.5) {
        Analysis += "High proportion of memory operations in critical path suggests ";
        if (Result.BandwidthSensitivityScore > 0.5) {
          Analysis += "both latency and bandwidth improvements from HBM would be beneficial.";
        } else {
          Analysis += "latency reduction would be more beneficial than bandwidth increase.";
        }
      } else {
        Analysis += "Memory operations appear at critical points but don't dominate the path.";
      }
    } else {
      Analysis += "primarily bandwidth-sensitive. ";
      
      if (Result.BandwidthSensitivityScore > 0.7) {
        Analysis += "Very high bandwidth sensitivity suggests significant HBM benefit.";
      } else if (Result.BandwidthSensitivityScore > 0.4) {
        Analysis += "Moderate bandwidth sensitivity suggests potential HBM benefit.";
      } else {
        Analysis += "Low bandwidth sensitivity suggests limited HBM benefit.";
      }
    }
    
    Result.Analysis = Analysis;
    
    return Result;
  }
  
  // Calculate latency sensitivity score for a pointer
  double DependencyChainAnalyzer::calculateLatencySensitivityScore(Value *Ptr) {
    errs() << "===== Function:calculateLatencySensitivityScore =====\n";
    if (!Ptr) return 0.0;
    
    // Get all memory accesses for this pointer
    std::vector<Instruction*> MemAccesses = getMemoryAccesses(Ptr);
    
    if (MemAccesses.empty()) {
      return 0.0;
    }
    
    // Calculate average latency sensitivity of all memory operations
    double TotalSensitivity = 0.0;
    unsigned CriticalAccessCount = 0;
    
    for (Instruction *I : MemAccesses) {
      auto It = NodeMap.find(I);
      if (It != NodeMap.end()) {
        TotalSensitivity += It->second->MemoryLatencySensitivity;
        
        if (It->second->IsInCriticalPath) {
          CriticalAccessCount++;
        }
      }
    }
    
    // Base sensitivity score on average
    double AvgSensitivity = MemAccesses.empty() ? 0.0 : 
                          TotalSensitivity / MemAccesses.size();
    
    // Adjust based on how many accesses are on critical paths
    double CriticalRatio = MemAccesses.empty() ? 0.0 : 
                          static_cast<double>(CriticalAccessCount) / MemAccesses.size();
    
    // Higher weight for critical accesses
    double Score = AvgSensitivity * (1.0 + CriticalRatio);
    
    // Scale to 0-1 range
    return std::min(1.0, Score);
  }
  
  // Calculate bandwidth sensitivity score for a pointer
  double DependencyChainAnalyzer::calculateBandwidthSensitivityScore(Value *Ptr) {
    errs() << "===== Function:calculateBandwidthSensitivityScore =====\n";
    if (!Ptr) return 0.0;
    
    // Get all memory accesses for this pointer
    std::vector<Instruction*> MemAccesses = getMemoryAccesses(Ptr);
    
    if (MemAccesses.empty()) {
      return 0.0;
    }
    
    // Factors that indicate bandwidth sensitivity:
    // 1. Large number of memory operations
    // 2. Operations in loops (especially tight loops)
    // 3. Sequential access patterns
    // 4. Vector operations
    
    // Count operations in loops
    unsigned LoopAccessCount = 0;
    unsigned InnerLoopAccessCount = 0;
    unsigned VectorAccessCount = 0;
    unsigned TotalAccessCount = MemAccesses.size();
    
    for (Instruction *I : MemAccesses) {
      // Check if in loop
      Loop *L = LI.getLoopFor(I->getParent());
      if (L) {
        LoopAccessCount++;
        
        // Check if innermost loop
        if (L->getSubLoops().empty()) {
          InnerLoopAccessCount++;
        }
      }
      
      // Check if vector operation
      if (auto *LI = dyn_cast<LoadInst>(I)) {
        if (LI->getType()->isVectorTy()) {
          VectorAccessCount++;
        }
      } 
      else if (auto *SI = dyn_cast<StoreInst>(I)) {
        if (SI->getValueOperand()->getType()->isVectorTy()) {
          VectorAccessCount++;
        }
      }
    }
    
    // Calculate ratios
    double LoopRatio = static_cast<double>(LoopAccessCount) / TotalAccessCount;
    double InnerLoopRatio = static_cast<double>(InnerLoopAccessCount) / TotalAccessCount;
    double VectorRatio = static_cast<double>(VectorAccessCount) / TotalAccessCount;
    
    // Calculate bandwidth sensitivity score
    double Score = 0.0;
    
    // Base score from access count
    Score += std::min(0.3, 0.01 * TotalAccessCount); // Up to 0.3 for many accesses
    
    // Adjust for loop presence
    Score += LoopRatio * 0.3; // Up to 0.3 for all accesses in loops
    Score += InnerLoopRatio * 0.2; // Up to 0.2 extra for innermost loops
    
    // Adjust for vector operations
    Score += VectorRatio * 0.2; // Up to 0.2 for vectorized accesses
    
    // Adjust for access patterns
    // Check for sequential access patterns
    bool HasSequentialPattern = false;
    
    // Simple heuristic: check GEP indices in loops
    for (Instruction *I : MemAccesses) {
      if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
        Loop *L = LI.getLoopFor(GEP->getParent());
        if (L) {
          for (auto I = GEP->idx_begin(), E = GEP->idx_end(); I != E; ++I) {
            Value *Idx = *I;
            
            if (SE.isSCEVable(Idx->getType())) {
              const SCEV *IdxSCEV = SE.getSCEV(Idx);
              
              // Check if the index depends on loop induction variable
              if (auto *AR = dyn_cast<SCEVAddRecExpr>(IdxSCEV)) {
                if (AR->getLoop() == L && AR->isAffine()) {
                  HasSequentialPattern = true;
                  break;
                }
              }
            }
          }
        }
      }
    }
    
    if (HasSequentialPattern) {
      Score += 0.2; // Bonus for sequential access
    }
    
    // Scale to 0-1 range
    return std::min(1.0, Score);
  }
  
  // Find all critical paths in the function
  std::vector<CriticalPath> DependencyChainAnalyzer::findCriticalPaths() {
    errs() << "===== Function:findCriticalPaths =====\n";
    // Make sure the dependency graph is built
    if (NodeMap.empty()) {
      buildDependencyGraph();
    }
    
    return findAllCriticalPaths();
  }
  
  // Identify instructions that would benefit most from reduced memory latency
  std::vector<Instruction*> DependencyChainAnalyzer::rankByLatencySensitivity() {
    errs() << "===== Function:rankByLatencySensitivity =====\n";
    // Make sure the dependency graph is built
    if (NodeMap.empty()) {
      buildDependencyGraph();
    }
    
    // Collect all memory operations
    std::vector<DependencyNode*> MemoryNodes;
    for (auto &Entry : NodeMap) {
      if (Entry.second->IsMemoryOp) {
        MemoryNodes.push_back(Entry.second);
      }
    }
    
    // Sort by memory latency sensitivity (highest first)
    std::sort(MemoryNodes.begin(), MemoryNodes.end(),
            [](DependencyNode *A, DependencyNode *B) {
              return A->MemoryLatencySensitivity > B->MemoryLatencySensitivity;
            });
    
    // Convert to instruction list
    std::vector<Instruction*> Result;
    for (DependencyNode *Node : MemoryNodes) {
      Result.push_back(Node->I);
    }
    
    return Result;
  }
  
  // Determine if a pointer is more latency or bandwidth sensitive
  bool DependencyChainAnalyzer::isLatencySensitive(Value *Ptr) {
    errs() << "===== Function:isLatencySensitive =====\n";
    double LatencyScore = calculateLatencySensitivityScore(Ptr);
    double BandwidthScore = calculateBandwidthSensitivityScore(Ptr);
    
    return LatencyScore > BandwidthScore;
  }
  
  // Check if there's a data dependency chain between two instructions
  bool DependencyChainAnalyzer::hasDataDependencyChain(Instruction *Src, Instruction *Dst, 
                                                    std::set<Instruction*> &Visited) {
    errs() << "===== Function:hasDataDependencyChain =====\n";
    if (!Src || !Dst) return false;
    if (Src == Dst) return true;
    if (Visited.count(Src)) return false;
    
    Visited.insert(Src);
    
    // Check direct operand dependencies
    for (Use &U : Src->uses()) {
      if (auto *UserInst = dyn_cast<Instruction>(U.getUser())) {
        if (UserInst == Dst) return true;
        if (hasDataDependencyChain(UserInst, Dst, Visited)) return true;
      }
    }
    
    return false;
  }
  
  // Check if instruction is memory dependent
  bool DependencyChainAnalyzer::isMemoryDependent(Instruction *I) {
    errs() << "===== Function:isMemoryDependent =====\n";
    if (!I) return false;
    
    // Direct memory instruction
    if (isa<LoadInst>(I) || isa<StoreInst>(I) || isa<AtomicRMWInst>(I) || 
        isa<AtomicCmpXchgInst>(I) || isa<MemIntrinsic>(I)) {
      return true;
    }
    
    // Check operands for memory dependencies
    for (Use &U : I->operands()) {
      if (auto *OpInst = dyn_cast<Instruction>(U.get())) {
        if (isa<LoadInst>(OpInst)) {
          return true;
        }
      }
    }
    
    return false;
  }