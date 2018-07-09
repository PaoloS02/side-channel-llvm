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
		
		void getAnalysisUsage(AnalysisUsage &AU) const override {
			AU.addRequired<MachineDominatorTree>();
			MachineFunctionPass::getAnalysisUsage(AU);
		}
		
		void equalBlocks(MachineFunction& MF);
		
		void findDomTreeLeaves(MachineFunction& MF, MachineDominatorTree& MDT);
		
		bool runOnMachineFunction(MachineFunction& MF) override ;
		
	};
		
}


char RISCVBranchBalancer::ID = 0;

INITIALIZE_PASS(RISCVBranchBalancer, "riscv-block-instruction-counter",
                RISCV_BRANCH_BALANCER, false, false)
//INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)


void RISCVBranchBalancer::findDomTreeLeaves(MachineFunction& MF, MachineDominatorTree& MDT) {
	
	errs() << MF.getName() << "\n";
	
	for(MachineBasicBlock& MB : MF) {
		errs() << MB.getName() << ":\t" << MDT.getNode(&MB)->getLevel() << "\n";
	}
	
	errs() << "\n\n";
	
	for(MachineBasicBlock& MB : MF) {
		if((MDT.getNode(&MB)->getNumChildren() == 0) && (MB.pred_size() == 1))
			errs() << MB.getName() << "\n";
	}
	
	errs() << "\nEND\n";
}


void RISCVBranchBalancer::equalBlocks(MachineFunction& MF) {
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
			
			MDT = &getAnalysis<MachineDominatorTree>();
			
			//equalBlocks(MF);
			
			findDomTreeLeaves(MF, *MDT);
			
			return true;
}




FunctionPass *llvm::createRISCVBranchBalancerPass() {
  return new RISCVBranchBalancer();
}
