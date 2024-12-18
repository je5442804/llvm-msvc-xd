//
// Ref to https://github.com/za233/Polaris-Obfuscator/
// copied by root on 12/11/2024.
//

#ifndef CUSTOMCC_H
#define CUSTOMCC_H
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class CustomCC : public PassInfoMixin<CustomCC> {
public:
  CustomCC()=default;
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
private:
  void FixInstrCallingConv(Module &M, Function &Target, CallingConv::ID CC);
public:
  static bool isRequired() { return true; }
};
}

#endif //CUSTOMCC_H
