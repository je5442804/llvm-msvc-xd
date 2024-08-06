#include "FlatteningEnhanced.h"
#include "CryptoUtils.h"
#include "Utils.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"

#include <cstdlib>
#include <ctime>
#include <list>
#include <map>
#include <utility>
#include <vector>

using namespace llvm;
static cl::opt<bool> FlaEnhanced("x-fla-enh", cl::init(false),
                                 cl::desc("OLLVM - x-fla-enh"));
static cl::opt<int> FlaEnhancedNum("x-fla-ender", cl::init(20),
                                   cl::desc("OLLVM - x-fla-enh ender number"));

namespace llvm {
struct TreeNode {
  unsigned int val = 0, limit = 0, l = 0,
               r = 0; // [x,y)   x<=val<y
  TreeNode *left = nullptr, *right = nullptr;
};
struct FlaEnhPass {
  bool runOnFunction(Function *f);
  std::list<TreeNode *>::iterator random_element(std::list<TreeNode *> *x);

  void expandNode(TreeNode *node);

  int genRandomTree(TreeNode *node, int node_limit);

  void walkTree(TreeNode *node);
  bool allocNode(TreeNode *node, unsigned int l, unsigned int r);
  BasicBlock *
  createRandomBasicBlock(TreeNode *node, Function *f, Value *var,
                         std::vector<BasicBlock *>::iterator &iter,
                         std::map<BasicBlock *, TreeNode *> *bb_info);

  bool spawnRandomIf(BasicBlock *from, std::vector<BasicBlock *> *son,
                     Value *var, std::map<BasicBlock *, TreeNode *> *bb_info);

  std::vector<BasicBlock *> *getBlocks(Function *function,
                                       std::vector<BasicBlock *> *lists);
  unsigned int getUniqueNumber(std::vector<unsigned int> *rand_list);
};

bool FlaEnhPass::runOnFunction(Function *f) {

  if (f->isDeclaration() || f->hasAvailableExternallyLinkage()) {
    return false;
  }

  if (f->getName().starts_with("??") || f->getName().contains("std@")) {
    return false;
  }

  if (isMemberFunction(f) || f->hasCXXEH() || f->hasCXXSEH()) {
    return false;
  }


  const unsigned int enderNum = FlaEnhancedNum;
  std::vector<BasicBlock *> origBB;
  getBlocks(f, &origBB);
  if (origBB.size() <= 1)
    return false;
  [[maybe_unused]] unsigned int rand_val = cryptoutils->get_uint32_t();
  const auto tmp = f->begin();
  BasicBlock *oldEntry = &*tmp;
  origBB.erase(origBB.begin());
  BranchInst *firstBr = nullptr;
  if (isa<BranchInst>(oldEntry->getTerminator()))
    firstBr = cast<BranchInst>(oldEntry->getTerminator());
  BasicBlock *firstbb = oldEntry->getTerminator()->getSuccessor(0);
  if ((firstBr != nullptr && firstBr->isConditional()) ||
      oldEntry->getTerminator()->getNumSuccessors() >
          2) // Split the first basic block
  {
    auto iter = oldEntry->end();
    --iter;
    if (oldEntry->size() > 1)
      --iter;
    BasicBlock *splited = oldEntry->splitBasicBlock(iter, Twine("FirstBB"));
    firstbb = splited;
    origBB.insert(origBB.begin(), splited);
  }
  unsigned int retBlockNum = 0;
  for (const auto bb : origBB) {
    if (bb->getTerminator()->getNumSuccessors() == 0)
      retBlockNum++;
  }
  const unsigned int loopEndNum =
      (enderNum >= (origBB.size() - retBlockNum) ? (origBB.size() - retBlockNum)
                                                 : enderNum);
  BasicBlock *newEntry = oldEntry; // Prepare basic block
  BasicBlock *loopBegin =
      BasicBlock::Create(f->getContext(), "LoopBegin", f, newEntry);
  std::vector<BasicBlock *> loopEndBlocks;
  for (size_t i = 0; i < loopEndNum; i++) {
    BasicBlock *tmp =
        BasicBlock::Create(f->getContext(), "LoopEnd", f, newEntry);
    loopEndBlocks.push_back(tmp);
    BranchInst::Create(loopBegin, tmp);
  }

  newEntry->moveBefore(loopBegin);
  newEntry->getTerminator()->eraseFromParent();
  BranchInst::Create(loopBegin, newEntry);

  AllocaInst *switchVar =
      new AllocaInst(Type::getInt32Ty(f->getContext()), 0, Twine("switchVar"),
                     newEntry->getTerminator()); // Create switch variable
  std::map<BasicBlock *, TreeNode *> bb_map;
  std::map<BasicBlock *, unsigned int> nums_map;
  spawnRandomIf(loopBegin, &origBB, switchVar, &bb_map);
  unsigned int startNum = 0;
  for (auto bb : origBB) {
    unsigned int l = bb_map[bb]->l, r = bb_map[bb]->r;
    unsigned int val = cryptoutils->get_uint32_t() % (r - l) + l;
    nums_map[bb] = val;
    if (bb == firstbb)
      startNum = val;
  }
  const int every =
      (int)((double)(origBB.size() - retBlockNum) / (double)loopEndNum);

  
  //errs() << f->getName() << " " << every << " " << loopEndNum << " "
  //       << origBB.size() - retBlockNum << "\n";

  int counter = 0;
  auto end_iter = loopEndBlocks.begin();
  for (const auto block : origBB) // Handle successors
  {
    BasicBlock *loopEnd = *end_iter;
    if (block->getTerminator()->getNumSuccessors() == 1) {
      // errs()<<"This block has 1 successor\n";
      BasicBlock *succ = block->getTerminator()->getSuccessor(0);
      ConstantInt *caseNum = cast<ConstantInt>(
          ConstantInt::get(Type::getInt32Ty(f->getContext()), nums_map[succ]));
      block->getTerminator()->eraseFromParent();
      new StoreInst(caseNum, switchVar, block);
      BranchInst::Create(loopEnd, block);
      counter++;
    } else if (block->getTerminator()->getNumSuccessors() == 2) {
      // errs()<<"This block has 2 successors\n";
      BasicBlock *succTrue = block->getTerminator()->getSuccessor(0);
      BasicBlock *succFalse = block->getTerminator()->getSuccessor(1);
      ConstantInt *numTrue = cast<ConstantInt>(ConstantInt::get(
          Type::getInt32Ty(f->getContext()), nums_map[succTrue]));
      ConstantInt *numFalse = cast<ConstantInt>(ConstantInt::get(
          Type::getInt32Ty(f->getContext()), nums_map[succFalse]));
      BranchInst *oldBr = cast<BranchInst>(block->getTerminator());
      SelectInst *select =
          SelectInst::Create(oldBr->getCondition(), numTrue, numFalse,
                             Twine("choice"), block->getTerminator());
      block->getTerminator()->eraseFromParent();
      new StoreInst(select, switchVar, block);
      BranchInst::Create(loopEnd, block);
      counter++;
    }
    if (counter == every) {
      counter = 0;
      ++end_iter;
      if (end_iter == loopEndBlocks.end())
        --end_iter;
    }
  }
  ConstantInt *startVal = cast<ConstantInt>(ConstantInt::get(
      Type::getInt32Ty(f->getContext()), startNum)); // Set the entry value
  new StoreInst(startVal, switchVar, newEntry->getTerminator());
  fixStack(*f, false);
  //errs() << "Finish\n";
  return false;
}

std::list<TreeNode *>::iterator
FlaEnhPass::random_element(std::list<TreeNode *> *x) {
  auto iter = x->begin();
  const int val = x->size();
  for (uint32_t i = 0; i < cryptoutils->get_uint32_t() % val; i++)
    ++iter;
  return iter;
}

void FlaEnhPass::expandNode(TreeNode *node) {
  auto new_node = static_cast<TreeNode *>(malloc(sizeof(TreeNode)));
  new_node->left = new_node->right = nullptr;
  node->left = new_node;
  new_node = static_cast<TreeNode *>(malloc(sizeof(TreeNode)));
  new_node->left = new_node->right = nullptr;
  node->right = new_node;
}

int FlaEnhPass::genRandomTree(TreeNode *node, int node_limit) {
  std::list<TreeNode *> q;
  q.push_back(node);
  int node_num = 1;
  while (!q.empty() && node_num < node_limit) {
    auto tmp = random_element(&q);
    TreeNode *node = *tmp;
    const int val = (node->left == nullptr) + (node->right == nullptr);
    if (val == 2) {
      expandNode(node);
      q.push_back(node->left);
      q.push_back(node->right);
      q.erase(tmp);
    }
    node_num++;
  }
  q.clear();
  return node_num;
}

void FlaEnhPass::walkTree(TreeNode *node) {
  if (node->left != nullptr) // Traverse all branches
    walkTree(node->left);
  if (node->right != nullptr)
    walkTree(node->right);
  node->limit = 0;
  if (node->left == nullptr &&
      node->right == nullptr) // Start to calculate node info
    node->limit = 1;
  else
    node->limit = node->left->limit + node->right->limit;
}

bool FlaEnhPass::allocNode(TreeNode *node, unsigned l, unsigned r) {
  if (r - l < node->limit)
    return false;
  node->l = l;
  node->r = r;
  if (node->left == nullptr && node->right == nullptr)
    return true;
  unsigned int var;
  if (r - l - node->limit == 0)
    var = 0;
  else
    var = cryptoutils->get_uint32_t() % (r - l - node->limit);
  unsigned int mid = l + node->left->limit + var;
  if (!allocNode(node->left, l, mid) || !allocNode(node->right, mid, r))
    return false;
  return true;
}

BasicBlock *FlaEnhPass::createRandomBasicBlock(
    TreeNode *node, Function *f, Value *var,
    std::vector<BasicBlock *>::iterator &iter,
    std::map<BasicBlock *, TreeNode *> *bb_info) {
  if (node->left == nullptr && node->right == nullptr) {
    BasicBlock *bb = *iter;
    bb_info->insert(std::pair<BasicBlock *, TreeNode *>(bb, node));
    iter++;
    return bb;
  }
  BasicBlock *left_bb =
      createRandomBasicBlock(node->left, f, var, iter, bb_info);
  BasicBlock *right_bb =
      createRandomBasicBlock(node->right, f, var, iter, bb_info);
  BasicBlock *node_bb = BasicBlock::Create(f->getContext(), "knot", f);
  if (node->left->r != node->right->l)
    errs() << "Error!\n";
  LoadInst *load =
      new LoadInst(Type::getInt32Ty(f->getContext()), var, "", node_bb);
  ICmpInst *condition = new ICmpInst(
      *node_bb, ICmpInst::ICMP_ULT, load,
      ConstantInt::get(Type::getInt32Ty(f->getContext()), node->left->r));
  BranchInst::Create(left_bb, right_bb, (Value *)condition, node_bb);
  return node_bb;
}

bool FlaEnhPass::spawnRandomIf(BasicBlock *from, std::vector<BasicBlock *> *son,
                               Value *var,
                               std::map<BasicBlock *, TreeNode *> *bb_info) {
  TreeNode tree;
  genRandomTree(&tree, son->size());
  walkTree(&tree);
  if (!allocNode(&tree, 0, 0x7fffffff))
    return false;
  std::vector<BasicBlock *>::iterator iter = son->begin();
  BasicBlock *head =
      createRandomBasicBlock(&tree, from->getParent(), var, iter, bb_info);
  BranchInst::Create(head, from);
  return true;
}

std::vector<BasicBlock *> *
FlaEnhPass::getBlocks(Function *function, std::vector<BasicBlock *> *lists) {
  lists->clear();
  for (BasicBlock &basicBlock : *function)
    lists->push_back(&basicBlock);
  return lists;
}

unsigned FlaEnhPass::getUniqueNumber(std::vector<unsigned> *rand_list) {
  unsigned int num = cryptoutils->get_uint32_t();
  while (true) {
    bool state = true;
    for (const auto &n : *rand_list)
      if (n == num) {
        state = false;
        break;
      }
    if (state)
      break;
    num = cryptoutils->get_uint32_t();
  }
  return num;
}
} // namespace llvm

PreservedAnalyses FlatteningEnhanced::run(Function &F,
                                          FunctionAnalysisManager &AM)
{
  bool ret = false;
  if (toObfuscate(FlaEnhanced, &F, "x-fla-enh")) {
    LowerSwitchPass lower;
    lower.run(F, AM);
    FlaEnhPass x;
    ret = x.runOnFunction(&F);
  }
  if (ret) {
    turnOffOptimization(&F);
    return PreservedAnalyses::none();
  }
  return PreservedAnalyses::all();
}
