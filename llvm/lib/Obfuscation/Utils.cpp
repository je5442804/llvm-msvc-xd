#include "Utils.h"

#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Local.h" // For DemoteRegToStack and DemotePHIToStack
#include <sstream>

using namespace llvm;

void turnOffOptimization(Function *f) {
  f->removeFnAttr(Attribute::AttrKind::MinSize);
  f->removeFnAttr(Attribute::AttrKind::OptimizeForSize);
  //f->removeFnAttr(Attribute::HasSEH);
  //f->removeFnAttr(Attribute::HasCXXSEH);
  if (!f->hasFnAttribute(Attribute::AttrKind::OptimizeNone)) {
    f->addFnAttr(Attribute::AttrKind::OptimizeNone);
    f->addFnAttr(Attribute::AttrKind::NoInline);
  }
}


std::string readAnnotate(Function *f) {
  std::string annotation = "";

  // Get annotation variable
  GlobalVariable *glob =
      f->getParent()->getGlobalVariable("llvm.global.annotations");

  if (glob != NULL) {
    // Get the array
    if (ConstantArray *ca = dyn_cast<ConstantArray>(glob->getInitializer())) {
      for (unsigned i = 0; i < ca->getNumOperands(); ++i) {
        // Get the struct
        if (ConstantStruct *structAn =
                dyn_cast<ConstantStruct>(ca->getOperand(i))) {
          if (ConstantExpr *expr =
                  dyn_cast<ConstantExpr>(structAn->getOperand(0))) {
            // If it's a bitcast we can check if the annotation is concerning
            // the current function
            if (expr->getOpcode() == Instruction::BitCast &&
                expr->getOperand(0) == f) {
              ConstantExpr *note = cast<ConstantExpr>(structAn->getOperand(1));
              // If it's a GetElementPtr, that means we found the variable
              // containing the annotations
              if (note->getOpcode() == Instruction::GetElementPtr) {
                if (GlobalVariable *annoteStr =
                        dyn_cast<GlobalVariable>(note->getOperand(0))) {
                  if (ConstantDataSequential *data =
                          dyn_cast<ConstantDataSequential>(
                              annoteStr->getInitializer())) {
                    if (data->isString()) {
                      annotation += data->getAsString().lower() + " ";
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  if(!f->getAnnotationStrings().empty())
    annotation+=std::string(f->getAnnotationStrings().data());
  return annotation;
}

bool isMemberFunction(Function *F)
{
  // if (F->arg_size() == 0)
  //     return false;


  // llvm::Argument &arg = *(F->arg_begin());

  // const auto type = arg.getType();
  // if(!type)
  //     return false;

  // if (!type->isPointerTy())
  //     return false;

  // if(!type->getNumContainedTypes())
  //     return false;

  // llvm::Type *pointeeType = type->getPointerElementType();
  // if(!pointeeType)
  //     return false;

  // return pointeeType->isStructTy();

  const std::string function_name = F->getName().str();


  const std::string demangled_name = llvm::demangle(function_name);


  if (demangled_name.find("::") != std::string::npos)
  {
    //errs()<<"class func = "<<demangled_name<<"\r\n";
    return true;
  }
  return false;
}


bool toObfuscate(bool flag, Function *f, std::string attribute) {
  std::string attr = attribute;
  std::string attrNo = "no" + attr;

  // Check if declaration
  if (f->isDeclaration()) {
    return false;
  }

  // Check external linkage
  if (f->hasAvailableExternallyLinkage() != 0) {
    return false;
  }

  // We have to check the nofla flag first
  // Because .find("fla") is true for a string like "fla" or
  // "nofla"
  if (readAnnotate(f).find(attrNo) != std::string::npos) {
    return false;
  }

  // If fla annotations
  if (readAnnotate(f).find(attr) != std::string::npos) {
    return true;
  }

  // If fla flag is set
  if (flag == true) {
    /* Check if the number of applications is correct
    if (!((Percentage > 0) && (Percentage <= 100))) {
      LLVMContext &ctx = llvm::getGlobalContext();
      ctx.emitError(Twine("Flattening application function\
              percentage -perFLA=x must be 0 < x <= 100"));
    }
    // Check name
    else if (func.size() != 0 && func.find(f->getName()) != std::string::npos) {
      return true;
    }

    if ((((int)llvm::cryptoutils->get_range(100))) < Percentage) {
      return true;
    }
    */
    return true;
  }

  return false;
}

bool valueEscapes(const Instruction &Inst) {
  if (!Inst.getType()->isSized())
    return false;

  const BasicBlock *bb = Inst.getParent();
  for (const User *u : Inst.users()) {
    auto ui = cast<Instruction>(u);
    if (ui->getParent() != bb || isa<PHINode>(ui))
      return true;
  }
  return false;
}
void fixStack(Function &F,bool use_alloc) {
  // Try to remove phi node and demote reg to stack
  // Try to remove phi node and demote reg to stack
  std::vector<PHINode *> tmpPhi;
  std::vector<Instruction *> tmpReg;
  BasicBlock *bbEntry = &*F.begin();
  // Find first non-alloca instruction and create insertion point. This is
  // safe if block is well-formed: it always have terminator, otherwise
  // we'll get and assertion.
  BasicBlock::iterator I = bbEntry->begin();
  while (isa<AllocaInst>(I))
    ++I;
  Instruction *AllocaInsertionPoint = &*I;
  do {
    tmpPhi.clear();
    tmpReg.clear();
    for (BasicBlock &i : F) {
      for (Instruction &j : i) {
        if (isa<PHINode>(&j)) {
          PHINode *phi = cast<PHINode>(&j);
          tmpPhi.emplace_back(phi);
          continue;
        }
        if (!(isa<AllocaInst>(&j) && j.getParent() == bbEntry) &&
            (valueEscapes(j) || j.isUsedOutsideOfBlock(&i))) {
          tmpReg.emplace_back(&j);
          continue;
        }
      }
    }
    for (Instruction *I : tmpReg)
      if(use_alloc)
          DemoteRegToStack(*I, false, AllocaInsertionPoint);
      else
          DemoteRegToStack(*I);

    for (PHINode *P : tmpPhi)
      if(use_alloc )DemotePHIToStack(P, AllocaInsertionPoint);
      else DemotePHIToStack(P);
  } while (tmpReg.size() != 0 || tmpPhi.size() != 0);
}

void OutputIR(Function &Func) {
  for (auto &BB : Func) {
    for (auto &Inst : BB) {
      Inst.print(errs());
      errs() << "\n";
    }
  }
}

void LowerConstantExpr(Function &F) {
  SmallPtrSet<Instruction *, 8> WorkList;

  for (inst_iterator It = inst_begin(F), E = inst_end(F); It != E; ++It) {
    Instruction *I = &*It;

    if (isa<LandingPadInst>(I) || isa<CatchPadInst>(I) || isa<CatchSwitchInst>(I) || isa<CatchReturnInst>(I))
      continue;
    if (auto *II = dyn_cast<IntrinsicInst>(I)) {
      if (II->getIntrinsicID() == Intrinsic::eh_typeid_for) {
        continue;
      }
    }

    for (unsigned int i = 0; i < I->getNumOperands(); ++i) {
      if (isa<ConstantExpr>(I->getOperand(i)))
        WorkList.insert(I);
    }
  }

  while (!WorkList.empty()) {
    auto It = WorkList.begin();
    Instruction *I = *It;
    WorkList.erase(*It);

    if (PHINode *PHI = dyn_cast<PHINode>(I)) {
      for (unsigned int i = 0; i < PHI->getNumIncomingValues(); ++i) {
        Instruction *TI = PHI->getIncomingBlock(i)->getTerminator();
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(PHI->getIncomingValue(i))) {
          Instruction *NewInst = CE->getAsInstruction();
          NewInst->insertBefore(TI);
          PHI->setIncomingValue(i, NewInst);
          WorkList.insert(NewInst);
        }
      }
    } else {
      for (unsigned int i = 0; i < I->getNumOperands(); ++i) {
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(I->getOperand(i))) {
          Instruction *NewInst = CE->getAsInstruction();
          NewInst->insertBefore(I);
          I->replaceUsesOfWith(CE, NewInst);
          WorkList.insert(NewInst);
        }
      }
    }
  }
}
uint64_t getRandomNumber() {
  return (((uint64_t)rand()) << 32) | ((uint64_t)rand());
}

uint32_t getUniqueNumber(std::vector<uint32_t> &rand_list) {
  uint32_t num = getRandomNumber() & 0xffffffff;
  while (true) {
    bool state = true;
    for (auto n = rand_list.begin(); n != rand_list.end(); n++) {
      if (*n == num) {
        state = false;
        break;
      }
    }

    if (state)
      break;
    num = getRandomNumber() & 0xffffffff;
  }
  return num;
}

void getRandomNoRepeat(unsigned upper_bound, unsigned size,
                       std::vector<unsigned> &result) {
  assert(upper_bound >= size);
  std::vector<unsigned> list;
  for (unsigned i = 0; i < upper_bound; i++) {
    list.push_back(i);
  }

  std::shuffle(list.begin(), list.end(), std::default_random_engine());
  for (unsigned i = 0; i < size; i++) {
    result.push_back(list[i]);
  }
}

