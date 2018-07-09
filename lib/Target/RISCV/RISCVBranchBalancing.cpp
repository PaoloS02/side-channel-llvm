#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "RISCVSubtarget.h"
#include "RISCV.h"


using namespace llvm;


#define RISCV_BRANCH_BALANCER "RISCV Block Instruction Counter"


namespace{

	struct RISCVBranchBalancer : public MachineFunctionPass{
	
		static char ID;
		RISCVBranchBalancer() : MachineFunctionPass(ID) {}
		MachineDominatorTree *MDT;
		
		/*void getAnalysisUsage(AnalysisUsage &AU) const override {
			AU.addRequired<MachineDominatorTree>();
			MachineFunctionPass::getAnalysisUsage(AU);
		  }
		*/
		void EqualBlocks(MachineFunction& MF);
		
		bool runOnMachineFunction(MachineFunction& MF) override ;
		
	};
		
}


char RISCVBranchBalancer::ID = 0;

INITIALIZE_PASS(RISCVBranchBalancer, "riscv-block-instruction-counter",
                RISCV_BRANCH_BALANCER, false, false)
//INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)

void RISCVBranchBalancer::EqualBlocks(MachineFunction& MF) {
	int InstrCount = 0;
	int maxInstrPerBlock = 0;
	const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
	
	for(MachineBasicBlock& MB : MF) {
		InstrCount = 0;
		
		for(MachineInstr& MI : MB) {
			InstrCount ++;
		}
		errs() << InstrCount << "\t" << MB.getFullName() << "\n";
		if(InstrCount > maxInstrPerBlock)
			maxInstrPerBlock = InstrCount;
		
		}
			
			
	for(MachineBasicBlock& MB : MF){
		InstrCount = 0;
		
		for(MachineInstr& MI : MB){
			InstrCount ++;
		}
		MachineInstr& MI = MB.instr_front();
		
		for(; InstrCount < maxInstrPerBlock;) {
			BuildMI(MB, MI, MI.getDebugLoc(), TII.get(RISCV::ADDI))		//RISC-V NOOP OPERATION: ADDI $X0, $X0, 0
				.addReg(RISCV::X0)
				.addReg(RISCV::X0)
				.addImm(0);
			InstrCount++;
		}
	}
			
	InstrCount = 0;
	for(MachineBasicBlock& MB : MF) {
		for(MachineInstr& MI : MB) {
			InstrCount ++;
		}
		errs() << InstrCount << "\t" << MB.getFullName() << "\n";
		
		InstrCount = 0;
	}	
}


bool RISCVBranchBalancer::runOnMachineFunction(MachineFunction& MF) {
		
			EqualBlocks(MF);
			
			return true;
}




FunctionPass *llvm::createRISCVBranchBalancerPass() {
  return new RISCVBranchBalancer();
}
