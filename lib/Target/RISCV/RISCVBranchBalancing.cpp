#include "llvm/CodeGen/MachineFunctionPass.h"
#include "RISCV.h"


using namespace llvm;


#define RISCV_BRANCH_BALANCER "RISCV Block Instruction Counter"


namespace{

	
	
	struct RISCVBranchBalancer : public MachineFunctionPass{
		static char ID;
		RISCVBranchBalancer() : MachineFunctionPass(ID) {}
		
		bool runOnMachineFunction(MachineFunction& MF){
		
			int InstrCount = 0;
			
			for(MachineBasicBlock& MB : MF){
				for(MachineInstr& MI : MB){
					InstrCount ++;
				}
				errs() << InstrCount << "\t" << MB.getFullName() << "\n";
				InstrCount = 0;
			}
			
			return false;
		}
		
	};
	
	
}

char RISCVBranchBalancer::ID = 0;

INITIALIZE_PASS(RISCVBranchBalancer, "riscv-block-instruction-counter",
                RISCV_BRANCH_BALANCER, false, false)

FunctionPass *llvm::createRISCVBranchBalancerPass() {
  return new RISCVBranchBalancer();
}
