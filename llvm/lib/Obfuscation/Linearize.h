#ifndef LLVM_LINEAR_OBFUSCATION_H
#define LLVM_LINEAR_OBFUSCATION_H

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
//#include <Obfuscation/PassRegistry.h>

namespace llvm {

class Linearize : public PassInfoMixin<Linearize> {
public:
	PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
    static bool isRequired() { return true; }
};
} // namespace llvm

#endif