//
// Created by root on 8/22/2024.
//

#ifndef ALIASACCESS_H
#define ALIASACCESS_H
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class AliasAccess : public PassInfoMixin<AliasAccess> {
public:
  AliasAccess()=default;
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
private:
  Function *buildGetterFunction(Module &M, StructType *ST, unsigned Index);
  void process(Function &F);
public:
  static bool isRequired() { return true; }
};
}
#endif //ALIASACCESS_H
