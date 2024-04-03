#ifndef LLVM_EASYCFG_OBFUSCATION_H
#define LLVM_EASYCFG_OBFUSCATION_H

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
//#include <Obfuscation/PassRegistry.h>

namespace llvm {

class EasyCfgPass : public PassInfoMixin<EasyCfgPass> {
public:
	PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
    static bool isRequired() { return true; }
};
} // namespace llvm

#endif