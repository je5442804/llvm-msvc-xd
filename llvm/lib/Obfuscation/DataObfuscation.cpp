#include "DataObfuscation.h"

#include "ConstObfuscation.h"
#include "CryptoUtils.h"
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
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FormatVariadic.h"



#include <iomanip>
#include <set>
#include <sstream>
#include <vector>

using namespace llvm;
static cl::opt<bool>
    RunDataObfuscationPass("data-obfus", cl::init(false),
                           cl::desc("OLLVM - DataObfuscationPass"));

static cl::opt<int>
    DataObfuProbRate("data-prob", cl::init(100),
                     cl::desc("Choose the probability <data-prob> will "
                              "be obfuscated by DataObfuscationPass"));
static cl::opt<int>
    ObfDataTimes("data-times", cl::init(1),
                 cl::desc("Run DataObfuscationPass <data-times> time(s)"));
namespace llvm {
struct DataObfuscator {
  LLVMContext *CONTEXT;
  DataObfuscator() : CONTEXT(nullptr) {}
  // void choose(int range, SetVector<int> *selected, SetVector<int> *unselect,
  //             int count);

  // void removeSet(SetVector<int> &all, SetVector<int> &remove,
  //                SetVector<int> &out);
  // void collectAlloca(Function &f, SetVector<AllocaInst *> &allocaList);
  // void dataFlowObfu(Function &f, SetVector<AllocaInst *> &allocaList);
  // void doObfu(Function &f, SetVector<AllocaInst *> &varList, int size);
  // void rubbishCode(IRBuilder<> &irb, SetVector<AllocaInst *> &array,
  //                  SetVector<int> &vars, int prob);
  bool runOnFunction(Function &F);
  Value *insertFastRem3(Value *n, BasicBlock *insertAfter);

  Value *genRandIndex(Instruction *I);

  void substitute(Instruction *I, int i);
};

bool DataObfuscator::runOnFunction(Function &F) {
  // SetVector<AllocaInst *> allocaInsts;
  // collectAlloca(f, allocaInsts);
  // dataFlowObfu(f, allocaInsts);
  // fixStack(f);
  CONTEXT = &F.getContext();
  if (F.getName().starts_with("genrand.")) {
    return false;
  }
  for (int i = 0; i < ObfDataTimes; i++) {
    for (BasicBlock &BB : F) {
      std::vector<Instruction *> origInst;
      for (Instruction &I : BB) {
        origInst.push_back(&I);
      }
      for (Instruction *I : origInst) {
        if (isa<StoreInst>(I) || isa<CmpInst>(I) || isa<BinaryOperator>(I)) {
          const int operand_num = I->getNumOperands();
          for (int i = 0; i < operand_num; i++) {
            if (isa<ConstantInt>(I->getOperand(i)) &&
                cryptoutils->get_uint8_t() % 100 < DataObfuProbRate) {
              substitute(I, i);
            }
          }
        }
      }
    }
  }
  return true;
}

Value *DataObfuscator::insertFastRem3(Value *n, BasicBlock *insertAfter) {
  IRBuilder<> builder(*CONTEXT);
  builder.SetInsertPoint(insertAfter);
  Value *op1, *op2, *op3, *op4, *op5, *op6;
  op1 = builder.CreateZExt(n, TYPE_I64);
  op2 = builder.CreateMul(CONST_I64(0xAAAAAAAB), op1);
  op3 = builder.CreateLShr(op2, 33);
  op4 = builder.CreateTrunc(op3, TYPE_I32);
  op5 = builder.CreateMul(op4, CONST_I32(3));
  op6 = builder.CreateSub(n, op5);
  return op6;
}

Value *DataObfuscator::genRandIndex(Instruction *I) {
  Module &M = *I->getModule();
  std::string funcName = formatv("genrand.{0:x-}", cryptoutils->get_uint64_t());
  Function *genFunc = cast<Function>(
      M.getOrInsertFunction(funcName, FunctionType::getInt32Ty(*CONTEXT))
          .getCallee());
  if (get_vm_fla_level()==7)
  {
    genFunc->setAnnotationStrings("x-vm,x-full");
  }
  else {
    genFunc->setAnnotationStrings("combine_func[grand]");
  }
  
  BasicBlock *entry = BasicBlock::Create(*CONTEXT, "entry", genFunc);
  IRBuilder<> builder(*CONTEXT);
  builder.SetInsertPoint(entry);
  Function *randFunc = cast<Function>(
      M.getOrInsertFunction("rand", FunctionType::getInt32Ty(*CONTEXT))
          .getCallee());
  randFunc->setDSOLocal(true);
  Value *op1, *op2;
  op1 = builder.CreateCall(randFunc->getFunctionType(), randFunc);
  op2 = insertFastRem3(op1, entry);
  builder.CreateRet(op2);
  //ConstEncryption str;
  //str.runOnFunction(*genFunc);
  builder.SetInsertPoint(I);
  return builder.CreateCall(genFunc->getFunctionType(), genFunc);
}

void DataObfuscator::substitute(Instruction *I, int i) {
  Module &M = *I->getModule();
  ConstantInt *val = cast<ConstantInt>(I->getOperand(i));
  IntegerType *eleType = cast<IntegerType>(val->getType());
  ArrayType *arrayType = ArrayType::get(eleType, 3);
  GlobalVariable *valTriple = new GlobalVariable(
      M, arrayType, true, GlobalValue::PrivateLinkage,
      (ConstantArray *)ConstantArray::get(arrayType, {val, val, val}));
  Value *randIndex = genRandIndex(I);
  IRBuilder<> builder(*CONTEXT);
  builder.SetInsertPoint(I);
  Value *anyElePtr =
      builder.CreateGEP(arrayType, valTriple, {CONST_I32(0), randIndex});
  LoadInst *anyEle = builder.CreateLoad(eleType, anyElePtr);
  I->setOperand(i, anyEle);
}

// void DataObfuscator::choose(int range, SetVector<int> *selected,
//                             SetVector<int> *unselect, int count) {
//   bool *used = (bool *)malloc(sizeof(bool) * range);
//   memset(used, 0, sizeof(used));
//   for (int i = 0; i < count; i++) {
//     int id = cryptoutils->get_uint32_t() % range;
//     while (used[id])
//       id = cryptoutils->get_uint32_t() % range;
//     used[id] = true;
//     selected->insert(id);
//   }
//   if (unselect != nullptr) {
//     for (int i = 0; i < range; i++)
//       if (!used[i])
//         unselect->insert(i);
//   }
//   free(used);
// }
//
// void DataObfuscator::removeSet(SetVector<int> &all, SetVector<int> &remove,
//                                SetVector<int> &out) {
//   for (int v : all) {
//     bool ok = true;
//     for (const int r : remove) {
//       if (v == r) {
//         ok = false;
//         break;
//       }
//     }
//     if (ok)
//       out.insert(v);
//   }
// }
//
// void DataObfuscator::collectAlloca(Function &f,
//                                    SetVector<AllocaInst *> &allocaList) {
//   for (BasicBlock &bb : f)
//     for (Instruction &instr : bb)
//       if (isa<AllocaInst>(instr))
//         allocaList.insert(static_cast<AllocaInst *>(&instr));
// }
//
// void DataObfuscator::dataFlowObfu(Function &f,
//                                   SetVector<AllocaInst *> &allocaList) {
//   SetVector<AllocaInst *> charList, shortList, intList, longList;
//   for (AllocaInst *a : allocaList) {
//     Type *type = a->getAllocatedType();
//     if (type->isIntegerTy()) {
//       IntegerType *intType = (IntegerType *)type;
//       if (intType->getBitWidth() == 8)
//         charList.insert(a);
//       else if (intType->getBitWidth() == 16)
//         shortList.insert(a);
//       else if (intType->getBitWidth() == 32)
//         intList.insert(a);
//       else if (intType->getBitWidth() == 64)
//         longList.insert(a);
//     }
//   }
//   doObfu(f, charList, charList.size() + 10);
//   doObfu(f, shortList, shortList.size() + 10);
//   doObfu(f, intList, intList.size() + 10);
//   doObfu(f, longList, longList.size() + 10);
//   for (AllocaInst *a : charList)
//     a->eraseFromParent();
//   for (AllocaInst *a : shortList)
//     a->eraseFromParent();
//   for (AllocaInst *a : intList)
//     a->eraseFromParent();
//   for (AllocaInst *a : longList)
//     a->eraseFromParent();
// }
//
// void DataObfuscator::doObfu(Function &f, SetVector<AllocaInst *> &varList,
//                             int size) {
//   int prob = 100;
//   MapVector<AllocaInst *, int> indexMap;
//   if (varList.size() <= 0)
//     return;
//   Type *type = varList[0]->getAllocatedType();
//   SetVector<int> freeId, usedId;
//   choose(size, &usedId, &freeId, varList.size());
//   IRBuilder<> irb(&*f.getEntryBlock().getFirstInsertionPt());
//   SetVector<AllocaInst *> array;
//   for (int i = 0; i < size; i++)
//     array.insert(irb.CreateAlloca(type));
//   BasicBlock::iterator entry = irb.GetInsertPoint();
//   for (BasicBlock &bb : f) {
//     BasicBlock::iterator iter = bb.begin();
//     if (&bb == &f.getEntryBlock())
//       iter = entry;
//     while (iter != bb.end()) {
//       irb.SetInsertPoint(&*iter);
//       rubbishCode(irb, array, freeId, prob);
//       iter++;
//     }
//   }
//   int idx = 0;
//   for (AllocaInst *a : varList) {
//     int id = usedId[idx++];
//     Value *zero = ConstantInt::get(Type::getInt32Ty(f.getContext()), 0);
//     for (BasicBlock &bb : f)
//       for (Instruction &inst : bb) {
//         if (isa<LoadInst>(inst) && inst.getOperand(0) == a) {
//           SetVector<int> used, unused;
//           choose(freeId.size(), &used, &unused, 3);
//           SetVector<int> rubbishVars;
//           for (int v : unused)
//             rubbishVars.insert(freeId[v]);
//           Value *vr = ConstantInt::get(type, cryptoutils->get_uint32_t());
//           irb.SetInsertPoint(&inst);
//           Value *ga = array[freeId[used[0]]];
//           Value *gb = array[freeId[used[1]]];
//           Value *gc = array[freeId[used[2]]];
//           if (cryptoutils->get_uint32_t() % 2) {
//             irb.CreateStore(
//                 irb.CreateAdd(irb.CreateLoad(ga->getType(), ga), vr), gb);
//             rubbishCode(irb, array, rubbishVars, prob);
//             rubbishCode(irb, array, rubbishVars, prob);
//             irb.CreateStore(
//                 irb.CreateAdd(irb.CreateLoad(ga->getType(), ga),
//                               irb.CreateLoad(array[id]->getType(),
//                               array[id])),
//                 ga);
//             rubbishCode(irb, array, rubbishVars, prob);
//             rubbishCode(irb, array, rubbishVars, prob);
//             irb.CreateStore(
//                 irb.CreateAdd(vr,
//                               irb.CreateSub(irb.CreateLoad(ga->getType(),
//                               ga),
//                                             irb.CreateLoad(gb->getType(),
//                                             gb))),
//                 gc);
//             rubbishCode(irb, array, rubbishVars, prob);
//             rubbishCode(irb, array, rubbishVars, prob);
//           } else {
//             irb.CreateStore(
//                 irb.CreateXor(irb.CreateLoad(ga->getType(), ga), vr), gb);
//             rubbishCode(irb, array, rubbishVars, prob);
//             rubbishCode(irb, array, rubbishVars, prob);
//             irb.CreateStore(
//                 irb.CreateXor(irb.CreateLoad(ga->getType(), ga),
//                               irb.CreateLoad(array[id]->getType(),
//                               array[id])),
//                 ga);
//             rubbishCode(irb, array, rubbishVars, prob);
//             rubbishCode(irb, array, rubbishVars, prob);
//             irb.CreateStore(
//                 irb.CreateXor(vr,
//                               irb.CreateXor(irb.CreateLoad(ga->getType(),
//                               ga),
//                                             irb.CreateLoad(gb->getType(),
//                                             gb))),
//                 gc);
//             rubbishCode(irb, array, rubbishVars, prob);
//             rubbishCode(irb, array, rubbishVars, prob);
//           }
//
//           inst.setOperand(0, gc);
//         } else if (isa<StoreInst>(inst) && inst.getOperand(1) == a) {
//           Value *val = inst.getOperand(0);
//           SetVector<int> used, unused;
//           choose(freeId.size(), &used, &unused, 3);
//           SetVector<int> rubbishVars;
//           for (int v : unused)
//             rubbishVars.insert(freeId[v]);
//           Value *vr = ConstantInt::get(type, cryptoutils->get_uint32_t());
//           irb.SetInsertPoint(&inst);
//           Value *ga = array[freeId[used[0]]];
//           Value *gb = array[freeId[used[1]]];
//           Value *gc = array[freeId[used[2]]];
//           if (cryptoutils->get_uint32_t() % 2) {
//             irb.CreateStore(
//                 irb.CreateAdd(irb.CreateLoad(ga->getType(), ga), vr), gb);
//             rubbishCode(irb, array, rubbishVars, prob);
//             rubbishCode(irb, array, rubbishVars, prob);
//             irb.CreateStore(
//                 irb.CreateAdd(irb.CreateLoad(ga->getType(), ga), val), ga);
//             rubbishCode(irb, array, rubbishVars, prob);
//             rubbishCode(irb, array, rubbishVars, prob);
//             irb.CreateStore(
//                 irb.CreateAdd(vr,
//                               irb.CreateSub(irb.CreateLoad(ga->getType(),
//                               ga),
//                                             irb.CreateLoad(gb->getType(),
//                                             gb))),
//                 gc);
//             rubbishCode(irb, array, rubbishVars, prob);
//             rubbishCode(irb, array, rubbishVars, prob);
//           } else {
//             irb.CreateStore(
//                 irb.CreateXor(irb.CreateLoad(ga->getType(), ga), vr), gb);
//             rubbishCode(irb, array, rubbishVars, prob);
//             rubbishCode(irb, array, rubbishVars, prob);
//             irb.CreateStore(
//                 irb.CreateXor(irb.CreateLoad(ga->getType(), ga), val), ga);
//             rubbishCode(irb, array, rubbishVars, prob);
//             rubbishCode(irb, array, rubbishVars, prob);
//             irb.CreateStore(
//                 irb.CreateXor(vr,
//                               irb.CreateXor(irb.CreateLoad(ga->getType(),
//                               ga),
//                                             irb.CreateLoad(gb->getType(),
//                                             gb))),
//                 gc);
//             rubbishCode(irb, array, rubbishVars, prob);
//             rubbishCode(irb, array, rubbishVars, prob);
//           }
//           inst.setOperand(0, irb.CreateLoad(gc->getType(), gc));
//           inst.setOperand(1, array[id]);
//         } else {
//           int c = 0;
//           for (Value *ops : inst.operands()) {
//             if (ops == a) {
//               irb.SetInsertPoint(&inst);
//               inst.setOperand(c, array[id]);
//             }
//
//             c++;
//           }
//         }
//       }
//   }
// }
//
// void DataObfuscator::rubbishCode(IRBuilder<> &irb,
//                                  SetVector<AllocaInst *> &array,
//                                  SetVector<int> &vars, int prob) {
//   if (cryptoutils->get_uint32_t() % 100 > prob)
//     return;
//   SetVector<int> idx;
//   choose(vars.size(), &idx, nullptr, 2);
//   Value *l = (Value *)array[vars[idx[0]]];
//   Value *r;
//   int op = cryptoutils->get_uint32_t() % 4;
//   int o = cryptoutils->get_uint32_t() % 2;
//   if (o)
//     r = ConstantInt::get(array[0]->getAllocatedType(),
//     cryptoutils->get_uint32_t());
//   else
//     r = irb.CreateLoad(array[vars[idx[1]]]->getType(), array[vars[idx[1]]]);
//   if (op == 0)
//     irb.CreateStore(irb.CreateAdd(irb.CreateLoad(l->getType(), l), r), l);
//   else if (op == 1)
//     irb.CreateStore(irb.CreateSub(irb.CreateLoad(l->getType(), l), r), l);
//   else if (op == 2)
//     irb.CreateStore(irb.CreateMul(irb.CreateLoad(l->getType(), l), r), l);
//   else
//     irb.CreateStore(irb.CreateXor(irb.CreateLoad(l->getType(), l), r), l);
// }

} // namespace llvm

PreservedAnalyses DataObfuscationPass::run(Function &F,
                                           FunctionAnalysisManager &AM) {
  DataObfuscator s;
  if (toObfuscate(RunDataObfuscationPass, &F, "data-obfus")) {
    if (s.runOnFunction(F))
      return PreservedAnalyses::none();
  }
  return PreservedAnalyses::all();
}