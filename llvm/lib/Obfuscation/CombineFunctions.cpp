#include "CombineFunctions.h"
#include "BogusControlFlow.h"
#include "ConstObfuscation.h"
#include "CryptoUtils.h"
#include "Flattening.h"
#include "IndirectGlobalVars.h"
#include "Utils.h"
#include "VMFlatten.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#ifdef _MSC_VER
#include <vcruntime_string.h>
#endif
#include <vector>

using namespace llvm;
static cl::opt<bool>
    RunCombineObfuscationPass("x-combine", cl::init(false),
                              cl::desc("OLLVM - Combine Functions"));

namespace {
struct CombineFunction {
  std::string CombineString;
  void getBlocks(Function *function, std::vector<BasicBlock *> &lists);
  void getFunctions(Module *module, std::vector<Function *> &lists);
  Function *Combine(std::vector<Function *> &func_list, ValueToValueMapTy *VMap,
                    Twine name, std::vector<unsigned int> &argPosList);
  unsigned int getUniqueNumber(std::vector<unsigned int> &rand_list);
  bool FixFunction(Function *target, std::vector<Function *> &orig_list,
                   ValueToValueMapTy *v_map,
                   std::vector<unsigned int> &value_list);
  bool FixCallInst(Function *target, std::vector<Function *> &orig_list,
                   std::vector<unsigned> &value_list,
                   std::vector<unsigned> &arg_pos_list);
  bool CombineFunctions(Module &m, std::vector<Function *> &func_list);
  bool runOnModule(Module &module);
};

void CombineFunction::getBlocks(Function *function,
                                std::vector<BasicBlock *> &lists) {
  lists.clear();
  for (BasicBlock &basicBlock : *function)
    lists.push_back(&basicBlock);
}

void CombineFunction::getFunctions(Module *module,
                                   std::vector<Function *> &lists) {

  lists.clear();
  for (Function &func : *module)
    lists.push_back(&func);
}

Function *CombineFunction::Combine(std::vector<Function *> &func_list,
                                   ValueToValueMapTy *VMap, Twine name,
                                   std::vector<unsigned> &argPosList) {
  if (func_list.empty())
    return nullptr;
  // errs() << "Check Function Type\n";
  for (const auto func : func_list) {
    if (func->isDeclaration() || func->hasAvailableExternallyLinkage() != 0 ||
        func->getFunctionType()->isVarArg() != false)
      return nullptr;
  }
  // errs() << "	Done\n";
  // errs() << "Prepare Function Type\n";
  std::vector<Type *> ArgTypes;
  for (const auto func : func_list) {
    for (Argument &I : func->args())
      ArgTypes.push_back(I.getType());
  }
  // errs() << "	Done\n";
  // errs() << "Check Function Return Type\n";
  Function *first = *func_list.begin();
  ArgTypes.push_back(Type::getInt32Ty(first->getParent()->getContext()));
  for (const auto func : func_list) {
    if (func->getFunctionType()->getReturnType() !=
        first->getFunctionType()->getReturnType())
      return nullptr;
    if (func->getParent() != first->getParent())
      return nullptr;
    if (func->getLinkage() != first->getLinkage())
      return nullptr;
  }
  FunctionType *fty = FunctionType::get(
      first->getFunctionType()->getReturnType(), ArgTypes, false);
  Function *result =
      Function::Create(fty, first->getLinkage(), first->getAddressSpace(), name,
                       first->getParent());
  Function ::arg_iterator iter = result->arg_begin();
  // errs() << "	Done\n";
  // errs() << "Start Working\n";
  unsigned int index = 0;
  for (const auto func : func_list) {
    argPosList.push_back(index);
    for (Argument &i : func->args())
      (*VMap)[&i] = &*iter++, index++;
  }
  ClonedCodeInfo CodeInfo;
  for (const auto func : func_list) {
    SmallVector<ReturnInst *, 8> returns;
    CloneAndPruneFunctionInto(result, func, *VMap,
                              func->getSubprogram() != nullptr, returns, "",
                              &CodeInfo);
  }
  // errs() << "	Done\n";
  return result;
}

unsigned CombineFunction::getUniqueNumber(std::vector<unsigned> &rand_list) {
  unsigned int num = cryptoutils->get_uint32_t();
  while (true) {
    bool state = true;
    for (const unsigned int &n : rand_list)
      if (n == num) {
        state = false;
        break;
      }
    if (state)
      break;
    num = cryptoutils->get_uint32_t();
  }
  rand_list.push_back(num);
  return num;
}

bool CombineFunction::FixFunction(Function *target,
                                  std::vector<Function *> &orig_list,
                                  ValueToValueMapTy *v_map,
                                  std::vector<unsigned> &value_list) {
  std::vector<BasicBlock *> entryBlocks;
  std::vector<BasicBlock *> bodyBlock;
  // errs() << "Get all entry blocks\n";
  for (const auto func : orig_list) {
    const BasicBlock *entry = &*func->begin();
    if (const auto ptr = static_cast<Value *>(v_map->lookup(entry));
        isa<BasicBlock>(*ptr))
      entryBlocks.push_back(static_cast<BasicBlock *>(ptr));
    else
      return false;
  }
  getBlocks(target, bodyBlock);
  // errs() << "	Done\n";
  // errs() << "Build switch\n";
  BasicBlock *entry = BasicBlock::Create(target->getContext(), "Entry", target);
  BasicBlock *selector =
      BasicBlock::Create(target->getContext(), "Selector", target);
  entry->moveBefore(*entryBlocks.begin());
  selector->moveBefore(*entryBlocks.begin());
  const auto var = new AllocaInst(Type::getInt32Ty(target->getContext()), 0,
                                  Twine("switchVar"), entry);
  Function::arg_iterator iter = target->arg_end();
  Value *control_arg = --iter;
  new StoreInst(control_arg, var, entry);
  BranchInst::Create(selector, entry);
  const auto load =
      new LoadInst(var->getAllocatedType(), var, Twine(""), selector);
  BasicBlock *end_block =
      BasicBlock::Create(target->getContext(), "DefaultEnd", target);
  [[maybe_unused]] ReturnInst *ret = ReturnInst::Create(
      target->getContext(),
      Constant::getNullValue(target->getFunctionType()->getReturnType()),
      end_block);
  SwitchInst *sw = SwitchInst::Create(load, end_block, 0, selector);
  std::vector<unsigned int> rand_list;
  auto bblist_iter = entryBlocks.begin();
  for (auto f = orig_list.begin(); f != orig_list.end(); ++f) {
    unsigned int val = getUniqueNumber(rand_list);
    // errs() << val << '\n';
    ConstantInt *num_case =
        cast<ConstantInt>(ConstantInt::get(sw->getCondition()->getType(), val));
    value_list.push_back(val);
    sw->addCase(num_case, *bblist_iter);

    ++bblist_iter;
  }
  // errs() << "	Done\n";
  // errs() << "Add useless code\n";
  for (const auto basic_block : bodyBlock) {
    if (isa<BranchInst>(*basic_block->getTerminator())) {
      if (const auto br =
              reinterpret_cast<BranchInst *>(basic_block->getTerminator());
          br->isUnconditional()) {
        BasicBlock *rand_target = entryBlocks.at(rand() % entryBlocks.size());
        BasicBlock *right = basic_block->getTerminator()->getSuccessor(0);
        basic_block->getTerminator()->eraseFromParent();
        const unsigned int val = getUniqueNumber(rand_list);
        const auto cmp_val_a =
            new LoadInst(var->getAllocatedType(), var, Twine(""), basic_block);
        ConstantInt *cmp_val_b =
            ConstantInt::get(Type::getInt32Ty(target->getContext()), val);
        const auto condition =
            new ICmpInst(*basic_block, ICmpInst::ICMP_EQ, cmp_val_a, cmp_val_b);
        BranchInst::Create(rand_target, right, condition, basic_block);
      }
    }
  }
  // errs() << "	Done\n";
  return true;
}

bool CombineFunction::FixCallInst(Function *target,
                                  std::vector<Function *> &orig_list,
                                  std::vector<unsigned> &value_list,
                                  std::vector<unsigned> &arg_pos_list) {
  auto v = value_list.begin();
  auto a = arg_pos_list.begin();
  std::vector<CallInst *> remove_list;
  for (auto f = orig_list.begin(); f != orig_list.end(); ++f, ++v, ++a) {
    const unsigned int val = *v;
    const unsigned int arg_pos = *a;
    Function *ff = *f;
    for (Function &func : *ff->getParent())
      for (BasicBlock &bb : func)
        for (Instruction &ii : bb) {
          if (isa<CallInst>(ii)) {
            if (auto call_inst = &cast<CallInst>(ii);
                call_inst->getCalledFunction() == ff) {
              std::vector<Value *> arg_list;
              Function ::arg_iterator iter_a = target->arg_begin();
              User::op_iterator iter_b = call_inst->arg_begin();
              for (size_t i = 0; i < target->arg_size() - 1; i++, iter_a++) {
                if (i >= arg_pos && i < arg_pos + call_inst->arg_size()) {
                  arg_list.push_back(*iter_b);
                  iter_b++;
                } else
                  arg_list.push_back(Constant::getNullValue(iter_a->getType()));
              }
              arg_list.push_back(ConstantInt::get(
                  Type::getInt32Ty(target->getContext()), val));
              CallInst *new_call =
                  CallInst::Create(target, arg_list, Twine(""), call_inst);
              remove_list.push_back(call_inst);
              call_inst->replaceAllUsesWith(new_call);
            }
          }
        }
  }
  for (const auto &c : remove_list)
    c->eraseFromParent();
  return true;
}

bool CombineFunction::CombineFunctions(Module &m,
                                       std::vector<Function *> &func_list) {
  ValueToValueMapTy VMap;
  std::string funcName = formatv("mix.{0:x-}", cryptoutils->get_uint64_t());
  std::vector<unsigned int> values, argPos;
  Function *target = Combine(func_list, &VMap, funcName, argPos);
  if (target == nullptr) {
    // errs() << "Combine Fail\n";
    return false;
  }

  if (!FixFunction(target, func_list, &VMap, values)) {
    // errs() << "FixFunction Fail\n";
    return false;
  }
  if (!FixCallInst(target, func_list, values, argPos)) {
    // errs() << "FixCallInst Fail\n";
    return false;
  }
  // m.getGlobalVariable("llvm.global.annotations")->eraseFromParent();
  for (const auto &func : func_list) {
    func->eraseFromParent();
  }
  return true;
}

bool CombineFunction::runOnModule(Module &module) {
  std::vector<Function *> func_list;
  getFunctions(&module, func_list);

  std::string rgx_str = CombineString + "\\[(.*)\\]";
  std::regex rgx(rgx_str);
  std::unordered_map<std::string, std::vector<Function *>> work_map;

  // errs() << "Function List:\n";
  for (auto &func : func_list) {
    // errs() << func->getName().str() << "\n";
    std::smatch match;
    std::string ann = readAnnotate(func);
    if (std::regex_search(ann, match, rgx) && match.size() > 1) {
      // errs() << "		-Add to work list "<<combine_string<<"\n";
      std::string tag_name = match.str(1);
      if (work_map.find(tag_name) != work_map.end()) {
        work_map[tag_name].push_back(func);
      } else {
        std::vector<Function *> tmp_list{};
        tmp_list.push_back(func);
        work_map.emplace(tag_name, tmp_list);
      }
    }
  }
  bool ret = false;
  for (auto &[tag_name, work_list] : work_map)
    ret |= CombineFunctions(module, work_list);
  // errs() << "combine done\n";
  return ret;
}
} // namespace

PreservedAnalyses CombineFunctionsPass::run(Module &M,
                                            ModuleAnalysisManager &AM) {

  if (RunCombineObfuscationPass) {
    CombineFunction a;
    a.CombineString = this->combine_str;
    if (a.runOnModule(M))
      return PreservedAnalyses::none();
  }
  return PreservedAnalyses::all();
}

CombineFunctionsPass::CombineFunctionsPass() { combine_str = "combine_func"; }
