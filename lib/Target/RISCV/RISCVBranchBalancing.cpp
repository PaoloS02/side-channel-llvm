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
		MachineDominatorTree *MDT, *MDTREF;
		
		void getAnalysisUsage(AnalysisUsage &AU) const override {
			AU.addRequired<MachineDominatorTree>();
			MachineFunctionPass::getAnalysisUsage(AU);
		}
		
		void equalBlocks(MachineFunction& MF);
		
		void findDomTreeLeaves(MachineFunction& MF, MachineDominatorTree& MDT, MachineDominatorTree& MDTREF);
		
		bool runOnMachineFunction(MachineFunction& MF) override ;
		
	};
		
}


char RISCVBranchBalancer::ID = 0;

INITIALIZE_PASS(RISCVBranchBalancer, "riscv-block-instruction-counter",
                RISCV_BRANCH_BALANCER, false, false)
//INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)


void RISCVBranchBalancer::findDomTreeLeaves(MachineFunction& MF, MachineDominatorTree& MDT, MachineDominatorTree& MDTREF) {
	
	const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
	errs() << MF.getName() << "\n";
	
	for(MachineBasicBlock& MB : MF) {
		errs() << MB.getName() << ":\t" << MDTREF.getNode(&MB)->getLevel() << "\n";
	}
	
	errs() << "\n\n";
	
	for(MachineBasicBlock& MB : MF) {
	
		for(MachineInstr &MTerm : MB.terminators()){
			errs() << MF.getName() << "\tterminator\n";
			MTerm.dump();
		}
		
		if((MDTREF.getNode(&MB)->getNumChildren() == 0) && (MB.pred_size() == 1)){ //invalidating multiple entry points, for now
			errs() << MB.getName() << "\n";
			
			for(MachineBasicBlock *pred : MB.predecessors()){
				unsigned int maxInstr = 0, instrCount;
				
				for(MachineBasicBlock *succ : pred->successors()){
					if((succ->size() > maxInstr) && (!MB.isSuccessor(succ))){
						maxInstr = succ->size();
					}
				}
				
				for(MachineBasicBlock *succ : pred->successors()){
					if(MB.isSuccessor(succ)) {
						MachineBasicBlock *dummyMB = MF.CreateMachineBasicBlock(pred->getBasicBlock());
						//dummyMB->addSuccessor(succ);
						//pred->removeSuccessor(succ);
						//pred->addSuccessor(dummyMB);
						//dummyMB->transferSuccessorsAndUpdatePHIs(pred);
					errs() << "NOT BLOCK CREATION: " << dummyMB->size() << "\n";
					dummyMB->dump();
					pred->getBasicBlock()->dump();
					MF.insert(++pred->getIterator(), dummyMB);
					
						MachineInstr& MI = pred->back();
					errs() << "NOT FIRST INSTRUCTION DETECTION IN THE NEWLY CREATED BLOCK\n";
						
						for(instrCount = dummyMB->size(); instrCount < maxInstr-1;) {
							BuildMI(*dummyMB, dummyMB->end(), MI.getDebugLoc(), TII.get(RISCV::ADDI))
							.addReg(RISCV::X0)
							.addReg(RISCV::X0)
							.addImm(0);			//RISC-V NOOP OPERATION: ADDI $X0, $X0, 0
							instrCount++;
						}
						BuildMI(*dummyMB, dummyMB->end(), MI.getDebugLoc(), TII.get(RISCV::JAL))
						.addReg(RISCV::X0)
						.addMBB(succ);
						
						pred->replaceSuccessor(succ, dummyMB);
						
						for(MachineInstr &MTerm : pred->terminators()){
							if(MTerm.isBranch()){
								if(succ == MTerm.getOperand(MTerm.getNumOperands()-1).getMBB())
									MTerm.getOperand(MTerm.getNumOperands()-1).setMBB(dummyMB);
							}
						}
						
						//createNopBlock();
					}
				}
			}
		}
			
	}
	
	errs() << "\nEND\n\n";
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
			MDTREF = &getAnalysis<MachineDominatorTree>();
			
			//equalBlocks(MF);
			
			findDomTreeLeaves(MF, *MDT, *MDTREF);
			
			return true;
}




FunctionPass *llvm::createRISCVBranchBalancerPass() {
  return new RISCVBranchBalancer();
}
