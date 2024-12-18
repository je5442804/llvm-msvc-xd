#include "xVMProtect.h"
#include "BogusControlFlow.h"
#include "ConstObfuscation.h"
#include "CryptoUtils.h"
#include "Flattening.h"
#include "IndirectGlobalVars.h"
#include "Utils.h"
#include "VMFlatten.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/IR/InlineAsm.h"
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#ifdef _MSC_VER
#include <vcruntime_string.h>
#endif
#include <vector>

using namespace llvm;

#define RAW_INST 0
#define PUSH_ADDR 1
#define STORE_IMM 2

static cl::opt<bool> RunXvm("x-vm", cl::init(false), cl::desc("OLLVM - xvm"));

namespace {
struct xvm_op_info {
  int opcode;
  int builtin_type;
  std::vector<std::pair<int, Value *>> opnds;
  union {
    Instruction *instr = nullptr;
    int store_size;
  };

  explicit xvm_op_info(int raw)
    : opcode(0) {
    builtin_type = raw;
  }
};
struct xvmm {
  //int op_raw_inst;
  //int op_push_addr;
  //int STORE_IMM;
  std::vector<Function *> vm_funcs;
  std::map<Function *, Function *> vm_mapping;
  void demote_registers(Function *f);
  BasicBlock *handleAlloca(Function &f, std::map<Value *, int> &value_map,
                           std::vector<std::pair<int, int>> &remap, int &space);
  Function *virtualization(Function &f);
  xvm_op_info *getOrCreateRawOp(Instruction &instr,
                                std::vector<xvm_op_info *> &ops, int &cur_op);
  void findOperand(Instruction &instr,std::vector<std::pair<int,Value*>> &operand,std::vector<std::pair<int,Value*>> &unsupported);
  static bool check(Function &f);
  void FixCallInst(Function *target, Function *orig);
  void createBuiltinOps(std::vector<xvm_op_info *> &ops, int &cur_op);
  void buildVMFunction(Function &f, Function &vm,
                       std::vector<xvm_op_info *> &ops, int mem_size,
                       GlobalVariable *opcodes, int addr_stack_size,
                       std::vector<std::pair<int, int>> &remap,
                       std::map<Value *, int> &value_map);
  void pushBytes(unsigned char* ptr,int size,std::vector<unsigned char> &buffer);

  int queryAddr(Value *v,std::map<Value*,int> &locals_addr_map,std::map<Instruction*,int> &reg_addr_map);

  unsigned char findStoreImmOp(std::vector<xvm_op_info*> &ops,int store_size);

  unsigned char findPushAddrOp(std::vector<xvm_op_info*> &ops);

  unsigned char findBranchOp(std::vector<xvm_op_info*> &ops);
  int generateOpcodes(std::vector<BasicBlock *> &code,
                      std::map<Value *, int> &locals_addr_map,
                      std::map<Instruction *, xvm_op_info *> &instr_map,
                      std::vector<xvm_op_info *> &ops, int mem_size,
                      std::vector<Constant *> &opcodes);
  int allocaMemory(BasicBlock &bb, std::map<Instruction *, int> &alloca_map,
                   int mem_base);
  //int get_unique_uint8_t(std::vector<uint8_t> & op_u);
  bool run_on_function(Function &f);
};

void xvmm::demote_registers(Function *f) { fixStack(*f, true); }

BasicBlock *xvmm::handleAlloca(Function &f, std::map<Value *, int> &value_map,
                               std::vector<std::pair<int, int>> &remap,
                               int &space) {
  std::vector<AllocaInst *> allocas;
 //printf("    -collect allocainst\n");
  for (BasicBlock &bb : f)
    for (Instruction &i : bb) {
      if (isa<AllocaInst>(i))
        allocas.push_back((AllocaInst *)&i);
    }
  BasicBlock *alloca_block = &f.getEntryBlock();
 //printf("    -move allocainst before\n");
  for (AllocaInst *a : allocas)
    a->moveBefore(&*alloca_block->getFirstInsertionPt());
 //printf("    -split allocainst block\n");
  for (Instruction &i : *alloca_block) {
    if (!isa<AllocaInst>(i)) {
      alloca_block->splitBasicBlock(&i);
      break;
    }
  }
 //printf("    -calculate locals address\n");
  const DataLayout data = f.getParent()->getDataLayout();
  for (AllocaInst *a : allocas) {
    int real_addr = space;
   //printf("   [%4d] alloc size %d\n", real_addr,
    //       static_cast<int>(data.getTypeAllocSize(a->getAllocatedType())));
    space += data.getTypeAllocSize(a->getAllocatedType());
    int ptr_addr = space;
    value_map[a] = space;
   //printf("   [%4d] store ptr size %d\n", ptr_addr,
    //       static_cast<int>(data.getTypeAllocSize(a->getType())));
    space += data.getTypeAllocSize(a->getType());
    remap.emplace_back(ptr_addr, real_addr);
  }

  return alloca_block;
}

Function *xvmm::virtualization(Function &f) {
 //printf("\nFunction Name: %s\n",f.getName().str().c_str());
  if (!check(f))
    return nullptr;
  bool need_obf = false;
 //printf("[1] start demote registers!\n");
  demote_registers(&f);
  std::map<Value *, int> alloca_map;
  std::vector<std::pair<int, int>> remap;
  int mem_size = 0, cur_op = 1;
 //printf("[2] start alloca vm memory for arguments!\n");
  const DataLayout data = f.getParent()->getDataLayout();
  for (Function::arg_iterator iter = f.arg_begin(); iter != f.arg_end();
       iter++) {
    Value *arg = &(*iter);
    alloca_map[arg] = mem_size;
    mem_size += data.getTypeAllocSize(arg->getType());
  }
 //printf("    ----arguments space %d\n", mem_size);
 //printf("[3] start alloca vm memory for locals!\n");
  const BasicBlock *locals_block = handleAlloca(f, alloca_map, remap, mem_size);
 //printf("    ----current allocated space %d\n", mem_size);
  std::vector<BasicBlock *> code_blocks;
 //printf("[4] create mapping from instruction to opcode\n");
  std::map<Instruction *, xvm_op_info *> instr_map;
  std::vector<xvm_op_info *> ops;
  for (BasicBlock &bb : f) {
    if (&bb == &(*locals_block))
      continue;
    code_blocks.push_back(&bb);
    for (Instruction &i : bb) {
      xvm_op_info *op = getOrCreateRawOp(i, ops, cur_op);
      instr_map[&i] = op;
    }
  }
  createBuiltinOps(ops, cur_op);
 //printf("   -current number of raw op handlers: %lld\n", ops.size());
  /*
  for (const xvm_op_info *o : ops) {
    if (!o->builtin_type)
        printf("    -- %s\n", o->instr->getOpcodeName());
  }*/
 //printf("[5] building vistualization function\n");
  std::vector<Constant *> opcodes;
  const int new_mem_size = generateOpcodes(code_blocks, alloca_map, instr_map,
                                           ops, mem_size, opcodes);
  ArrayType *AT =
      ArrayType::get(Type::getInt8Ty(f.getContext()), opcodes.size());
  Constant *opcode_array =
      ConstantArray::get(AT, ArrayRef<Constant *>(opcodes));
  const auto oparr_var = new GlobalVariable(
      *(f.getParent()), AT, false, GlobalValue::LinkageTypes::PrivateLinkage,
      opcode_array, "opcodes");
  Function *vm_func =
      Function::Create(f.getFunctionType(), f.getLinkage(),
                       f.getName() + Twine("_VM"), f.getParent());
    auto ann = readAnnotate(&f);
    ann.erase(ann.find("x-vm"),4);
    if (ann.find("x-vobf") !=  std::string::npos) {
       ann.erase(ann.find("x-vobf"),6);
       need_obf = true;
    }
    if (ann.find("x-full") != std::string::npos) {
        ann+= "vm-fla,x-full,x-fla-enh";
    }
  vm_func->setAnnotationStrings(ann.c_str());
  
  buildVMFunction(f, *vm_func, ops, new_mem_size, oparr_var, 256, remap,
                  alloca_map);
#if 1

  if (need_obf) {
    auto *AsmStr = InlineAsm::get(FunctionType::get(Type::getVoidTy(vm_func->getContext()), false),
                                        "backend-obfu", /* Constraints */ "", /* hasSideEffects */ true);

    // Add a call to the inline assembly at the beginning of the function
    BasicBlock &EntryBB = vm_func->getEntryBlock();
    Instruction *FirstInst = &*EntryBB.getFirstNonPHI();
    IRBuilder<> Builder(FirstInst);
    Builder.CreateCall(AsmStr);
  }
#endif
  turnOffOptimization(vm_func);
  return vm_func;
}

xvm_op_info * xvmm::getOrCreateRawOp(Instruction &instr,
    std::vector<xvm_op_info *> &ops, int &cur_op) {
  if(instr.getOpcode()==Instruction::Br)
    {
        for(xvm_op_info *info:ops) 
        {
            if(info->instr->getOpcode()==Instruction::Br)
                return info;
        }
        const auto news=new xvm_op_info(RAW_INST);
        news->opcode=cur_op++;
        news->instr=&instr;
        ops.push_back(news);
        return news;
    }
    else
    {
        std::vector<std::pair<int,Value*>> operand1,un1;
        findOperand(instr,operand1,un1);
        for(xvm_op_info *info:ops) 
        {
            if(info->instr!=nullptr && info->instr->isSameOperationAs(&instr))
            {
                if(un1.empty())
                    return info;
                else
                {
                    std::vector<std::pair<int,Value*>> operand2,un2;
                    findOperand(*info->instr,operand2,un2);
                    if(un1.size()!=un2.size())
                        continue;
                    bool ok=true;
                    for(int i=0;i<un1.size();i++)
                    {
                        if(un1[i]!=un2[i])
                        {
                            ok=false;
                            break;
                        }
                    }
                    if(ok)
                        return info;
                }
            }
        }
        const auto news=new xvm_op_info(RAW_INST);
        news->opcode=cur_op++;
        news->instr=&instr;
        for (auto i : operand1) {
          news->opnds.push_back(i);
        }
        ops.push_back(news);
        return news;
    }
}

void xvmm::findOperand(Instruction &instr,
    std::vector<std::pair<int, Value *>> &operand,
    std::vector<std::pair<int, Value *>> &unsupported) {
    int i=0;
    for(Value *opnd:instr.operands())
    {
        if(isa<ConstantInt>(*opnd)/* || isa<ConstantFP>(*opnd)*/ || isa<Instruction>(*opnd) || isa<Argument>(*opnd))
            operand.emplace_back(i,(GlobalVariable*)opnd);
        else
            unsupported.emplace_back(i,(GlobalVariable*)opnd);
        i++;
    }
}

bool xvmm::check(Function &f) {
  for (BasicBlock &bb : f)
    for (Instruction &i : bb) {
      if ((i.isTerminator() && !isa<BranchInst>(i) && !isa<ReturnInst>(i)) ||
          i.isFuncletPad()) {
       //printf("[!] Unsupported Instruction Type: %s\n",i.getOpcodeName());
        return false;
      }
    }
  return true;
}

void xvmm::FixCallInst(Function *target, Function *orig) {
    orig->dropAllReferences();
    BasicBlock *dummy=BasicBlock::Create(orig->getContext(),"dummy",orig);
    IRBuilder<> irb(dummy);
    std::vector<Value*> args;
    for(Function::arg_iterator iter=orig->arg_begin();iter!=orig->arg_end();iter++)
        args.push_back(&*iter);
    Value *call=irb.CreateCall(FunctionCallee(target),args);
    if(target->getReturnType()->isVoidTy())
        irb.CreateRetVoid();
    else
        irb.CreateRet(call);
}

void xvmm::createBuiltinOps(std::vector<xvm_op_info *> &ops, int &cur_op) {
    const auto push_addr=new xvm_op_info(RAW_INST);
    push_addr->opcode=cur_op++;
    push_addr->builtin_type=PUSH_ADDR;
    const auto store_1b=new xvm_op_info(RAW_INST);
    store_1b->opcode=cur_op++;
    store_1b->builtin_type=STORE_IMM;
    store_1b->store_size=1;
    const auto store_2b=new xvm_op_info(RAW_INST);
    store_2b->opcode=cur_op++;
    store_2b->builtin_type=STORE_IMM;
    store_2b->store_size=2;
    const auto store_4b=new xvm_op_info(RAW_INST);
    store_4b->opcode=cur_op++;
    store_4b->builtin_type=STORE_IMM;
    store_4b->store_size=4;
    const auto store_8b=new xvm_op_info(RAW_INST);
    store_8b->opcode=cur_op++;
    store_8b->builtin_type=STORE_IMM;
    store_8b->store_size=8;
    ops.push_back(push_addr);
    ops.push_back(store_1b);
    ops.push_back(store_2b);
    ops.push_back(store_4b);
    ops.push_back(store_8b);
}

void xvmm::buildVMFunction(Function &f, Function &vm,
    std::vector<xvm_op_info *> &ops, int mem_size, GlobalVariable *opcodes,
    int addr_stack_size, std::vector<std::pair<int, int>> &remap,
    std::map<Value *, int> &value_map) {
    auto type_int32_ty = Type::getInt32Ty(vm.getContext());
    //auto type_int64_ty = Type::getInt64Ty(vm.getContext());
    auto type_int8_ty = Type::getInt8Ty(vm.getContext());
    assert(ops.size()<=255);
    BasicBlock *entry=BasicBlock::Create(vm.getContext(),"entry",&vm);
    BasicBlock *dispatch=BasicBlock::Create(vm.getContext(),"dispatch",&vm);
    BasicBlock *loop_end=BasicBlock::Create(vm.getContext(),"loopend",&vm);
    IRBuilder<> irb(entry);


    auto pc=irb.CreateAlloca(type_int32_ty);
    auto memory=irb.CreateAlloca(ArrayType::get(type_int8_ty,mem_size));
    auto addr_stack=irb.CreateAlloca(ArrayType::get(type_int32_ty,addr_stack_size));
    auto ptr=irb.CreateAlloca(type_int32_ty);
    
    //initial for locals
    // errs()<<"initial for locals\n";
    for(std::pair<int,int> p:remap)
    {
        int ptr_addr=p.first,real_addr=p.second;
        Value *ptr=irb.CreateGEP(memory->getAllocatedType(),memory,{irb.getInt32(0),irb.getInt32(real_addr)});
        Value *to_store=irb.CreateGEP(memory->getAllocatedType(),memory,{irb.getInt32(0),irb.getInt32(ptr_addr)});
        irb.CreateStore(ptr,irb.CreateBitCast(to_store,ptr->getType()->getPointerTo()));
    }
   // errs()<<"initial for arguments\n";

    
    //initial for arguments
    Function::arg_iterator real_iter=vm.arg_begin();
    for(Function::arg_iterator iter=f.arg_begin();iter!=f.arg_end();iter++)
    {
        Value *arg=&*iter;
        Value *real_arg=&*real_iter;
        assert(value_map.count(arg)!=0);
        int addr=value_map[arg];
        Value *to_store=irb.CreateGEP(memory->getAllocatedType(),memory,{irb.getInt32(0),irb.getInt32(addr)});
        irb.CreateStore(real_arg,irb.CreateBitCast(to_store,real_arg->getType()->getPointerTo()));
        real_iter++;
    }
    // errs()<<"initial for dispatches\n";
    irb.CreateStore(irb.getInt32(0),pc);
    irb.CreateStore(irb.getInt32(0),ptr);
    BranchInst::Create(dispatch,entry);
    irb.SetInsertPoint(dispatch);
    Value *opvalue=irb.CreateLoad(type_int8_ty,irb.CreateGEP(opcodes->getValueType(),opcodes,{irb.getInt32(0),irb.CreateLoad(irb.getInt32Ty(),pc)}));
    irb.CreateStore(irb.CreateAdd(irb.CreateLoad(irb.getInt32Ty(),pc),irb.getInt32(1)),pc);
    SwitchInst *dispatcher=SwitchInst::Create(opvalue,loop_end,0,dispatch);
    bool has_br=false;
  // errs()<<"initial for ops_handle\n";
#if 1
    for(xvm_op_info* op:ops)
    {
        if(op->builtin_type)
        {
            if(op->builtin_type==PUSH_ADDR)
            {
                BasicBlock *handler=BasicBlock::Create(vm.getContext(),"push_addr",&vm);
                dispatcher->addCase(irb.getInt8(op->opcode),handler);
                irb.SetInsertPoint(handler);
                Value *vpc=irb.CreateLoad(irb.getInt32Ty(),pc);
                Value *vptr=irb.CreateLoad(irb.getInt32Ty(),ptr);
                Value *addr=irb.CreateLoad(irb.getInt32Ty(),irb.CreateBitCast(irb.CreateGEP(opcodes->getValueType(),opcodes,{irb.getInt32(0),vpc}),irb.getInt32Ty()->getPointerTo()));
                irb.CreateStore(addr,irb.CreateGEP(addr_stack->getAllocatedType(),addr_stack,{irb.getInt32(0),vptr}));
                irb.CreateStore(irb.CreateAdd(vptr,irb.getInt32(1)),ptr);
                irb.CreateStore(irb.CreateAdd(vpc,irb.getInt32(4)),pc);
                BranchInst::Create(loop_end,handler);
                handler->moveBefore(loop_end);
            }
            else if(op->builtin_type==STORE_IMM)
            {
                
                if(op->store_size==1)
                {
                    BasicBlock *handler=BasicBlock::Create(vm.getContext(),"store_imm1",&vm);
                    irb.SetInsertPoint(handler);
                    dispatcher->addCase(irb.getInt8(op->opcode),handler);
                    Value *vpc=irb.CreateLoad(irb.getInt32Ty(),pc);
                    Value *vptr=irb.CreateLoad(irb.getInt32Ty(),ptr);
                    Value *imm=irb.CreateLoad(irb.getInt8Ty(),irb.CreateGEP(opcodes->getValueType(),opcodes,{irb.getInt32(0),vpc}));
                    Value *addr=irb.CreateLoad(irb.getInt32Ty(),irb.CreateGEP(addr_stack->getAllocatedType(),addr_stack,{irb.getInt32(0),irb.CreateSub(vptr,irb.getInt32(1))}));
                    irb.CreateStore(imm,irb.CreateGEP(memory->getAllocatedType(),memory,{irb.getInt32(0),addr}));
                    
                    irb.CreateStore(irb.CreateSub(vptr,irb.getInt32(1)),ptr);
                    irb.CreateStore(irb.CreateAdd(vpc,irb.getInt32(1)),pc);
                    BranchInst::Create(loop_end,handler);
                    handler->moveBefore(loop_end);
                }
                else if(op->store_size==2)
                {
                    BasicBlock *handler=BasicBlock::Create(vm.getContext(),"store_imm2",&vm);
                    irb.SetInsertPoint(handler);
                    dispatcher->addCase(irb.getInt8(op->opcode),handler);
                    Value *vpc=irb.CreateLoad(irb.getInt32Ty(),pc);
                    Value *vptr=irb.CreateLoad(irb.getInt32Ty(),ptr);
                    Value *imm=irb.CreateLoad(irb.getInt16Ty(),irb.CreateBitCast(irb.CreateGEP(opcodes->getValueType(),opcodes,{irb.getInt32(0),vpc}),irb.getInt16Ty()->getPointerTo()));

                    Value *addr=irb.CreateLoad(irb.getInt32Ty(),irb.CreateGEP(addr_stack->getAllocatedType(),addr_stack,{irb.getInt32(0),irb.CreateSub(vptr,irb.getInt32(1))}));
                    irb.CreateStore(imm,irb.CreateBitCast(irb.CreateGEP(memory->getAllocatedType(),memory,{irb.getInt32(0),addr}),irb.getInt16Ty()->getPointerTo()));

                    irb.CreateStore(irb.CreateSub(vptr,irb.getInt32(1)),ptr);
                    irb.CreateStore(irb.CreateAdd(vpc,irb.getInt32(2)),pc);
                    BranchInst::Create(loop_end,handler);
                    handler->moveBefore(loop_end);
                }
                else if(op->store_size==4)
                {
                    BasicBlock *handler=BasicBlock::Create(vm.getContext(),"store_imm4",&vm);
                    irb.SetInsertPoint(handler);
                    dispatcher->addCase(irb.getInt8(op->opcode),handler);
                    Value *vpc=irb.CreateLoad(irb.getInt32Ty(),pc);
                    Value *vptr=irb.CreateLoad(irb.getInt32Ty(),ptr);
                    Value *imm=irb.CreateLoad(irb.getInt32Ty(),irb.CreateBitCast(irb.CreateGEP(opcodes->getValueType(),opcodes,{irb.getInt32(0),vpc}),irb.getInt32Ty()->getPointerTo()));

                    Value *addr=irb.CreateLoad(irb.getInt32Ty(),irb.CreateGEP(addr_stack->getAllocatedType(),addr_stack,{irb.getInt32(0),irb.CreateSub(vptr,irb.getInt32(1))}));
                    irb.CreateStore(imm,irb.CreateBitCast(irb.CreateGEP(memory->getAllocatedType(),memory,{irb.getInt32(0),addr}),irb.getInt32Ty()->getPointerTo()));

                    irb.CreateStore(irb.CreateSub(vptr,irb.getInt32(1)),ptr);
                    irb.CreateStore(irb.CreateAdd(vpc,irb.getInt32(4)),pc);
                    BranchInst::Create(loop_end,handler);
                    handler->moveBefore(loop_end);
                }
                else
                {
#if 1
                    BasicBlock *handler=BasicBlock::Create(vm.getContext(),"store_imm8",&vm);
                    irb.SetInsertPoint(handler);
                    dispatcher->addCase(irb.getInt8(op->opcode),handler);
                    Value *vpc=irb.CreateLoad(irb.getInt32Ty(),pc);
                    Value *vptr=irb.CreateLoad(irb.getInt32Ty(),ptr);
                    Value *imm=irb.CreateLoad(irb.getInt64Ty(),irb.CreateBitCast(irb.CreateGEP(opcodes->getValueType(),opcodes,{irb.getInt32(0),vpc}),irb.getInt64Ty()->getPointerTo()));

                    Value *addr=irb.CreateLoad(irb.getInt32Ty(),irb.CreateGEP(addr_stack->getAllocatedType(),addr_stack,{irb.getInt32(0),irb.CreateSub(vptr,irb.getInt32(1))}));
                    irb.CreateStore(imm,irb.CreateBitCast(irb.CreateGEP(memory->getAllocatedType(),memory,{irb.getInt32(0),addr}),irb.getInt64Ty()->getPointerTo()));

                    irb.CreateStore(irb.CreateSub(vptr,irb.getInt32(1)),ptr);
                    irb.CreateStore(irb.CreateAdd(vpc,irb.getInt32(8)),pc);
                    BranchInst::Create(loop_end,handler);
                    handler->moveBefore(loop_end);
#endif
                }
            }
            else
                assert(false && "Unknow builtin op type!");
        }
        else
        {
            if(op->instr->getOpcode()==Instruction::Br)
            {
                if(!has_br)
                {
                    BasicBlock *handler=BasicBlock::Create(vm.getContext(),Twine("branch"),&vm);
                    irb.SetInsertPoint(handler);
                    dispatcher->addCase(irb.getInt8(op->opcode),handler);
                    Value *vpc=irb.CreateLoad(irb.getInt32Ty(),pc);
                    Value *vptr=irb.CreateLoad(irb.getInt32Ty(),ptr);
                    Value *cond_addr=irb.CreateLoad(irb.getInt32Ty(),irb.CreateGEP(addr_stack->getAllocatedType(),addr_stack,{irb.getInt32(0),irb.CreateSub(vptr,irb.getInt32(1))}));
                    Value *cond=irb.CreateLoad(irb.getInt1Ty(),irb.CreateBitCast(irb.CreateGEP(memory->getAllocatedType(),memory,{irb.getInt32(0),cond_addr}),irb.getInt1Ty()->getPointerTo()));
                    Value *jmp_offset=irb.CreateLoad(irb.getInt32Ty(),irb.CreateBitCast(irb.CreateGEP(opcodes->getValueType(),opcodes,{irb.getInt32(0),vpc}),irb.getInt32Ty()->getPointerTo()));
                    Value *final_offset=irb.CreateSelect(cond,jmp_offset,irb.getInt32(4));
                    irb.CreateStore(irb.CreateAdd(vpc,final_offset),pc);
                    irb.CreateStore(irb.CreateSub(vptr,irb.getInt32(1)),ptr);
                    BranchInst::Create(loop_end,handler);
                    handler->moveBefore(loop_end);
                    has_br=true;
                }
                
            }
            else
            {
                BasicBlock *handler=BasicBlock::Create(vm.getContext(),Twine("handler_")+op->instr->getOpcodeName(),&vm);
                irb.SetInsertPoint(handler);
                dispatcher->addCase(irb.getInt8(op->opcode),handler);
                Value *vpc=irb.CreateLoad(irb.getInt32Ty(),pc);
                Value *vptr=irb.CreateLoad(irb.getInt32Ty(),ptr);
                Instruction *target=op->instr->clone();
                Value *pos=vptr;
                int arg_num=op->opnds.size();
                for(int i=0;i<arg_num;i++)
                {
                    std::pair<int,Value*> p=op->opnds[i];
                    Value *arg_addr=irb.CreateLoad(irb.getInt32Ty(),irb.CreateGEP(addr_stack->getAllocatedType(),addr_stack,{irb.getInt32(0),irb.CreateSub(vptr,irb.getInt32(arg_num-i))}));
                    Value *arg=irb.CreateLoad(p.second->getType(),irb.CreateBitCast(irb.CreateGEP(memory->getAllocatedType(),memory,{irb.getInt32(0),arg_addr}),p.second->getType()->getPointerTo()));
                    target->setOperand(p.first,arg);
                    pos=arg;
                }
                target->insertAfter((Instruction*)pos);
                if(target->getOpcode()==Instruction::Ret)
                {
                    handler->moveBefore(loop_end);
                    continue;
                }
                
                if(!target->getType()->isVoidTy())
                {
                    Value *to_store=irb.CreateLoad(irb.getInt32Ty(),irb.CreateBitCast(irb.CreateGEP(opcodes->getValueType(),opcodes,{irb.getInt32(0),vpc}),irb.getInt32Ty()->getPointerTo()));
                    irb.CreateStore(target,irb.CreateBitCast(irb.CreateGEP(memory->getAllocatedType(),memory,{irb.getInt32(0),to_store}),target->getType()->getPointerTo()));
                    irb.CreateStore(irb.CreateAdd(vpc,irb.getInt32(4)),pc);
                }
                irb.CreateStore(irb.CreateSub(vptr,irb.getInt32(arg_num)),ptr);
                BranchInst::Create(loop_end,handler);
                handler->moveBefore(loop_end);
            }
        }
    }
#endif
    BranchInst::Create(dispatch,loop_end);
}

void xvmm::pushBytes(unsigned char *ptr, int size,
                     std::vector<unsigned char> &buffer) {
  for(int i=0;i<size;i++)
    buffer.push_back(ptr[i]);
}

int xvmm::queryAddr(Value *v, std::map<Value *, int> &locals_addr_map,
    std::map<Instruction *, int> &reg_addr_map) {
  //int addr;
  if(locals_addr_map.count(v)!=0)
    return locals_addr_map[v];
  else if(reg_addr_map.count((Instruction*)v)!=0)
    return reg_addr_map[(Instruction*)v];
  else
    assert(false && "Unknown value!");
}

unsigned char xvmm::findStoreImmOp(std::vector<xvm_op_info *> &ops,
    int store_size) {
  for(xvm_op_info *op:ops)
  {
    if(op->builtin_type==STORE_IMM && op->store_size==store_size)
      return op->opcode;
  }
  assert(false);
}

unsigned char xvmm::findPushAddrOp(std::vector<xvm_op_info *> &ops) {
  for(xvm_op_info *op:ops)
  {
    if(op->builtin_type==PUSH_ADDR)
      return op->opcode;
  }
  assert(false);
}

unsigned char xvmm::findBranchOp(std::vector<xvm_op_info *> &ops) {
  for(xvm_op_info *op:ops)
  {
    if(op->instr->getOpcode()==Instruction::Br)
      return op->opcode;
  }
  assert(false);
}

int xvmm::generateOpcodes(std::vector<BasicBlock *> &code,
                          std::map<Value *, int> &locals_addr_map,
                          std::map<Instruction *, xvm_op_info *> &instr_map,
                          std::vector<xvm_op_info *> &ops, int mem_size,
                          std::vector<Constant *> &opcodes) {
  if(code.empty())
        return mem_size;
    std::vector<unsigned char> opcodes_raw;
    int new_mem_size=mem_size;
    LLVMContext *context=&code[0]->getContext();
    std::vector<std::pair<int,BasicBlock*>> br_to_fix;
    std::map<BasicBlock*,int> block_addr;
  const unsigned char store1_op=findStoreImmOp(ops,1);
  const unsigned char store2_op=findStoreImmOp(ops,2);
  const unsigned char store4_op=findStoreImmOp(ops,4);
  const unsigned char store8_op=findStoreImmOp(ops,8);
  const unsigned char push_addr_op=findPushAddrOp(ops);
    for(int i=0;i<code.size();i++)
    {
        BasicBlock *bb=code[i];
        DataLayout data=bb->getParent()->getParent()->getDataLayout();
        const int cur_addr=opcodes_raw.size();
        block_addr[bb]=cur_addr;
        std::map<Instruction*,int> reg_addr_map;
        const int allocated_space=allocaMemory(*bb,reg_addr_map,mem_size);
        int max_used_space=allocated_space;
        for(Instruction &instr:*bb)
        {
            assert(instr_map.count(&instr)!=0);
            [[maybe_unused]] xvm_op_info *op=instr_map[&instr];
            int used_space=allocated_space;
            if(isa<BranchInst>(instr))
            {

                const auto br=(BranchInst*)&instr;
                unsigned char br_op=findBranchOp(ops);
                if(br->isConditional())
                {
                    Value *cond=br->getCondition();
                    assert(cond->getType()==Type::getInt1Ty(*context));
                    int addr=queryAddr(cond,locals_addr_map,reg_addr_map);
                    int empty=0xdeadbeef;
                    // push addr
                   //printf("%lld",opcodes_raw.size());
                    opcodes_raw.push_back(push_addr_op);
                    pushBytes((unsigned char*)&addr,4,opcodes_raw);
                   //printf("push addr=[%d]\n",addr);
                    // br offset
                   //printf("%lld",opcodes_raw.size());
                    br_to_fix.emplace_back(opcodes_raw.size(),br->getSuccessor(0));     
                    opcodes_raw.push_back(br_op);
                    pushBytes((unsigned char*)&empty,4,opcodes_raw);
                   //printf("branch offset=%d\n",empty);
                    
                    
                    if(i==code.size()-1 || code[i+1]!=br->getSuccessor(1))              
                    {
                        int true_value=1;
                        //push addr
                       //printf("%lld",opcodes_raw.size());
                        opcodes_raw.push_back(push_addr_op);
                        pushBytes((unsigned char*)&used_space,4,opcodes_raw);
                       //printf("push addr=[%d]\n",used_space);
                        //store 1
                       //printf("%lld",opcodes_raw.size());
                        opcodes_raw.push_back(store1_op);
                        pushBytes((unsigned char*)&true_value,1,opcodes_raw);
                       //printf("store imm1=%d\n",1);
                        //push addr
                       //printf("%lld",opcodes_raw.size());
                        opcodes_raw.push_back(push_addr_op);
                        pushBytes((unsigned char*)&used_space,4,opcodes_raw);
                       //printf("push addr=[%d]\n",used_space);
                        //br offset
                        br_to_fix.emplace_back(opcodes_raw.size(),br->getSuccessor(1));
                       //printf("%lld",opcodes_raw.size());
                        opcodes_raw.push_back(br_op);
                        pushBytes((unsigned char*)&empty,4,opcodes_raw);
                       //printf("branch offset=%d\n",empty);
                        used_space+=1;                          
                    }
                    
                }
                else                                                                        
                {
                    if(i==code.size()-1 || code[i+1]!=br->getSuccessor(0))
                    {
                        int true_value=1;
                        int empty=0xdeadbeef;
                        //push addr
                       //printf("%lld",opcodes_raw.size());
                        opcodes_raw.push_back(push_addr_op);
                        pushBytes((unsigned char*)&used_space,4,opcodes_raw);
                       //printf("push addr=[%d]\n",used_space);
                        //store 1
                       //printf("%lld",opcodes_raw.size());
                        opcodes_raw.push_back(store1_op);
                        pushBytes((unsigned char*)&true_value,1,opcodes_raw);
                       //printf("store imm1=%d\n",1);
                        //push addr
                       //printf("%lld",opcodes_raw.size());
                        opcodes_raw.push_back(push_addr_op);
                        pushBytes((unsigned char*)&used_space,4,opcodes_raw);
                       //printf("push addr=[%d]\n",used_space);
                        //br offset
                       //printf("%lld",opcodes_raw.size());
                        br_to_fix.emplace_back(opcodes_raw.size(),br->getSuccessor(0));
                        opcodes_raw.push_back(br_op);
                        pushBytes((unsigned char*)&empty,4,opcodes_raw);
                       //printf("branch offset=%d\n",empty);
                        used_space+=1;
                    }   
                    
                }
            }
            else
            {
                xvm_op_info *vmop=instr_map[&instr];
                for(std::pair<int,Value*> p:vmop->opnds)
                {
                    Value *op=instr.getOperand(p.first);
                    if(isa<ConstantInt>(*op)/* || isa<ConstantFP>(op)*/)
                    {
                        const auto val=(ConstantInt*)op;
                        const int store_size=data.getTypeAllocSize(val->getType());
                        if(store_size==1)
                        {
                            //push addr
                           //printf("%lld",opcodes_raw.size());
                            opcodes_raw.push_back(push_addr_op);
                            pushBytes((unsigned char*)&used_space,4,opcodes_raw);
                           //printf("push addr=[%d]\n",used_space);
                            
                            //store value
                           //printf("%lld",opcodes_raw.size());
                            opcodes_raw.push_back(store1_op);
                            unsigned char r=val->getZExtValue()&0xff;
                            pushBytes((unsigned char*)&r,1,opcodes_raw);
                           //printf("store imm1=%d\n",r);
                            
                            //push addr
                           //printf("%lld",opcodes_raw.size());
                            opcodes_raw.push_back(push_addr_op);
                            pushBytes((unsigned char*)&used_space,4,opcodes_raw);
                           //printf("push addr=[%d]\n",used_space);
                            used_space+=1;
                            
                        }
                        else if(store_size==2)
                        {
                            //push addr
                           //printf("%lld",opcodes_raw.size());
                            opcodes_raw.push_back(push_addr_op);
                            pushBytes((unsigned char*)&used_space,4,opcodes_raw);
                           //printf("push addr=[%d]\n",used_space);
                            
                            //store value
                           //printf("%lld",opcodes_raw.size());
                            opcodes_raw.push_back(store2_op);
                            unsigned short r=val->getZExtValue()&0xff;
                            pushBytes((unsigned char*)&r,2,opcodes_raw);
                           //printf("store imm2=%d\n",r);
                            
                            //push addr
                           //printf("%lld",opcodes_raw.size());
                            opcodes_raw.push_back(push_addr_op);
                            pushBytes((unsigned char*)&used_space,4,opcodes_raw);
                           //printf("push addr=[%d]\n",used_space);
                            used_space+=2;
                        }
                        else if(store_size==4)
                        {
                            //push addr
                           //printf("%lld",opcodes_raw.size());
                            opcodes_raw.push_back(push_addr_op);
                            pushBytes((unsigned char*)&used_space,4,opcodes_raw);
                           //printf("push addr=[%d]\n",used_space);
                            
                            //store value
                           //printf("%lld",opcodes_raw.size());
                            opcodes_raw.push_back(store4_op);
                            unsigned int r=val->getZExtValue();
                            pushBytes((unsigned char*)&r,4,opcodes_raw);
                           //printf("store imm4=%d\n",r);
                            
                            //push addr
                           //printf("%lld",opcodes_raw.size());
                            opcodes_raw.push_back(push_addr_op);
                            pushBytes((unsigned char*)&used_space,4,opcodes_raw);
                           //printf("push addr=[%d]\n",used_space);
                            used_space+=4;
                        }
                        else if(store_size==8)
                        {
                            //push addr
                           //printf("%lld",opcodes_raw.size());
                            opcodes_raw.push_back(push_addr_op);
                            pushBytes((unsigned char*)&used_space,4,opcodes_raw);
                           //printf("push addr=[%d]\n",used_space);
                            
                            //store value
                           //printf("%lld",opcodes_raw.size());
                            opcodes_raw.push_back(store8_op);
                            unsigned long long r=val->getZExtValue();
                            pushBytes((unsigned char*)&r,8,opcodes_raw);
                           //printf("store imm8=%lld\n",r);
                            
                            //push addr
                           //printf("%lld",opcodes_raw.size());
                            opcodes_raw.push_back(push_addr_op);
                            pushBytes((unsigned char*)&used_space,4,opcodes_raw);
                           //printf("push addr=[%d]\n",used_space);
                            used_space+=8;
                        }
                        else
                            assert(false);
                    }
                    else
                    {
                        int addr=queryAddr(op,locals_addr_map,reg_addr_map);
                        //push addr
                       //printf("%lld",opcodes_raw.size());
                        opcodes_raw.push_back(push_addr_op);
                        pushBytes((unsigned char*)&addr,4,opcodes_raw);
                       //printf("push addr=[%d]\n",addr);
                    }
                    
                }
                
                if(!instr.getType()->isVoidTy())
                {
                    //handler addr
                    
                    int addr=queryAddr(&instr,locals_addr_map,reg_addr_map);
                   //printf("%lld",opcodes_raw.size());
                    opcodes_raw.push_back(vmop->opcode);
                    pushBytes((unsigned char*)&addr,4,opcodes_raw);
                   //printf("handler_%d st=[%d] name=%s\n",vmop->opcode,addr,vmop->instr->getOpcodeName());
                }
                else
                {
                    //handler
                   //printf("%lld",opcodes_raw.size());
                    opcodes_raw.push_back(vmop->opcode);
                   //printf("handler_%d name=%s\n",vmop->opcode,vmop->instr->getOpcodeName());
                }
            }
            max_used_space=max_used_space>used_space?max_used_space:used_space;
           //printf("\n");
        }
       //printf("   block max used space: %d\n\n",max_used_space);
        new_mem_size=new_mem_size>max_used_space?new_mem_size:max_used_space;
    }
    for(std::pair<int,BasicBlock*> p:br_to_fix)
    {
        const int pos=p.first+1;
        assert(opcodes_raw[pos]==0xEF && opcodes_raw[pos+1]==0xBE && opcodes_raw[pos+2]==0xAD && opcodes_raw[pos+3]==0xDE);
        BasicBlock *target=p.second;
        assert(block_addr.count(target)!=0);
        int delta=block_addr[target]-pos;
        const auto ptr=(unsigned char*)&delta;
        for(int i=0;i<4;i++)
            opcodes_raw[pos+i]=ptr[i];
    }
    for(const unsigned char op:opcodes_raw)
        opcodes.push_back(ConstantInt::get(Type::getInt8Ty(*context),op));
    return new_mem_size;
}
bool cmp(std::pair<Instruction*,int> &a, std::pair<Instruction*,int> &b)
{
    return a.second<b.second;
}
int xvmm::allocaMemory(BasicBlock &bb, std::map<Instruction *, int> &alloca_map,
    int mem_base) {

      int max_space=0;
    DataLayout data=bb.getParent()->getParent()->getDataLayout();
    std::map<Instruction*,std::set<Instruction*>*> alive;
    for(Instruction &i:bb)
    {
        if(alive.count(&i)==0)
        {
            std::set<Instruction*> *instr_set=new std::set<Instruction*>;
            alive[&i]=instr_set;
        }
    }
    for(Instruction &i:bb)
    {
        if(i.isUsedOutsideOfBlock(&bb))
            assert(false && "Impossible: value escaped");
        for(Value *opnd:i.operands())
        {
            if(isa<Instruction>(*opnd) && !isa<AllocaInst>(*opnd))
            {
                Instruction* instr=(Instruction*)opnd;
                if(instr->getParent()!=&bb)
                    assert(false && "Impossible: value escaped");
                BasicBlock::iterator start=instr->getIterator(),end=i.getIterator();
                ++end;
                for(BasicBlock::iterator iter=++start;iter!=end;iter++)
                {
                    Instruction *ii=&*iter;
                    alive[ii]->insert(instr);
                }
            }
            
        }
    }
    /*for(Instruction &i:bb)
    {
       //printf("current opname: %s\n\t",i.getOpcodeName());
        if(alive[&i]->size()==0)
           //printf("null");
        for(std::set<Instruction*>::iterator iter=alive[&i]->begin();iter!=alive[&i]->end();iter++)
           //printf("%s ",(*iter)->getOpcodeName());
       //printf("\n");
    }*/
    //printf("value remain!\n");
    std::vector<std::pair<Instruction*,int>> current_alloc;
    for(Instruction &i:bb)
    {
        std::vector<std::pair<Instruction*,int>> freed;
        for(std::vector<std::pair<Instruction*,int>>::iterator iter=current_alloc.begin();iter!=current_alloc.end();iter++)
        {
            std::pair<Instruction*,int> p=*iter;
            bool find=false;
            for(std::set<Instruction*>::iterator iter=alive[&i]->begin();iter!=alive[&i]->end();iter++)
            {
                if(p.first==*iter)
                {
                    find=true;
                    break;
                }
            }
            if(!find)
            {
                //printf("    free value opcode: %s\n",p.first->getOpcodeName());
                freed.push_back(p);
            }
                
        }
        for(std::pair<Instruction*,int> pp:freed)
        {
            for(std::vector<std::pair<Instruction*,int>>::iterator iter=current_alloc.begin();iter!=current_alloc.end();iter++)
            {
                std::pair<Instruction*,int> p=*iter;
                if(p==pp)
                {
                    current_alloc.erase(iter);
                    break;
                }
            }
            
        }
            
        //printf("2 --------- %s\n",i.getOpcodeName());
        //printf("    allocated value num: %d\n",current_alloc.size());
        sort(current_alloc,cmp);
        for(std::vector<std::pair<Instruction*,int>>::iterator iter=current_alloc.begin();iter!=current_alloc.end();iter++)
        {
            std::pair<Instruction*,int> p=*iter;
            //printf("    > value %s at addr %d\n",p.first->getOpcodeName(),p.second+mem_base);
        }
        //printf("free ok %d!\n",freed.size());
        if(!i.getType()->isVoidTy())
        {
            //printf("    this instruction need space %d\n",data.getTypeAllocSize(i.getType()));
            std::vector<std::pair<Instruction*,int>>::iterator ptr=current_alloc.begin(),prev;
            while(ptr!=current_alloc.end())
            {
                int space,addr;
                std::pair<Instruction*,int> cur=*ptr;
                if(ptr!=current_alloc.begin())
                {
                    addr=prev->second+data.getTypeAllocSize(prev->first->getType());
                    space=cur.second-addr;
                }
                else
                {
                    addr=0;
                    space=cur.second;
                }
                if(space>=data.getTypeAllocSize(i.getType()))
                {
                    //printf("    find free space\n");
                    current_alloc.insert(ptr,std::make_pair(&i,addr));
                    alloca_map[&i]=addr+mem_base;
                    break;
                }
                prev=ptr;
                ptr++;
            }
            if(ptr==current_alloc.end())
            {
                //printf("    no free space,alloca new space\n");
                int addr;
                if(current_alloc.size()!=0)
                    addr=prev->second+data.getTypeAllocSize(prev->first->getType());
                else
                    addr=0;
                int bound=addr+data.getTypeAllocSize(i.getType());
                max_space=max_space>bound?max_space:bound;
                current_alloc.push_back(std::make_pair(&i,addr));
                alloca_map[&i]=addr+mem_base;
            }
        }
        //if(alloca_map.count(&i)>0)
            //printf("    [~] value addr: %d\n",alloca_map[&i]);
            
    }
    //printf("-----after alloca max_space %d\n",max_space+mem_base);
    for(Instruction &i:bb)
        delete alive[&i];
    return max_space+mem_base;
}

//int xvmm::get_unique_uint8_t(std::vector<uint8_t> &op_u) {
//  while(true) {
//    const auto op = cryptoutils->get_uint8_t();
//    if(std::find(std::begin(op_u),std::end(op_u),op)==std::end(op_u)) {
//      op_u.push_back(op);
//      return op;
//    }
//  }
//  return cryptoutils->get_uint8_t();
//}

bool xvmm::run_on_function(Function &f) {
  bool is_vm = false;
  //std::vector<uint8_t> op_uid;
  //
  //op_raw_inst = get_unique_uint8_t(op_uid);
  //op_push_addr = get_unique_uint8_t(op_uid);
  //STORE_IMM = get_unique_uint8_t(op_uid);
  for (const Function *ff : vm_funcs) {
    if (&f == ff) {
      is_vm = true;
      break;
    }
  }
  if (!is_vm && f.hasExactDefinition()) {
    if (Function *vm_func = virtualization(f); vm_func != nullptr) {
      vm_funcs.push_back(vm_func);
      FixCallInst(vm_func, &f);
      turnOffOptimization(vm_func);
      return true;
    }
  }
  return false;
}

}; // namespace

PreservedAnalyses xvmPass::run(Module &M, ModuleAnalysisManager &AM) {

  if constexpr (true) {
    xvmm xvm;
    bool vm = false;
    for (Function &fn : M) {
      if (toObfuscate(false, &fn, "x-vm")){
        const auto vm_ret = xvm.run_on_function(fn);
        vm|=vm_ret;
      }
    }
    if (vm) {

       return PreservedAnalyses::none();
    }
     
  }
  return PreservedAnalyses::all();
}
