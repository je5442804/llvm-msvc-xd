//
// Created by root on 8/22/2024.
//

#ifndef INDIRECTBRANCH_H
#define INDIRECTBRANCH_H
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class IndirectBranch : public PassInfoMixin<IndirectBranch> {
  public:
    IndirectBranch()=default;
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  private:
    void process(Function &F);
  public:
    static bool isRequired() { return true; }
};
}
#endif //INDIRECTBRANCH_H
