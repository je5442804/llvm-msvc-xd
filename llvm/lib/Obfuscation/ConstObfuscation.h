#ifndef LLVM_CONST_OBFUSCATION_H
#define LLVM_CONST_OBFUSCATION_H

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
//#include <Obfuscation/PassRegistry.h>

namespace llvm {

struct ConstEncryption {
  ConstEncryption():CONTEXT(nullptr),vm_flag(false){}
  LLVMContext *CONTEXT;
  bool vm_flag;
  bool shouldEncryptConstant(Instruction *I);
  void bitwiseSubstitute(Instruction *I, int i);
  void linearSubstitute(Instruction *I, int i);
  void substitute(Instruction *I);
  bool runOnFunction(Function &F,bool vm_fla);
};

class ConstObfuscationPass : public PassInfoMixin<ConstObfuscationPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

} // namespace llvm

#endif