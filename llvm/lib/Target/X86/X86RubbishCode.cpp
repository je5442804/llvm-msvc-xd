//
// Ref to https://github.com/za233/Polaris-Obfuscator/
// Copyed by root on 10/16/2024.
//
#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/IndirectThunks.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <cstdio>
#include <ctime>
#include <random>
#include <vector>
using namespace llvm;

#define DEBUG_TYPE "x86-obfuscation"

namespace {

struct ObfuscateInstrInfo {
  MachineInstr *RawInst;
  std::vector<MCPhysReg> AvailableRegs;
  bool StackAvailable;
  bool EFlagsAvailable;
};

struct ObfuscateOption {
  bool InsertRubbishCode = false;
  bool SplitBasicBlock = false;
};
enum OperandsType {
  NoOpreand,
  OnlyReg,
  OnlyImm,
  RegReg,
  RegImm,
  MemReg,
  RegMem,

};

class X86RubbishCodePass : public MachineFunctionPass {
public:
  static char ID;
  std::vector<std::string> Asm;
  std::map<MCSymbol *, MachineBasicBlock *> Syms;

  X86RubbishCodePass() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "X86 Obfuscation"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void splitBasicBlocks(MachineFunction &MF, unsigned SplitAlign,
                        std::vector<MachineInstr *> &NoSplitPoint);

  MachineInstr *insertInlineBytes(MachineInstr *Before,
                                  std::vector<unsigned char> &Data);

  void generateRubbishCode(ObfuscateInstrInfo &OI, unsigned Depth,
                           std::vector<MachineInstr *> &NoSplitPoint);

  MCPhysReg getRegisterBySize(unsigned Size, std::vector<MCPhysReg> &Regs);

  unsigned getRegisterSize(MCPhysReg Reg);

  bool queryStackSpace(int StackDelta, bool WrMem, unsigned Bits,
                       int64_t *Result);

  bool buildMIWrapper(unsigned Opcode, ObfuscateInstrInfo &OII, OperandsType Ty,
                      std::vector<MCPhysReg> &Regs, unsigned Bits,
                      int StackDelta, bool DefFlag, bool WrMem);

  bool checkShouldProcess(MachineFunction &MF, ObfuscateOption &OO);

  void process(MachineFunction &MF, ObfuscateOption &OO);

private:
  MachineRegisterInfo *MRI = nullptr;
  const X86InstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  MachineFrameInfo *MFI = nullptr;

  std::vector<MCPhysReg> RegList = {
      X86::AL,   X86::AX,   X86::EAX,  X86::RAX,  X86::BL,   X86::BX,
      X86::EBX,  X86::RBX,  X86::CL,   X86::CX,   X86::ECX,  X86::RCX,
      X86::DL,   X86::DX,   X86::EDX,  X86::RDX,  X86::SIL,  X86::SI,
      X86::ESI,  X86::RSI,  X86::DIL,  X86::DI,   X86::EDI,  X86::RDI,
      X86::R8B,  X86::R8W,  X86::R8D,  X86::R8,   X86::R9B,  X86::R9W,
      X86::R9D,  X86::R9,   X86::R10B, X86::R10W, X86::R10D, X86::R10,
      X86::R11B, X86::R11W, X86::R11D, X86::R11,  X86::R12B, X86::R12W,
      X86::R12D, X86::R12,  X86::R13B, X86::R13W, X86::R13D, X86::R13,
      X86::R14B, X86::R14W, X86::R14D, X86::R14,  X86::R15B, X86::R15W,
      X86::R15D, X86::R15};
};
} // end anonymous namespace

char X86RubbishCodePass ::ID = 0;

INITIALIZE_PASS(X86RubbishCodePass, DEBUG_TYPE, DEBUG_TYPE, false, false)

FunctionPass *llvm::createX86RubbishCodePassPass() {
  return new X86RubbishCodePass();
}
MachineInstr *
X86RubbishCodePass::insertInlineBytes(MachineInstr *Before,
                                      std::vector<unsigned char> &Data) {
  MachineBasicBlock *MBB = Before->getParent();
  std::string AsmStr = ".byte ";
  char Hex[10] = {0};
  bool Tail = false;
  for (unsigned char d : Data) {
    if (Tail)
      AsmStr += ", ";
    sprintf(Hex, "0x%02x", d);
    AsmStr += std::string(Hex);

    Tail = true;
  }
  Asm.push_back(AsmStr);
  std::string &T = Asm.back();
  return BuildMI(*MBB, *Before, Before->getDebugLoc(), TII->get(X86::INLINEASM))
      .addExternalSymbol(T.c_str())
      .addImm(InlineAsm::Extra_HasSideEffects)
      .getInstr();
}
unsigned X86RubbishCodePass::getRegisterSize(MCPhysReg Reg) {

  const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(MCRegister(Reg));
  return TRI->getRegSizeInBits(*RC);
}
void X86RubbishCodePass::splitBasicBlocks(
    MachineFunction &MF, unsigned SplitAlign,
    std::vector<MachineInstr *> &NoSplitPoint) {
  std::vector<MachineInstr *> SplitPoints;
  unsigned Bound = rand() % SplitAlign + 4;
  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock::iterator I = MBB.begin(), E = --MBB.end();
    if (MBB.size() <= SplitAlign)
      continue;
    unsigned Count = 0;

    while (I != MBB.getFirstTerminator() && I != E) {
      MachineInstr *Instr = &*I;
      if (std::find(NoSplitPoint.begin(), NoSplitPoint.end(), Instr) !=
          NoSplitPoint.end()) {
        I++;
        continue;
      }
      if (Count >= Bound) {
        Count = 0;
        Bound = rand() % SplitAlign + 4;
        SplitPoints.push_back(&*I);
      }
      I++, Count++;
    }
  }

  for (MachineInstr *MI : SplitPoints) {
    MachineBasicBlock *MBB = MI->getParent();
    MBB->splitAt(*MI);
  }

  std::vector<MachineBasicBlock *> Moves;
  for (MachineBasicBlock &MBB : MF) {
    if (MBB.succ_size() == 1 && MBB.getFirstTerminator() == MBB.end()) {
      MachineBasicBlock *Succ = *MBB.succ_begin();
      MachineInstr &MI = Succ->front();
      MCSymbol *TargetSym = MI.getPreInstrSymbol();
      if (TargetSym == nullptr) {
        TargetSym = MF.getContext().createTempSymbol();
        Succ->front().setPreInstrSymbol(MF, TargetSym);
      }
      // Succ->setMachineBlockAddressTaken();
      // Succ->setLabelMustBeEmitted();
      BuildMI(MBB, MBB.end(), nullptr, TII->get(X86::JMP_4)).addSym(TargetSym);

      if (!MBB.isEntryBlock()) {
        Moves.push_back(&MBB);
      }
    }
  }
  std::shuffle(Moves.begin(), Moves.end(),
               std::default_random_engine(time(NULL)));
  for (MachineBasicBlock *MBB : Moves) {
    bool CanMove = true;
    for (auto I = MBB->pred_begin(); I != MBB->pred_end(); I++) {
      MachineBasicBlock *Pred = *I;
      if (std::find(Moves.begin(), Moves.end(), Pred) == Moves.end()) {
        CanMove = false;
      }
    }
    if (CanMove) {
      MBB->moveAfter(&MF.back());
    }
  }
}
MCPhysReg X86RubbishCodePass::getRegisterBySize(unsigned SizeInBits,
                                                std::vector<MCPhysReg> &Regs) {
  std::vector<MCPhysReg> Available;
  for (MCPhysReg Reg : Regs) {
    if (getRegisterSize(Reg) == SizeInBits) {
      Available.push_back(Reg);
    }
  }
  if (Available.size() == 0)
    return 0;
  return Available[rand() % Available.size()];
}
bool X86RubbishCodePass::queryStackSpace(int StackDelta, bool WrMem,
                                         unsigned Bits, int64_t *Result) {

  int64_t StackSize = MFI->getStackSize();
  if (!WrMem) {
    int64_t Range = StackDelta * 8 + StackSize;
    if (Range <= Bits / 8) {
      return false;
    }

    *Result = rand() % (Range - Bits / 8);

  } else {
    int64_t Range = StackDelta * 8;
    if (Range <= Bits / 8) {
      return false;
    }

    *Result = rand() % (Range - Bits / 8);
  }
  return true;
}
bool X86RubbishCodePass::buildMIWrapper(unsigned Opcode,
                                        ObfuscateInstrInfo &OII,
                                        OperandsType Ty,
                                        std::vector<MCPhysReg> &Regs,
                                        unsigned Bits, int StackDelta,
                                        bool DefFlag, bool WrMem) {
  MachineInstr *Ptr = OII.RawInst;
  MachineBasicBlock *MBB = Ptr->getParent();
  if (Ty == OperandsType::NoOpreand) {
    BuildMI(*MBB, *Ptr, Ptr->getDebugLoc(), TII->get(Opcode));
    return true;
  } else if (Ty == OperandsType::RegReg) {
    MCPhysReg Reg0 = getRegisterBySize(Bits, Regs);
    MCPhysReg Reg1 = getRegisterBySize(Bits, Regs);
    if (Reg0 == 0 || Reg1 == 0) {
      return false;
    }
    if (DefFlag) {
      BuildMI(*MBB, *Ptr, Ptr->getDebugLoc(), TII->get(Opcode))
          .addDef(Reg0)
          .addReg(Reg0)
          .addReg(Reg1);
      return true;
    } else {
      BuildMI(*MBB, *Ptr, Ptr->getDebugLoc(), TII->get(Opcode))
          .addReg(Reg0)
          .addReg(Reg1);
      return true;
    }

  } else if (Ty == OperandsType::RegImm) {
    MCPhysReg Reg0 = getRegisterBySize(Bits, Regs);
    if (Reg0 == 0) {
      return false;
    }
    if (DefFlag) {
      BuildMI(*MBB, *Ptr, Ptr->getDebugLoc(), TII->get(Opcode))
          .addDef(Reg0)
          .addReg(Reg0)
          .addImm(rand());
      return true;
    } else {
      BuildMI(*MBB, *Ptr, Ptr->getDebugLoc(), TII->get(Opcode))
          .addReg(Reg0)
          .addImm(rand());
      return true;
    }

  } else if (Ty == OperandsType::MemReg) {
    uint64_t StackSize = MFI->getStackSize();
    MCPhysReg Reg0 = getRegisterBySize(Bits, Regs);
    if (Reg0 == 0) {
      return false;
    }
    int64_t Offset;

    bool Succ = queryStackSpace(StackDelta, WrMem, Bits, &Offset);
    if (!Succ) {
      return false;
    }
    Offset &= 0xfffffffffffffffe;
    BuildMI(*MBB, *Ptr, Ptr->getDebugLoc(), TII->get(Opcode))
        .addReg(X86::RSP)
        .addImm(1)
        .addReg(X86::NoRegister)
        .addImm(Offset)
        .addReg(X86::NoRegister)
        .addReg(Reg0);
    return true;
  } else if (Ty == OperandsType::RegMem) {
    uint64_t StackSize = MFI->getStackSize();
    MCPhysReg Reg0 = getRegisterBySize(Bits, Regs);
    if (Reg0 == 0) {
      return false;
    }
    int64_t Offset;
    bool Succ = queryStackSpace(StackDelta, WrMem, Bits, &Offset);
    if (!Succ) {
      return false;
    }
    Offset &= 0xfffffffffffffffe;
    if (DefFlag) {
      BuildMI(*MBB, *Ptr, Ptr->getDebugLoc(), TII->get(Opcode))
          .addDef(Reg0)
          .addReg(Reg0)
          .addReg(X86::RSP)
          .addImm(1)
          .addReg(X86::NoRegister)
          .addImm(Offset)
          .addReg(X86::NoRegister);
      return true;
    } else {
      BuildMI(*MBB, *Ptr, Ptr->getDebugLoc(), TII->get(Opcode))
          .addReg(Reg0)
          .addReg(X86::RSP)
          .addImm(1)
          .addReg(X86::NoRegister)
          .addImm(Offset)
          .addReg(X86::NoRegister);
      return true;
    }
  } else if (Ty == OperandsType::OnlyReg) {
    MCPhysReg Reg0 = getRegisterBySize(Bits, Regs);
    if (Reg0 == 0) {
      return false;
    }
    if (DefFlag) {
      BuildMI(*MBB, *Ptr, Ptr->getDebugLoc(), TII->get(Opcode))
          .addDef(Reg0)
          .addReg(Reg0);
      return true;
    } else {
      BuildMI(*MBB, *Ptr, Ptr->getDebugLoc(), TII->get(Opcode)).addReg(Reg0);
      return true;
    }
  }
  return false;
}
void X86RubbishCodePass::generateRubbishCode(
    ObfuscateInstrInfo &OI, unsigned Depth,
    std::vector<MachineInstr *> &NoSplitPoint) {
  int Num = 4 + rand() % 4;
  MachineInstr *Ptr = OI.RawInst;

  MachineBasicBlock *MBB = Ptr->getParent();
  MachineFunction *MF = MBB->getParent();
  std::vector<MCPhysReg> &AvailableRegs = OI.AvailableRegs;
  std::vector<MCPhysReg> CommonRegs(RegList.begin(), --RegList.end());
  int StackDelta = 0;
  std::
      vector<unsigned>
          Opcodes =
              {X86::STC,         X86::CLC,         X86::CMC,
               X86::CLD,         X86::STD,         X86::SAHF,
               X86::LAHF,        X86::TEST8rr,     X86::TEST16rr,
               X86::TEST32rr,    X86::TEST64rr,    X86::CMP8rr,
               X86::CMP16rr,     X86::CMP32rr,     X86::CMP64rr,
               X86::TEST8ri,     X86::TEST16ri,    X86::TEST32ri,
               X86::TEST64ri32,  X86::CMP8ri,      X86::CMP16ri,
               X86::CMP32ri,     X86::CMP64ri32,   X86::TEST8mr,
               X86::CMP8mr,      X86::TEST16mr,    X86::CMP16mr,
               X86::TEST32mr,    X86::CMP32mr,     X86::TEST64mr,
               X86::CMP64mr,

               X86::ADD8rr,      X86::ADC8rr,      X86::SUB8rr,
               X86::SBB8rr,      X86::OR8rr,       X86::XOR8rr,
               X86::AND8rr,      X86::ADD16rr,     X86::ADC16rr,
               X86::SUB16rr,     X86::SBB16rr,     X86::OR16rr,
               X86::XOR16rr,     X86::AND16rr,     X86::ADD32rr,
               X86::ADC32rr,     X86::SUB32rr,     X86::SBB32rr,
               X86::OR32rr,      X86::XOR32rr,     X86::AND32rr,
               X86::ADD64rr,     X86::ADC64rr,     X86::SUB64rr,
               X86::SBB64rr,     X86::OR64rr,      X86::XOR64rr,
               X86::AND64rr,

               X86::ADD8ri,      X86::ADC8ri,      X86::SUB8ri,
               X86::SBB8ri,      X86::OR8ri,       X86::XOR8ri,
               X86::AND8ri,      X86::ADD16ri,     X86::ADC16ri,
               X86::SUB16ri,     X86::SBB16ri,     X86::OR16ri,
               X86::XOR16ri,     X86::AND16ri,     X86::ADD32ri,
               X86::ADC32ri,     X86::SUB32ri,     X86::SBB32ri,
               X86::OR32ri,      X86::XOR32ri,     X86::AND32ri,
               X86::ADD64ri32,   X86::ADC64ri32,   X86::SUB64ri32,
               X86::SBB64ri32,   X86::OR64ri32,    X86::XOR64ri32,
               X86::AND64ri32,

               X86::ADD8rm,      X86::ADC8rm,      X86::SUB8rm,
               X86::SBB8rm,      X86::OR8rm,       X86::XOR8rm,
               X86::AND8rm,      X86::ADD16rm,     X86::ADC16rm,
               X86::SUB16rm,     X86::SBB16rm,     X86::OR16rm,
               X86::XOR16rm,     X86::AND16rm,     X86::ADD32rm,
               X86::ADC32rm,     X86::SUB32rm,     X86::SBB32rm,
               X86::OR32rm,      X86::XOR32rm,     X86::AND32rm,
               X86::ADD64rm,     X86::ADC64rm,     X86::SUB64rm,
               X86::SBB64rm,     X86::OR64rm,      X86::XOR64rm,
               X86::AND64rm,

               X86::SHL8ri,      X86::SHL16ri,     X86::SHL32ri,
               X86::SHL64ri,     X86::SHR8ri,      X86::SHR16ri,
               X86::SHR32ri,     X86::SHR64ri,

               X86::MOV8rr,      X86::MOV16rr,     X86::MOV32rr,
               X86::MOV64rr,     X86::MOV8ri,      X86::MOV16ri,
               X86::MOV32ri,     X86::MOV64ri32,   X86::MOV8rm,
               X86::MOV16rm,     X86::MOV32rm,     X86::MOV64rm,
               X86::MOVSX16rr16, X86::MOVSX32rr32, X86::MOVZX16rr16,

               X86::LEA16r,      X86::LEA32r,      X86::LEA64r,
               X86::INC8r,       X86::INC16r,      X86::INC32r,
               X86::INC64r,      X86::DEC8r,       X86::DEC16r,
               X86::DEC32r,      X86::DEC64r,      /*X86::POP64r,
X86::PUSH64r*/};
  while (Num-- > 0) {

    if (rand() % 100 >= 90 && OI.EFlagsAvailable && Depth <= 2) {
      MachineBasicBlock *TransMBB = MF->CreateMachineBasicBlock();
      MF->insert(MF->end(), TransMBB);
      MCSymbol *TargetSym = MF->getContext().createTempSymbol();
      MachineInstr *CallInst =
          BuildMI(*MBB, *Ptr, Ptr->getDebugLoc(), TII->get(X86::CALL64pcrel32))
              .addSym(TargetSym)
              .getInstr();
      NoSplitPoint.push_back(CallInst);
      unsigned RubbishSize = (rand() % 10) * 2 + 16;
      std::vector<unsigned char> Rubbish;
      for (unsigned i = 0; i < RubbishSize; i++) {
        unsigned char C = rand() & 0xff;
        Rubbish.push_back(C);
      }
      MachineInstr *AsmInstr = insertInlineBytes(Ptr, Rubbish);
      NoSplitPoint.push_back(AsmInstr);

      MachineInstr *AddInst =
          BuildMI(*TransMBB, TransMBB->end(), nullptr, TII->get(X86::ADD64mi32))
              .addReg(X86::RSP)
              .addImm(1)
              .addReg(X86::NoRegister)
              .addImm(0)
              .addReg(X86::NoRegister)
              .addImm(RubbishSize)
              .getInstr();
      MachineInstr *RetInst =
          BuildMI(*TransMBB, TransMBB->end(), nullptr, TII->get(X86::RET64))
              .getInstr();

      ObfuscateInstrInfo OI2;
      OI2.EFlagsAvailable = OI.EFlagsAvailable;
      OI2.RawInst = AddInst;
      OI2.StackAvailable = OI.StackAvailable;
      for (MCPhysReg Reg : OI.AvailableRegs) {
        OI2.AvailableRegs.push_back(Reg);
      }
      generateRubbishCode(OI2, Depth + 1, NoSplitPoint);
      OI2.RawInst = RetInst;
      generateRubbishCode(OI2, Depth + 1, NoSplitPoint);
      TransMBB->front().setPreInstrSymbol(*MF, TargetSym);
      continue;
    } else {

      unsigned Opcode = Opcodes[rand() % Opcodes.size()];
      switch (Opcode) {
      case X86::STC:
      case X86::CLC:
      case X86::CMC:
      case X86::CLD:
      case X86::SAHF:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, NoOpreand, CommonRegs, 0, StackDelta,
                         false, false);
        }
        continue;
      case X86::LAHF:
        if (OI.EFlagsAvailable &&
            std::find(AvailableRegs.begin(), AvailableRegs.end(), X86::AH) !=
                AvailableRegs.end()) {
          buildMIWrapper(Opcode, OI, NoOpreand, CommonRegs, 0, StackDelta,
                         false, false);
        }
        continue;
      case X86::TEST8rr:
      case X86::CMP8rr:
        if (OI.EFlagsAvailable) {

          buildMIWrapper(Opcode, OI, RegReg, CommonRegs, 8, StackDelta, false,
                         false);
        }
        continue;
      case X86::TEST16rr:
      case X86::CMP16rr:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegReg, CommonRegs, 16, StackDelta, false,
                         false);
        }
        continue;
      case X86::TEST32rr:
      case X86::CMP32rr:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegReg, CommonRegs, 32, StackDelta, false,
                         false);
        }
        continue;
      case X86::TEST64rr:
      case X86::CMP64rr:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegReg, CommonRegs, 64, StackDelta, false,
                         false);
        }
        continue;

      case X86::TEST8ri:
      case X86::CMP8ri:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegImm, CommonRegs, 8, StackDelta, false,
                         false);
        }
        continue;

      case X86::TEST16ri:
      case X86::CMP16ri:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegImm, CommonRegs, 16, StackDelta, false,
                         false);
        }
        continue;

      case X86::TEST32ri:
      case X86::CMP32ri:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegImm, CommonRegs, 32, StackDelta, false,
                         false);
        }
        continue;
      case X86::TEST64ri32:
      case X86::CMP64ri32:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegImm, CommonRegs, 64, StackDelta, false,
                         false);
        }
        continue;

      case X86::TEST8mr:
      case X86::CMP8mr:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, MemReg, CommonRegs, 8, StackDelta, false,
                         false);
        }
        continue;

      case X86::TEST16mr:
      case X86::CMP16mr:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, MemReg, CommonRegs, 16, StackDelta, false,
                         false);
        }
        continue;
      case X86::TEST32mr:
      case X86::CMP32mr:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, MemReg, CommonRegs, 32, StackDelta, false,
                         false);
        }
        continue;
      case X86::TEST64mr:
      case X86::CMP64mr:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, MemReg, CommonRegs, 64, StackDelta, false,
                         false);
        }
        continue;
      case X86::ADD8rr:
      case X86::ADC8rr:
      case X86::SUB8rr:
      case X86::SBB8rr:
      case X86::OR8rr:
      case X86::XOR8rr:
      case X86::AND8rr:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegReg, AvailableRegs, 8, StackDelta, true,
                         false);
        }
        continue;

      case X86::ADD16rr:
      case X86::ADC16rr:
      case X86::SUB16rr:
      case X86::SBB16rr:
      case X86::OR16rr:
      case X86::XOR16rr:
      case X86::AND16rr:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegReg, AvailableRegs, 16, StackDelta,
                         true, false);
        }
        continue;
      case X86::ADD32rr:
      case X86::ADC32rr:
      case X86::SUB32rr:
      case X86::SBB32rr:
      case X86::OR32rr:
      case X86::XOR32rr:
      case X86::AND32rr:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegReg, AvailableRegs, 32, StackDelta,
                         true, false);
        }
        continue;
      case X86::ADD64rr:
      case X86::ADC64rr:
      case X86::SUB64rr:
      case X86::SBB64rr:
      case X86::OR64rr:
      case X86::XOR64rr:
      case X86::AND64rr:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegReg, AvailableRegs, 64, StackDelta,
                         true, false);
        }
        continue;

      case X86::MOV8rr:
        buildMIWrapper(Opcode, OI, RegReg, AvailableRegs, 8, StackDelta, false,
                       false);
        continue;

      case X86::MOV16rr:
      case X86::MOVSX16rr16:
      case X86::MOVZX16rr16:
        buildMIWrapper(Opcode, OI, RegReg, AvailableRegs, 16, StackDelta, false,
                       false);
        continue;
      case X86::MOV32rr:
      case X86::MOVSX32rr32:
        buildMIWrapper(Opcode, OI, RegReg, AvailableRegs, 32, StackDelta, false,
                       false);
        continue;
      case X86::MOV64rr:
        buildMIWrapper(Opcode, OI, RegReg, AvailableRegs, 64, StackDelta, false,
                       false);
        continue;

      case X86::ADD8ri:
      case X86::ADC8ri:
      case X86::SUB8ri:
      case X86::SBB8ri:
      case X86::OR8ri:
      case X86::XOR8ri:
      case X86::AND8ri:
      case X86::SHL8ri:
      case X86::SHR8ri:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegImm, AvailableRegs, 8, StackDelta, true,
                         false);
        }
        continue;

      case X86::ADD16ri:
      case X86::ADC16ri:
      case X86::SUB16ri:
      case X86::SBB16ri:
      case X86::OR16ri:
      case X86::XOR16ri:
      case X86::AND16ri:
      case X86::SHL16ri:
      case X86::SHR16ri:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegImm, AvailableRegs, 16, StackDelta,
                         true, false);
        }
        continue;
      case X86::ADD32ri:
      case X86::ADC32ri:
      case X86::SUB32ri:
      case X86::SBB32ri:
      case X86::OR32ri:
      case X86::XOR32ri:
      case X86::AND32ri:
      case X86::SHL32ri:
      case X86::SHR32ri:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegImm, AvailableRegs, 32, StackDelta,
                         true, false);
        }
        continue;
      case X86::ADD64ri32:
      case X86::ADC64ri32:
      case X86::SUB64ri32:
      case X86::SBB64ri32:
      case X86::OR64ri32:
      case X86::XOR64ri32:
      case X86::AND64ri32:
      case X86::SHL64ri:
      case X86::SHR64ri:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegImm, AvailableRegs, 64, StackDelta,
                         true, false);
        }
        continue;

      case X86::MOV8ri:
        buildMIWrapper(Opcode, OI, RegImm, AvailableRegs, 8, StackDelta, false,
                       false);
        continue;

      case X86::MOV16ri:
        buildMIWrapper(Opcode, OI, RegImm, AvailableRegs, 16, StackDelta, false,
                       false);
        continue;
      case X86::MOV32ri:
        buildMIWrapper(Opcode, OI, RegImm, AvailableRegs, 32, StackDelta, false,
                       false);
        continue;
      case X86::MOV64ri32:
      case X86::MOV64ri:
        buildMIWrapper(Opcode, OI, RegImm, AvailableRegs, 64, StackDelta, false,
                       false);
        continue;

      case X86::ADD8rm:
      case X86::ADC8rm:
      case X86::SUB8rm:
      case X86::SBB8rm:
      case X86::OR8rm:
      case X86::XOR8rm:
      case X86::AND8rm:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegMem, AvailableRegs, 8, StackDelta, true,
                         false);
        }
        continue;

      case X86::ADD16rm:
      case X86::ADC16rm:
      case X86::SUB16rm:
      case X86::SBB16rm:
      case X86::OR16rm:
      case X86::XOR16rm:
      case X86::AND16rm:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegMem, AvailableRegs, 16, StackDelta,
                         true, false);
        }
        continue;
      case X86::ADD32rm:
      case X86::ADC32rm:
      case X86::SUB32rm:
      case X86::SBB32rm:
      case X86::OR32rm:
      case X86::XOR32rm:
      case X86::AND32rm:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegMem, AvailableRegs, 32, StackDelta,
                         true, false);
        }
        continue;
      case X86::ADD64rm:
      case X86::ADC64rm:
      case X86::SUB64rm:
      case X86::SBB64rm:
      case X86::OR64rm:
      case X86::XOR64rm:
      case X86::AND64rm:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, RegMem, AvailableRegs, 64, StackDelta,
                         true, false);
        }
        continue;

      case X86::MOV8rm:
        buildMIWrapper(Opcode, OI, RegMem, AvailableRegs, 8, StackDelta, false,
                       false);
        continue;

      case X86::MOV16rm:
      case X86::LEA16r:
        buildMIWrapper(Opcode, OI, RegMem, AvailableRegs, 16, StackDelta, false,
                       false);
        continue;
      case X86::MOV32rm:
      case X86::LEA32r:
        buildMIWrapper(Opcode, OI, RegMem, AvailableRegs, 32, StackDelta, false,
                       false);
        continue;
      case X86::MOV64rm:
      case X86::LEA64r:
        buildMIWrapper(Opcode, OI, RegMem, AvailableRegs, 64, StackDelta, false,
                       false);
        continue;

      case X86::INC8r:
      case X86::DEC8r:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, OnlyReg, AvailableRegs, 8, StackDelta,
                         true, false);
        }
        continue;

      case X86::INC16r:
      case X86::DEC16r:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, OnlyReg, AvailableRegs, 16, StackDelta,
                         true, false);
        }
        continue;

      case X86::INC32r:
      case X86::DEC32r:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, OnlyReg, AvailableRegs, 32, StackDelta,
                         true, false);
        }
        continue;

      case X86::INC64r:
      case X86::DEC64r:
        if (OI.EFlagsAvailable) {
          buildMIWrapper(Opcode, OI, OnlyReg, AvailableRegs, 64, StackDelta,
                         true, false);
        }
        continue;
      case X86::PUSH64r:
        if (StackDelta == 0) {
          if (buildMIWrapper(Opcode, OI, OnlyReg, CommonRegs, 64, StackDelta,
                             false, false)) {
            StackDelta++;
          }
        }
        continue;
      case X86::POP64r:
        if (StackDelta > 0) {
          if (buildMIWrapper(Opcode, OI, OnlyReg, AvailableRegs, 64, StackDelta,
                             false, false)) {
            StackDelta--;
          }
        }
        continue;

      case X86::POPF64:
        if (StackDelta > 0 && OI.EFlagsAvailable) {
          if (buildMIWrapper(Opcode, OI, NoOpreand, CommonRegs, 0, StackDelta,
                             false, false)) {
            StackDelta--;
          }
        }
        continue;
      case X86::PUSHF64:
        if (StackDelta == 0) {
          if (buildMIWrapper(Opcode, OI, NoOpreand, CommonRegs, 0, StackDelta,
                             false, false)) {
            StackDelta++;
          }
        }
        continue;
      default:
        return;
      };
    }
  }
  assert(StackDelta <= 1);
  if (StackDelta == 1) {
    if (!buildMIWrapper(X86::POP64r, OI, OnlyReg, AvailableRegs, 64, StackDelta,
                        false, false)) {
      BuildMI(*MBB, *Ptr, Ptr->getDebugLoc(), TII->get(X86::LEA64r))
          .addReg(X86::RSP)
          .addReg(X86::RSP)
          .addImm(1)
          .addReg(X86::NoRegister)
          .addImm(8)
          .addReg(X86::NoRegister);
      assert(false);
    }
  }
}

void X86RubbishCodePass::process(MachineFunction &MF, ObfuscateOption &OO) {
  LivePhysRegs LiveRegs;
  std::vector<ObfuscateInstrInfo> Items;
  std::vector<MCPhysReg> CallArgRegs = {X86::RCX, X86::RDX, X86::RSI,
                                        X86::RDI, X86::R8,  X86::R9};
  for (MachineBasicBlock &MBB : MF) {
    LiveRegs.init(*MF.getSubtarget().getRegisterInfo());
    LiveRegs.addLiveOutsNoPristines(MBB);
    for (auto I = MBB.rbegin(), E = MBB.rend(); I != E; ++I) {
      ObfuscateInstrInfo Info;
      Info.StackAvailable = true;
      MachineInstr *Instr = &*I;
      Info.RawInst = Instr;
      LiveRegs.stepBackward(*I);
      for (MCPhysReg Reg : RegList) {
        if (LiveRegs.available(*MRI, Reg)) {
          Info.AvailableRegs.push_back(Reg);
        }
      }
      if (LiveRegs.available(*MRI, X86::EFLAGS)) {
        Info.EFlagsAvailable = true;
      } else {
        Info.EFlagsAvailable = false;
      }
      Items.push_back(Info);
    }
  }
  std::vector<MachineInstr *> NoSplit;
  if (OO.InsertRubbishCode) {
    for (ObfuscateInstrInfo &OI : Items) {
      generateRubbishCode(OI, 0, NoSplit);
    }
  }

  if (OO.SplitBasicBlock) {
    splitBasicBlocks(MF, 4, NoSplit);
  }

} // namespace

bool X86RubbishCodePass::checkShouldProcess(MachineFunction &MF,
                                            ObfuscateOption &OO) {

  std::vector<MachineInstr *> Marks;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.getOpcode() == X86::INLINEASM) {
        MachineOperand &MO = MI.getOperand(0);
        if (!MO.isSymbol()) {
          continue;
        }
        const char *Name = MO.getSymbolName();
        if (!strcmp(Name, "rubbish-code")) {
          Marks.push_back(&MI);
          OO.InsertRubbishCode = true;
        } else if (!strcmp(Name, "split-bb")) {
          Marks.push_back(&MI);
          OO.SplitBasicBlock = true;
        }
      }
    }
  }
  unsigned Num = Marks.size();
  for (MachineInstr *MI : Marks) {
    MI->eraseFromParent();
  }
  return Num != 0;
}
bool X86RubbishCodePass::runOnMachineFunction(MachineFunction &MF) {
  srand(time(0));
  if (!MF.getSubtarget<X86Subtarget>().is64Bit()) {
    return false;
  }
  MRI = &MF.getRegInfo();
  TII = MF.getSubtarget<X86Subtarget>().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  MFI = &MF.getFrameInfo();
  ObfuscateOption OO;
  if (checkShouldProcess(MF, OO)) {
    process(MF, OO);
    return true;
  }
  return false;
}