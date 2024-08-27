//
// Created by root on 8/22/2024.
//

#ifndef GLOBALSENCRYPTION_H
#define GLOBALSENCRYPTION_H
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class GlobalsEncryption : public PassInfoMixin<GlobalsEncryption> {
  public:
    GlobalsEncryption()=default;
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  private:
    Function *buildDecryptFunction(Module &M);
    void process(Module &M);
  public:
    static bool isRequired() { return true; }
};
}
#endif //GLOBALSENCRYPTION_H
