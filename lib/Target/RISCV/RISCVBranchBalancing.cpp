#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "RISCVSubtarget.h"
#include "RISCV.h"


using namespace llvm;


#define RISCV_BRANCH_BALANCER "RISCV Block Instruction Counter"

struct CostFromMBBToLeaf {
	MachineBasicBlock *MBB;
	unsigned int cost;
};

std::vector<struct CostFromMBBToLeaf> CostsFromMBBToLeaves;

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
/*	errs() << MF.getName() << "\n";
	
	for(MachineBasicBlock& MB : MF) {
		errs() << MB.getName() << ":\t" << MDTREF.getNode(&MB)->getLevel() << "\n";
	}
	
	errs() << "\n\n";
*/	
	unsigned int costToLeaf = 0;
	unsigned int maxInstr = 0;
	unsigned int instrCount = 0;
	
	bool waitForBranch = false;
	
	for(MachineBasicBlock& MBB : MF) {
	
		/*for(MachineInstr &MTerm : MBB.terminators()){
			errs() << MF.getName() << "\tterminator\n";
			MTerm.dump();
		}*/
		costToLeaf = maxInstr = instrCount = 0;
		
		/*Finding the leaves: nodes with no children in the dominator 
		  tree and just one predecessor in the control flow graph.*/
		if((MDTREF.getNode(&MBB)->getNumChildren() == 0) && (MBB.pred_size() == 1)){ //invalidating multiple entry points, for now
		
			
			
			/*Balancing operations: to be performed by considering all 
			  concurrent children. If the node has more predecessors in
			  the CFG such balancing must be made on all the predecerrors 
			  together or on each predecessor separatelly. According to
			  that the max number of instruction will need to be reset
			  to 0 at each iteration*/
			for(MachineBasicBlock *pred : MBB.predecessors()){
				//maxInstr = instrCount = 0; //what when we have multiple entry points?
			errs() << "BEGINNING  " << MBB.getParent()->getName() << "  " << MBB.getName() << "\n\tcost: " << costToLeaf << "\n\tmax: " << maxInstr << "\n\n";
				waitForBranch = false;
				costToLeaf = maxInstr = instrCount = 0;
				
				for(MachineBasicBlock *succ : pred->successors()){
					if(MDTREF.getNode(succ)->getNumChildren() != 0)
						waitForBranch = true;
				}
				
				if(!waitForBranch) {
				
				
				struct CostFromMBBToLeaf NewCostFromMBBToLeaf;
		
				/*Searching previously computed costs. If this leaf 
				  is original it will have no previous cost*/
			
				for(CostFromMBBToLeaf CL : CostsFromMBBToLeaves){
					if(CL.MBB == &MBB){
						costToLeaf = CL.cost;
						errs() << "FOUND:  " << CL.MBB->getParent()->getName() << "  " << MBB.getName() << "\n";
					}
				}
				
				/*Searching the maximum number of instructions among the 'brothers'*/
				for(MachineBasicBlock *succ : pred->successors()){
					if((succ->size() > maxInstr) && (!MBB.isSuccessor(succ))){
						maxInstr = succ->size();
					}
				}
				
				/*Searching for direct paths between the predecessor 
				  and the successor of the current block, if any, there's 
				  a chance to skip the current block according to some 
				  conditions. Such chance must be eliminated by interleaving 
				  a dummy block equal in size*/
				for(MachineBasicBlock *succ : pred->successors()){
					if(MBB.isSuccessor(succ)) {
						MachineBasicBlock *dummyMBB = MF.CreateMachineBasicBlock(MBB.getBasicBlock());
						//dummyMB->addSuccessor(succ);
						//pred->removeSuccessor(succ);
						//pred->addSuccessor(dummyMB);
						//dummyMB->transferSuccessorsAndUpdatePHIs(pred);
			
						MF.insert(++pred->getIterator(), dummyMBB);
						
						MachineInstr& MI = pred->back();
						
						/*The cost of previously processed nested nodes is considered too (costToLeaf)*/
						for(instrCount = dummyMBB->size(); instrCount < maxInstr+costToLeaf-1;) {
							BuildMI(*dummyMBB, dummyMBB->end(), MI.getDebugLoc(), TII.get(RISCV::ADDI))
							.addReg(RISCV::X0)
							.addReg(RISCV::X0)
							.addImm(0);			//RISC-V NOOP OPERATION: ADDI $X0, $X0, 0
							instrCount++;
						}
						
						/*Setting the links with the new block*/
						BuildMI(*dummyMBB, dummyMBB->end(), MI.getDebugLoc(), TII.get(RISCV::JAL))
						.addReg(RISCV::X0)
						.addMBB(succ);
						
						//pred->replaceSuccessor(succ, dummyMBB);
						pred->removeSuccessor(succ);
						
						for(MachineInstr &MTerm : pred->terminators()){
							if(MTerm.isBranch()){
								if(succ == MTerm.getOperand(MTerm.getNumOperands()-1).getMBB())
									MTerm.getOperand(MTerm.getNumOperands()-1).setMBB(dummyMBB);
							}
						}
						
						//createNopBlock();
					}
				}
				
				/*Balancing all the brothers, also the dummy ones*/
				for(MachineBasicBlock *succ : pred->successors()){
					MachineInstr& MI = succ->instr_front();
					
					for(unsigned int instrCount = succ->size(); instrCount < maxInstr + costToLeaf; instrCount++){
						BuildMI(*succ, MI, MI.getDebugLoc(), TII.get(RISCV::ADDI))		
							.addReg(RISCV::X0)
							.addReg(RISCV::X0)
							.addImm(0);			//RISC-V NOOP OPERATION: ADDI $X0, $X0, 0
					}
				}
				
				/*Keeping record of the costs of the leaves*/
				NewCostFromMBBToLeaf.MBB = pred;
				NewCostFromMBBToLeaf.cost = maxInstr + costToLeaf;
				CostsFromMBBToLeaves.push_back(NewCostFromMBBToLeaf);
				errs() << MBB.getParent()->getName() << "  " << MBB.getName() << "\n\tcost: " << costToLeaf << "\n\tmax: " << maxInstr << "\n\n";
			
				} //waitForBranch
			
			
			}
			
		}
			
	}
	
}


void RISCVBranchBalancer::equalBlocks(MachineFunction& MF) {
	int InstrCount = 0;
	int maxInstrPerBlock = 0;
	const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
	
	
	for(MachineBasicBlock& MBB : MF) {
		InstrCount = 0;
		
		for(MachineInstr& MI : MBB) {
			InstrCount ++;
		}
		errs() << InstrCount << "\t" << MBB.getFullName() << "\n";
		if(InstrCount > maxInstrPerBlock)
			maxInstrPerBlock = InstrCount;
		
		}
			
			
	for(MachineBasicBlock& MBB : MF){
		InstrCount = 0;
		
		for(MachineInstr& MI : MBB){
			InstrCount ++;
		}
		MachineInstr& MI = MBB.instr_front();
		
		for(; InstrCount < maxInstrPerBlock;) {
			BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(RISCV::ADDI))		//RISC-V NOOP OPERATION: ADDI $X0, $X0, 0
				.addReg(RISCV::X0)
				.addReg(RISCV::X0)
				.addImm(0);
			InstrCount++;
		}
	}
			
	InstrCount = 0;
	for(MachineBasicBlock& MBB : MF) {
		for(MachineInstr& MI : MBB) {
			InstrCount ++;
		}
		errs() << InstrCount << "\t" << MBB.getFullName() << "\n";
		
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
