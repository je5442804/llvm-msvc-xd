#include "BogusControlFlow.h"
#include "CombineFunctions.h"
#include "ConstObfuscation.h"
#include "DataObfuscation.h"
#include "EasyCFG.hpp"
#include "Flattening.h"
#include "FlatteningEnhanced.h"
#include "IndirectCall.h"
#include "IndirectGlobalVars.h"
#include "Linearize.h"
#include "MBAObfuscation.h"
#include "SplitBasicBlock.h"
#include "StringObfuscation.h"
#include "Substitution.h"
#include "VMFlatten.h"
#include "VariableRotation.h"
#include "xVMP.h"
#include "xVMProtect.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#include <AliasAccess.h>
#include <GlobalsEncryption.h>
#include <IndirectBranch.h>

using namespace llvm;

llvm::PassPluginLibraryInfo getObfuscationPluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION, "Obfuscation", LLVM_VERSION_STRING,
      [](PassBuilder &PB) {
        PB.registerPipelineStartEPCallback([](llvm::ModulePassManager &MPM,
                                              OptimizationLevel Level) {

          MPM.addPass(createModuleToFunctionPassAdaptor(SplitBasicBlockPass()));
          MPM.addPass(
              createModuleToFunctionPassAdaptor(BogusControlFlowPass()));
          MPM.addPass(createModuleToFunctionPassAdaptor(SubstitutionPass()));
          MPM.addPass(createModuleToFunctionPassAdaptor(MBAObfuscationPass()));
          MPM.addPass(createModuleToFunctionPassAdaptor(FlatteningPass()));
          MPM.addPass(createModuleToFunctionPassAdaptor(VmProtectPass()));

        });
        PB.registerOptimizerEarlyEPCallback([](llvm::ModulePassManager &MPM,
                                               OptimizationLevel Level) {
          MPM.addPass(StringObfuscationPass());
          MPM.addPass(createModuleToFunctionPassAdaptor(DataObfuscationPass()));
          MPM.addPass(xvmPass());
          MPM.addPass(CombineFunctionsPass());
          MPM.addPass(createModuleToFunctionPassAdaptor(FlatteningEnhanced()));
          MPM.addPass(VariableRotationPass());
          MPM.addPass(AliasAccess());
          MPM.addPass(GlobalsEncryption());
        });

        PB.registerOptimizerLastEPCallback([](llvm::ModulePassManager &MPM,
                                              OptimizationLevel Level) {


          MPM.addPass(createModuleToFunctionPassAdaptor(ConstObfuscationPass()));
          MPM.addPass(createModuleToFunctionPassAdaptor(IndirectCallPass()));
          MPM.addPass(IngvObfuscationPass());
          MPM.addPass(createModuleToFunctionPassAdaptor(VmFlatObfuscationPass()));
          MPM.addPass(Linearize());
          MPM.addPass(EasyCfgPass());
          MPM.addPass(createModuleToFunctionPassAdaptor(IndirectBranch()));


        });
        //PB.registerVectorizerStartEPCallback(
        //    [](FunctionPassManager &FPM, OptimizationLevel Level) {});
      }};
}

#ifndef LLVM_OBFUSCATION_LINK_INTO_TOOLS
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getObfuscationPluginInfo();
}
#endif
