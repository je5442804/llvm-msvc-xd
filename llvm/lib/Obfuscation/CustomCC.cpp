//
// Ref to https://github.com/za233/Polaris-Obfuscator/
// copied by root on 12/11/2024.
//


#include "CustomCC.h"
#include "BogusControlFlow.h"
#include "CryptoUtils.h"
#include "Utils.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/IR/Verifier.h"

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
#include <VMFlatten.h>
#include <vector>

namespace llvm {
PreservedAnalyses CustomCC::run(Module &M, ModuleAnalysisManager &AM) {
  srand(time(NULL));
  static constexpr CallingConv::ID ObfuCCs[] = {
      CallingConv::Obfu1, CallingConv::Obfu2, CallingConv::Obfu3,
      CallingConv::Obfu4, CallingConv::Obfu5, CallingConv::Obfu6,
      CallingConv::Obfu7, CallingConv::Obfu8};
  for (Function &F : M) {
    if (readAnnotate(&F).find("custom-cc") != std::string::npos) {
      errs() << F.getName() << '\n';
      CallingConv::ID CC = ObfuCCs[getRandomNumber() % std::size(ObfuCCs)];
      F.setCallingConv(CC);
      FixInstrCallingConv(M, F, CC);
    }
  }

  return PreservedAnalyses::none();
}
void CustomCC::FixInstrCallingConv(Module &M, Function &Target,
                                   CallingConv::ID CC) {
  std::vector<CallInst *> CIs;
  for (Function &F : M) {
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (isa<CallInst>(I)) {
          CallInst *CI = (CallInst *)&I;
          Function *Func = CI->getCalledFunction();
         
          if (Func && Func == &Target) {
            errs()<<"debug call\n";
            CIs.push_back(CI);
          }
        }
      }
    }
  }
  for (CallInst *CI : CIs) {
    CI->setCallingConv(CC);
  }
  if (verifyModule(M, &errs())) {
    errs() << "Module verification failed after calling convention modification.\n";
  }
}
} // namespace llvm;
