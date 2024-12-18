#ifndef LLVM_OBFUSCATION_UTILS_H
#define LLVM_OBFUSCATION_UTILS_H

#include "llvm/IR/Function.h"



#define INIT_CONTEXT(X) CONTEXT = &X.getContext()
#define TYPE_I64 Type::getInt64Ty(*CONTEXT)
#define TYPE_I32 Type::getInt32Ty(*CONTEXT)
#define TYPE_I8 Type::getInt8Ty(*CONTEXT)
#define GET_TYPE(X) TYPE::getInt(X) Ty(*CONTEXT)
#define CONST_I64(V) ConstantInt::get(TYPE_I64, V, false)
#define CONST_I32(V) ConstantInt::get(TYPE_I32, V, false)
#define CONST_I8(V) ConstantInt::get(TYPE_I8, V, false)
#define CONST(T, V) ConstantInt::get(T, V)
#define RANDOM(X) (cryptoutils->get_uint8_t() % 100 < X)


std::string readAnnotate(llvm::Function *f);
bool toObfuscate(bool flag, llvm::Function *f, std::string attribute);
void LowerConstantExpr(llvm::Function &F);
void fixStack(llvm::Function &F,bool use_alloc);
void OutputIR(llvm::Function &Func);
void turnOffOptimization(llvm::Function *f);
bool isMemberFunction(llvm::Function *F);
bool valueEscapes(const llvm::Instruction &Inst);
void getRandomNoRepeat(unsigned upper_bound, unsigned size,
                       std::vector<unsigned> &result);
uint32_t getUniqueNumber(std::vector<uint32_t> &rand_list);
uint64_t getRandomNumber();
#endif
