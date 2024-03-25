#include "xVMTranslator.h"
#include "llvm/IR/IRBuilder.h"

using namespace llvm;

void VMTranslator::construct_gv() {
    // construct code global array from vm_code

    // set Initializer for gv_code_seg
    ArrayRef<uint8_t> code_seg_arrayref(vm_code);
    Constant * code_seg_init = ConstantDataArray::get(Mod->getContext(), code_seg_arrayref);

    vm_store->code_seg_type = ArrayType::get(IntegerType::get(Mod->getContext(), 8), vm_code.size());
    // ArrayType * code_seg_type = ArrayType::get(IntegerType::get(Mod->getContext(), 8), VM_CODE_SEG_SIZE);
    // global
    vm_store->gv_code_seg = new GlobalVariable(
                                                    /*Module=*/*Mod,
                                                    /*Type=*/ vm_store->code_seg_type,
                                                    /*isConstant=*/true,
                                                    /*Linkage=*/GlobalValue::InternalLinkage,
                                                    /*Initializer=*/code_seg_init, // has initializer, specified
                                                                                    // below
                                                    /*Name=*/"gv_code_seg_"+F->getName());



    // construct data global array
    std::vector<uint8_t> data_seg_vector(curr_data_offset);
    // std::vector<uint8_t> data_seg_vector(VM_DATA_SEG_SIZE);
    ArrayRef<uint8_t> data_seg_arrayref(data_seg_vector);
    Constant * data_seg_init = ConstantDataArray::get(Mod->getContext(), data_seg_arrayref);
    vm_store->data_seg_type = ArrayType::get(IntegerType::get(Mod->getContext(), 8), curr_data_offset);
    // ArrayType * data_seg_type = ArrayType::get(IntegerType::get(Mod->getContext(), 8), VM_DATA_SEG_SIZE);
    // global
    vm_store->gv_data_seg = new GlobalVariable(
                                                    /*Module=*/*Mod,
                                                    /*Type=*/ vm_store->data_seg_type,
                                                    /*isConstant=*/false,
                                                    /*Linkage=*/GlobalValue::InternalLinkage,
                                                    /*Initializer=*/data_seg_init, // has initializer, specified
                                                                                    // below
                                                    /*Name=*/"gv_data_seg_"+F->getName());


    // ip
    Constant *ip_initGV = ConstantInt::get(Type::getInt32Ty(Mod->getContext()), 0);
     vm_store->ip = new GlobalVariable(*Mod, Type::getInt32Ty(Mod->getContext()),
                false,  GlobalValue::InternalLinkage,
                ip_initGV, "ip_"+F->getName());

    // data_seg_addr
    Constant *data_seg_addr_initGV = ConstantInt::get(Type::getInt64Ty(Mod->getContext()), 0);
  vm_store->data_seg_addr = new GlobalVariable(*Mod, Type::getInt64Ty(Mod->getContext()),
                false,  GlobalValue::InternalLinkage,
                data_seg_addr_initGV, "data_seg_addr_"+F->getName());

    // code_seg_addr
    Constant *code_seg_addr_initGV = ConstantInt::get(Type::getInt64Ty(Mod->getContext()), 0);
     vm_store->code_seg_addr = new GlobalVariable(*Mod, Type::getInt64Ty(Mod->getContext()),
                false,  GlobalValue::InternalLinkage,
                code_seg_addr_initGV, "code_seg_addr_"+F->getName());
}

void VMTranslator::setup_callinst_handler() {
    // collect dispatch function args type
    std::vector<Type*> FuncTy_args;
    // param: targetfunc_id_value
    FuncTy_args.push_back(Type::getInt64Ty(Mod->getContext()));

    // get dispatch function type
    FunctionType* FuncTy = FunctionType::get(
        /*Result=*/Type::getVoidTy(this->Mod->getContext()),  // returning void
        /*Params=*/FuncTy_args,
        /*isVarArg=*/false);
    // Constant *tmp = Mod->getOrInsertFunction("",FuncTy);
    Constant *tmp = Function::Create(FuncTy, llvm::GlobalValue::LinkageTypes::InternalLinkage, "vm_interpreter_callinst_dispatch_"+F->getName(), Mod);
    Function *func =  cast<Function>(tmp);
    // func->setLinkage(llvm::GlobalValue::LinkageTypes::InternalLinkage);

    // create entry BasicBlock
    BasicBlock *entryBB = BasicBlock::Create(func->getContext(), "entryBB", func);
    IRBuilder<> IRBentryBB(entryBB);

    // Store params
    Value *target_id_value;
    for (auto arg = func->arg_begin(); arg != func->arg_end(); arg++) {
        Value *tmparg = &*arg;
        if (arg == func->arg_begin()) {
            // targetfunc_id_value
            Value *paramPtr = IRBentryBB.CreateAlloca(Type::getInt64Ty(Mod->getContext()));
            IRBentryBB.CreateStore(tmparg, paramPtr);
            target_id_value = IRBentryBB.CreateLoad(paramPtr->getType(),paramPtr);
        }
    }

    // create condition basicblock
    BasicBlock *conBBL = BasicBlock::Create(func->getContext(), "conBBL", func);
    IRBentryBB.CreateBr(conBBL);
    // BasicBlock *falseconBBL;

    // traverse Functions and put them in the switch
    this->callinst_handler_curr_idx = 0;

    this->callinst_handler = func;
    this->callinst_handler_conBBL = conBBL;

    this->targetfunc_id = target_id_value;
}

void VMTranslator::finish_callinst_handler() {
    // default branch
    IRBuilder<> IRBcon(callinst_handler_conBBL);

    // Create Return
    IRBcon.CreateRetVoid();
}

void VMTranslator::handle_callinst(CallInst *inst, long long curr_func_id) {

    IRBuilder<> IRBcon(this->callinst_handler_conBBL);

    
    // firstly,  we need to unpack function args
    std::vector<Value *> target_func_args;
    for (unsigned idx = 0; idx < inst->arg_size(); idx++){
        Value * currarg = inst->getArgOperand(idx);

        // if value is a constant, use it directly
        if(ConstantData* CD = dyn_cast<ConstantData>(currarg)){
            target_func_args.push_back(currarg);
            continue;
        }

        unsigned curroffset = value_map[currarg];

        // construct load
        ConstantInt *Zero = ConstantInt::get(Type::getInt64Ty(Mod->getContext()), 0);
        Value * offset_value = ConstantInt::get(Type::getInt64Ty(Mod->getContext()), curroffset);
        Value * gepinst = IRBcon.CreateGEP( vm_store->data_seg_type,vm_store->gv_data_seg, {Zero, offset_value}, "");
         
        // convert gep from i8* to value->getType() *
        PointerType * target_ptr_type = PointerType::get(currarg->getType(), cast<PointerType>(gepinst->getType())->getAddressSpace());
        Value * ptr = IRBcon.CreatePointerCast(gepinst, target_ptr_type);

        // load from gv_data_seg
        Value * arg = IRBcon.CreateLoad(ptr->getType(),ptr);

        target_func_args.push_back(arg);
    }


    // secondly, we create a new basic block to construct callinst
    BasicBlock *callFunction = BasicBlock::Create(Mod->getContext(), "callFunction_" + std::to_string(this->callinst_handler_curr_idx), this->callinst_handler);
    IRBuilder<> IRBcallFunction(callFunction);

    Value *resultValue;

    if (!inst->isIndirectCall()) {
        // direct call
        Function *callee = inst->getCalledFunction();
        // call replace function
        resultValue = IRBcallFunction.CreateCall(callee->getFunctionType(), callee,
                    ArrayRef<Value *>(target_func_args));
    }
    else {
        // indirect call
        // CallInst *inst;
        Value *called_value = inst->getCalledOperand();
        unsigned called_value_offset = value_map[called_value];

        // load value from gv_data_seg
        ConstantInt *Zero = ConstantInt::get(Type::getInt64Ty(Mod->getContext()), 0);
        Value * offset_value = ConstantInt::get(Type::getInt64Ty(Mod->getContext()), called_value_offset);
        Value * gepinst = IRBcallFunction.CreateGEP( vm_store->data_seg_type,vm_store->gv_data_seg, {Zero, offset_value}, "");

        // convert gep from i8* to value->getType() *
        PointerType * target_ptr_type = PointerType::get(called_value->getType(), cast<PointerType>(gepinst->getType())->getAddressSpace());
        Value * ptr = IRBcallFunction.CreatePointerCast(gepinst, target_ptr_type);

        // load from gv_data_seg
        Value * value = IRBcallFunction.CreateLoad(ptr->getType(),ptr);


        // indirect call4re5
        FunctionType *funcType = cast<FunctionType>(value->getType()->getPointerElementType());
        resultValue = IRBcallFunction.CreateCall(funcType, value, ArrayRef<Value *>(target_func_args));
    }

    // if return not void, store it to gv_data_seg
    if (inst->getType() != Type::getVoidTy(this->Mod->getContext())) {
        unsigned result_value_offset = value_map[inst];

        // load value from gv_data_seg
        ConstantInt *Zero = ConstantInt::get(Type::getInt64Ty(Mod->getContext()), 0);
        Value * offset_value = ConstantInt::get(Type::getInt64Ty(Mod->getContext()), result_value_offset);
        Value * gepinst = IRBcallFunction.CreateGEP( vm_store->data_seg_type,vm_store->gv_data_seg, {Zero, offset_value}, "");

        // convert gep from i8* to value->getType() *
        PointerType * target_ptr_type = PointerType::get(resultValue->getType(), cast<PointerType>(gepinst->getType())->getAddressSpace());
        Value * ptr = IRBcallFunction.CreatePointerCast(gepinst, target_ptr_type);

        // store
        IRBcallFunction.CreateStore(resultValue, ptr);
    }


    // Create Return
    IRBcallFunction.CreateRetVoid();


    // compare and jmp
    BasicBlock *falseconBBL = BasicBlock::Create(Mod->getContext(), "falseconBBL", this->callinst_handler);

    Value *currfunc_id = ConstantInt::get(Type::getInt64Ty(Mod->getContext()), curr_func_id);
    Value *condition = IRBcon.CreateICmpEQ(this->targetfunc_id, currfunc_id);
    IRBcon.CreateCondBr(condition, callFunction, falseconBBL);
    this->callinst_handler_conBBL = falseconBBL;

}


void VMTranslator::handle_inst(Instruction *ins) {

    // switch inst type
    if(AllocaInst * inst = dyn_cast<AllocaInst>(ins)){
        // alloca memory for AllocaInst_Res and AllocaInst_alloca_area

        // AllocaInst_Res
        int pointer_offset = curr_data_offset;
        insert_to_value_map(&value_map, inst, curr_data_offset);
        int res_size = modDataLayout->getTypeAllocSize(inst->getType());
        curr_data_offset += res_size;

        std::vector<uint8_t> packed_res = GET_PACK_VALUE(inst);

        // AllocaInst_alloca_area
        int area_offset = curr_data_offset;
        int alloca_size = modDataLayout->getTypeAllocSize(inst->getAllocatedType());
        curr_data_offset += alloca_size;

        std::vector<uint8_t> hex_code;
        ins_to_hex(hex_code, pack_op(ALLOCA_OP), packed_res, pack(area_offset, POINTER_SIZE));
        vm_code.insert(vm_code.end(), hex_code.begin(), hex_code.end());

        #ifdef VMPTRANSLATOR_DEBUG
        errs() << "[*] AllocaInst: " << *inst << "\n";
        errs() << "\t pointer_offset: " << pointer_offset << "\tpointer_size: " << res_size << "\n";
        errs() << "\t area_offset: " << area_offset << "\tarea_size: " << alloca_size << "\n";
        errs() << "\t current code_pos: " << vm_code.size() - hex_code.size() << "\n";
        // errs() << "\t Hex Code: "; dump_vector(hex_code); errs() << "\n";
        #endif
    }

    else if(LoadInst * inst = dyn_cast<LoadInst>(ins)){

        // return
        int res_offset = curr_data_offset;
        insert_to_value_map(&value_map, inst, curr_data_offset);
        int res_size = modDataLayout->getTypeAllocSize(inst->getType());
        curr_data_offset += res_size;

        std::vector<uint8_t> packed_res = GET_PACK_VALUE(inst);

        // PointerOperand
        std::vector<uint8_t> packed_pointer_operand = GET_PACK_VALUE(inst->getPointerOperand());


        std::vector<uint8_t> hex_code;
        ins_to_hex(hex_code, pack_op(LOAD_OP), packed_res, packed_pointer_operand);
        vm_code.insert(vm_code.end(), hex_code.begin(), hex_code.end());

        #ifdef VMPTRANSLATOR_DEBUG
        errs() << "[*] LoadInst: " << *inst << "\n";
        errs() << "\t current code_pos: " << vm_code.size() - hex_code.size() << "\n";
        // errs() << "\t Hex Code: "; dump_vector(hex_code); errs() << "\n";
        #endif
    }

    else if(StoreInst * inst = dyn_cast<StoreInst>(ins)){

        // ValueOperand
        std::vector<uint8_t> packed_value_operand = GET_PACK_VALUE(inst->getValueOperand());

        // PointerOperand
        std::vector<uint8_t> packed_pointer_operand = GET_PACK_VALUE(inst->getPointerOperand());


        std::vector<uint8_t> hex_code;
        ins_to_hex(hex_code, pack_op(STORE_OP), packed_value_operand, packed_pointer_operand);
        vm_code.insert(vm_code.end(), hex_code.begin(), hex_code.end());

        #ifdef VMPTRANSLATOR_DEBUG
        errs() << "[*] StoreInst: " << *inst << "\n";
        errs() << "\t current code_pos: " << vm_code.size() - hex_code.size() << "\n";
        // errs() << "\t Hex Code: "; dump_vector(hex_code); errs() << "\n";
        #endif
    }

    else if(ins->isBinaryOp()){
        BinaryOperator * inst = dyn_cast<BinaryOperator>(ins);

        int res_offset = curr_data_offset;
        insert_to_value_map(&value_map, inst, curr_data_offset);
        int res_size = modDataLayout->getTypeAllocSize(inst->getType());
        curr_data_offset += res_size;

        std::vector<uint8_t> packed_res = GET_PACK_VALUE(inst);

        std::vector<uint8_t> packed_binaryOpcode = {static_cast<uint8_t>(inst->getOpcode())};

        std::vector<uint8_t> packed_op0 = GET_PACK_VALUE(inst->getOperand(0));
        std::vector<uint8_t> packed_op1 = GET_PACK_VALUE(inst->getOperand(1));


        std::vector<uint8_t> hex_code;
        ins_to_hex(hex_code, pack_op(BinaryOperator_OP), packed_binaryOpcode, packed_res, packed_op0, packed_op1);
        vm_code.insert(vm_code.end(), hex_code.begin(), hex_code.end());

        #ifdef VMPTRANSLATOR_DEBUG
        errs() << "[*] BinaryOperator: " << *inst << "\n";
        errs() << "\t Opcode: " << inst->getOpcode() << "\t OpcodeName: " << inst->getOpcodeName() << "\n";
        errs() << "\t res_offset: " << res_offset << "\t res_size: " << res_size << "\n";
        errs() << "\t Operand 0: " << inst->getOperand(0)->getName() << "\t Operand 1: " << inst->getOperand(1)->getName() << "\n";
        errs() << "\t Op 0 Type: " << *inst->getOperand(0)->getType() << "\t Op 0 Size: " << modDataLayout->getTypeAllocSize(inst->getOperand(0)->getType()) << "\n";
        errs() << "\t Op 1 Type: " << *inst->getOperand(1)->getType() << "\t Op 1 Size: " << modDataLayout->getTypeAllocSize(inst->getOperand(1)->getType()) << "\n";
        errs() << "\t current code_pos: " << vm_code.size() - hex_code.size() << "\n";
        // errs() << "\t Hex Code: "; dump_vector(hex_code); errs() << "\n";
        errs() << "\n";
        #endif
    }

    else if(CmpInst * inst = dyn_cast<CmpInst>(ins)){

        int res_offset = curr_data_offset;
        insert_to_value_map(&value_map, inst, curr_data_offset);
        int res_size = modDataLayout->getTypeAllocSize(inst->getType());
        curr_data_offset += res_size;

        std::vector<uint8_t> packed_res = GET_PACK_VALUE(inst);

        std::vector<uint8_t> packed_op0 = GET_PACK_VALUE(inst->getOperand(0));
        std::vector<uint8_t> packed_op1 = GET_PACK_VALUE(inst->getOperand(1));

        std::vector<uint8_t> packed_predicate = {static_cast<uint8_t>(inst->getPredicate())};

        std::vector<uint8_t> hex_code;
        ins_to_hex(hex_code, pack_op(CMP_OP), packed_predicate, packed_res, packed_op0, packed_op1);
        vm_code.insert(vm_code.end(), hex_code.begin(), hex_code.end());

        #ifdef VMPTRANSLATOR_DEBUG
        errs() << "[*] CmpInst: " << *inst << "\n";
        errs() << "\t Predicate: " << inst->getPredicate() << "\n";
        errs() << "\t res_offset: " << res_offset << "\t res_size: " << res_size << "\n";
        errs() << "\t Op 0 Type: " << *inst->getOperand(0)->getType() << "\t Op 0 Size: " << modDataLayout->getTypeAllocSize(inst->getOperand(0)->getType()) << "\n";
        errs() << "\t Op 1 Type: " << *inst->getOperand(1)->getType() << "\t Op 1 Size: " << modDataLayout->getTypeAllocSize(inst->getOperand(1)->getType()) << "\n";
        errs() << "\t current code_pos: " << vm_code.size() - hex_code.size() << "\n";
        // errs() << "\t Hex Code: "; dump_vector(hex_code); errs() << "\n";
        errs() << "\n";
        #endif
    }

    else if(GetElementPtrInst * inst = dyn_cast<GetElementPtrInst>(ins)){
        int res_offset = curr_data_offset;
        insert_to_value_map(&value_map, inst, curr_data_offset);
        int res_size = modDataLayout->getTypeAllocSize(inst->getType());
        curr_data_offset += res_size;

        std::vector<uint8_t> packed_res = GET_PACK_VALUE(inst);

        std::vector<uint8_t> packed_ptr = GET_PACK_VALUE(inst->getPointerOperand());

        // get indices
        // but only consider last indice
        std::vector<Value *> indices;
        for (auto curr_idx=inst->idx_begin(); curr_idx != inst->idx_end(); curr_idx++){
            indices.push_back(*curr_idx);
        }

        // GEP type
        // {0, 0}: structure value is offset
        // {x, x}: array, value is offset
        Type * srcType = inst->getSourceElementType();
        std::vector<uint8_t> gep_type;
        std::vector<uint8_t> packed_value;
        if (dyn_cast<StructType>(srcType)) {
            // is struct type
            StructType * st = dyn_cast<StructType>(srcType);
            gep_type = {0, 0};
            ConstantInt* CI = dyn_cast<ConstantInt>(indices[indices.size()-1]);     // last indice
            int element_idx = CI->getSExtValue();       // const value to int
            int curr_element_offset = 0;
            for (int i = 0; i < element_idx; i++) {             // calc the offset between curr_element and struct_begin
                curr_element_offset += modDataLayout->getTypeAllocSize(st->getElementType(i));
            }

            // Construct const-offset manually
            packed_value = {0, 0};
            std::vector<uint8_t> tmp = pack(curr_element_offset, POINTER_SIZE);
            packed_value.insert(packed_value.end(), tmp.begin(), tmp.end());
        } else {
            // is array type
            gep_type = type_to_hex(inst->getResultElementType());
            packed_value = GET_PACK_VALUE(indices[indices.size()-1]);
        }

        std::vector<uint8_t> hex_code;
        ins_to_hex(hex_code, pack_op(GEP_OP), gep_type, packed_res, packed_ptr, packed_value);
        vm_code.insert(vm_code.end(), hex_code.begin(), hex_code.end());


        #ifdef VMPTRANSLATOR_DEBUG
        errs() << "[*] GetElementPtrInst: " << *inst << "\n";
        errs() << "\t res_offset: " << res_offset << "\t res_size: " << res_size << "\n";
        errs() << "\t is struct gep: " << (dyn_cast<StructType>(srcType) != 0) << "\n";
        errs() << "\t current code_pos: " << vm_code.size() - hex_code.size() << "\n";
        // errs() << "\t Hex Code: "; dump_vector(hex_code); errs() << "\n";
        errs() << "\n";
        #endif
    }

    else if(CastInst * inst = dyn_cast<CastInst>(ins)){
        int res_offset = curr_data_offset;
        insert_to_value_map(&value_map, inst, curr_data_offset);
        int res_size = modDataLayout->getTypeAllocSize(inst->getType());
        curr_data_offset += res_size;

        std::vector<uint8_t> packed_res = GET_PACK_VALUE(inst);
        std::vector<uint8_t> packed_value = GET_PACK_VALUE(inst->getOperand(0));

        std::vector<uint8_t> hex_code;
        ins_to_hex(hex_code, pack_op(CAST_OP), packed_res, packed_value);
        vm_code.insert(vm_code.end(), hex_code.begin(), hex_code.end());

        #ifdef VMPTRANSLATOR_DEBUG
        errs() << "[*] CastInst: " << *inst << "\n";
        errs() << "\t res_offset: " << res_offset << "\t res_size: " << res_size << "\n";
        errs() << "\t Op 0 Type: " << *inst->getOperand(0)->getType() << "\t Op 0 Size: " << modDataLayout->getTypeAllocSize(inst->getOperand(0)->getType()) << "\n";
        errs() << "\t current code_pos: " << vm_code.size() - hex_code.size() << "\n";
        // errs() << "\t Hex Code: "; dump_vector(hex_code); errs() << "\n";
        errs() << "\n";
        #endif
    }

    else if(BranchInst * inst = dyn_cast<BranchInst>(ins)){
        // Construct code_hex in here manually
        std::vector<uint8_t> hex_code;
        hex_code = pack_op(BR_OP);
        std::vector<uint8_t> padding = pack(0, POINTER_SIZE);

        if (inst->isUnconditional()) {
            hex_code.push_back(0);
            // errs() << vm_code.size()+2 << "\n";
            br_map.push_back(std::pair<int, BasicBlock *>(vm_code.size()+hex_code.size(), inst->getSuccessor(0)));          // fill after traverse whole function
            hex_code.insert(hex_code.end(), padding.begin(), padding.end());
        } else {
            hex_code.push_back(1);
            // condition
            std::vector<uint8_t> pack_condition = packValue(inst->getCondition(), &value_map);
            hex_code.insert(hex_code.end(), pack_condition.begin(), pack_condition.end());

            // errs() << vm_code.size()+2 << "\n";
            br_map.push_back(std::pair<int, BasicBlock *>(vm_code.size()+hex_code.size(), inst->getSuccessor(0)));
            hex_code.insert(hex_code.end(), padding.begin(), padding.end());
            // errs() << vm_code.size()+2+POINTER_SIZE << "\n";
            br_map.push_back(std::pair<int, BasicBlock *>(vm_code.size()+hex_code.size(), inst->getSuccessor(1)));
            hex_code.insert(hex_code.end(), padding.begin(), padding.end());
        }


        vm_code.insert(vm_code.end(), hex_code.begin(), hex_code.end());

        #ifdef VMPTRANSLATOR_DEBUG
        errs() << "[*] BranchInst: " << *inst << "\n";
        if (inst->isConditional())
            errs() << "\t Condition: " << *inst->getCondition() << "\n";
        for (unsigned i=0; i<inst->getNumSuccessors(); i++) {
            errs() << "\t Successors: " << inst->getSuccessor(i)->getName().str() << "\n";
        }
        errs() << "\t current code_pos: " << vm_code.size() - hex_code.size() << "\n";
        // errs() << "\t Hex Code: "; dump_vector(hex_code); errs() << "\n";
        errs() << "\n";
        #endif
    }

    else if(ReturnInst * inst = dyn_cast<ReturnInst>(ins)) {
        std::vector<uint8_t> value;

        if (inst->getNumOperands() == 0){           // return void
            value = GET_NULL_VALUE();
        }
        else{                                       // return something
            value = GET_PACK_VALUE(inst->getReturnValue());
        }

        std::vector<uint8_t> hex_code;
        ins_to_hex(hex_code, pack_op(Ret_OP), value);
        vm_code.insert(vm_code.end(), hex_code.begin(), hex_code.end());

        #ifdef VMPTRANSLATOR_DEBUG
        errs() << "[*] ReturnInst: " << *inst << "\n";
        errs() << "\t value_offset: "; dump_vector(value);
        errs() << "\t current code_pos: " << vm_code.size() - hex_code.size() << "\n";
        // errs() << "\t Hex Code: "; dump_vector(hex_code); errs() << "\n";
        #endif

    }

    else if(CallInst * inst = dyn_cast<CallInst>(ins)) {

        // current function id
        long long curr_func_id = this->callinst_handler_curr_idx ++;

        std::vector<uint8_t> packed_funcid = pack(curr_func_id, POINTER_SIZE);

        // check if this callsite return a void
        std::vector<uint8_t> packed_res;
        if (inst->getType() != Type::getVoidTy(this->Mod->getContext())) {
            // return a value
            int res_offset = curr_data_offset;
            insert_to_value_map(&value_map, inst, curr_data_offset);
            int res_size = modDataLayout->getTypeAllocSize(inst->getType());
            curr_data_offset += res_size;

            packed_res = GET_PACK_VALUE(inst);
        }

        // construct hex code
        std::vector<uint8_t> hex_code;
        ins_to_hex(hex_code, pack_op(Call_OP), packed_funcid);
        // ins_to_hex(hex_code, pack_op(Call_OP), packed_funcid, packed_res);


        // pack args into hex_code
        // for (unsigned idx = 0; idx < inst->getNumArgOperands(); idx++){
        //     Value * arg = inst->getArgOperand(idx);
        //     std::vector<uint8_t> packed_value = GET_PACK_VALUE(arg);

        //     ins_to_hex(hex_code, packed_value);
        // }

        vm_code.insert(vm_code.end(), hex_code.begin(), hex_code.end());

        #ifdef VMPTRANSLATOR_DEBUG
        errs() << "[*] CallInst: " << *inst << "\n";
        errs() << "\t curr_func_id: " << curr_func_id << "\n";
        errs() << "\t current code_pos: " << vm_code.size() - hex_code.size() << "\n";
        // errs() << "\t Hex Code: "; dump_vector(hex_code); errs() << "\n";
        #endif

        // handle_callinst(inst, curr_func_id);
        callinst_map.insert(std::pair<CallInst *, long long>(inst, curr_func_id));
    }

    else{
    ERROR_HANDLER:
        errs() << "[ERROR] Unsupport Instruction: \n";
        errs() << *ins << "\n";
    }
}


// Translator
void VMTranslator::run(){
    // errs() << "Calling Reg2Mem\n";
    // FunctionPass *reg2mempass = createDemoteRegisterToMemoryPass();
    // reg2mempass->runOnFunction(*F);

    // F->dump();
    errs() << "[*] to-be-protect function: \n" << *F << "\n";

    // if return not void, alloca a memory
    if (!F->getReturnType()->isVoidTy()) {
        curr_data_offset += modDataLayout->getTypeAllocSize(F->getReturnType());
    }

    // parameter allocation
    if(!F->isVarArg()){
        for(auto arg = F->arg_begin(); arg != F->arg_end(); ++arg) {

            Value * tmparg = &*arg;
            insert_to_value_map(&value_map, tmparg, curr_data_offset);
            curr_data_offset += modDataLayout->getTypeAllocSize(tmparg->getType());

            #ifdef VMPTRANSLATOR_DEBUG
            errs() << "[*] Arg: " << tmparg->getName() << "\n";
            errs() << "\t size: " << modDataLayout->getTypeAllocSize(tmparg->getType()) << "\n\n";
            #endif
        }
    }


    // traverse whole function
    for(auto bbl = F->begin(); bbl != F->end(); bbl++){

        // mark each basicblock address
        BasicBlock * bb = &*bbl;
        basicblock_map.insert(std::pair<BasicBlock *, int>(bb, vm_code.size()));

        // Opcode seed
        uint32_t opcode_seed = opcode_seed_setup();

        // vm_code seed
        uint32_t vm_code_seed = vm_code_seed_setup();
        uint32_t currbb_begin = vm_code.size();

        #ifdef VMPTRANSLATOR_DEBUG
        errs() << "[*] Visiting BasicBlock: " << bb->getName() << "\n";
        errs() << "\t opcode_seed: " << opcode_seed << "\n";
        errs() << "\t vm_code_seed: " << vm_code_seed << "\n";
        errs() << "\t currbb_begin: " << currbb_begin << "\n";
        errs() << "\n";
        #endif

        for(auto ins = bbl->begin(); ins != bbl->end(); ins++){

            Instruction *inst = dyn_cast<Instruction>(ins);

            for (unsigned idx = 0; idx < inst->getNumOperands(); idx++) {
                if (ConstantExpr * Op = dyn_cast<ConstantExpr>(inst->getOperand(idx))) {
                    // we found a ConstantExpr
                    // convert ConstantExpr to a equal instruction
                    Instruction * const_inst = Op->getAsInstruction();
                    const_inst->insertBefore(inst);
                    // There is a problem, PHINode must at first instruction of a basicblock, unpack all constantExpr is a potential problem
                    // God bless there is not a constantExpr in PHINode

                    // replace ConstantExpr to a value in inst
                    inst->setOperand(idx, const_inst);

                    handle_inst(const_inst);
                }
            }
            handle_inst(inst);
        }

        uint32_t currbb_end = vm_code.size();
        vm_code_seed_map.insert(std::pair<uint32_t, std::pair<uint32_t, uint32_t>>(vm_code_seed, std::pair<uint32_t, uint32_t>(currbb_begin, currbb_end)));
    }

    errs() << "[*] gv_value_map:\n";
    errs() << "size: " << gv_value_map.size() << "\n";
    for (auto p: gv_value_map) {
        errs() << *(p.first) << "\t" << p.second << "\n";
    }
    errs() << "\n";


    errs() << "[*] Fill br_map\n";
    // fill br map
    for(auto it=br_map.rbegin(); it!=br_map.rend(); it++) {
        int code_pos = it->first;
        BasicBlock * target_bb = it->second;
        // errs() << "" << code_pos << " " << target_bb->getName() << " "<<  basicblock_map[target_bb] << "\n";
        std::vector<uint8_t> bb_addr = pack(basicblock_map[target_bb], POINTER_SIZE);
        std::copy(bb_addr.begin(), bb_addr.end(), vm_code.begin()+code_pos);
    }
    errs() << "\n";

    /* vm_code finish */

    // encrypt vm_code with basicblock seed
    encrypt_vm_code();

    // display final hex code
    errs() << "[*] Hex code: \n";
    dump_vector(vm_code);
    errs() << "\n";
    errs() << "[*] Hex code size: " << vm_code.size() << "\n";

    construct_gv();

    // handle callinst
    for (const auto p: callinst_map) {
        handle_callinst(p.first, p.second);
    }

    // callinst_handler fini
    finish_callinst_handler();

    //errs() << *LogUtils::log_title("Translate Finish");
}
