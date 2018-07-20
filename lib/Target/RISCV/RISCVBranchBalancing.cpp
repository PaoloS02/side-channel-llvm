#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "RISCVSubtarget.h"
#include "RISCV.h"


using namespace llvm;


#define RISCV_BRANCH_BALANCER_NAME "RISCV Block Instruction Counter"

struct CostFromMBBToLeaf {
	MachineBasicBlock *MBB;
	unsigned int cost;
};



namespace{

	struct RISCVBranchBalancer : public MachineFunctionPass{
	
		static char ID;
		RISCVBranchBalancer() : MachineFunctionPass(ID) {}
		MachineDominatorTree *MDT;
		
		void getAnalysisUsage(AnalysisUsage &AU) const override {
			AU.addRequired<MachineDominatorTree>();
			MachineFunctionPass::getAnalysisUsage(AU);
		}
		
		StringRef getPassName() const override {
 			return RISCV_BRANCH_BALANCER_NAME;
		}
		
		void displayInfo(MachineFunction& MF);
		void balanceBlockSizes(MachineFunction& MF);
		void balanceBranchSizes(MachineFunction& MF, MachineDominatorTree& MDT);
		
		bool runOnMachineFunction(MachineFunction& MF) override ;
		
	};
		
}


char RISCVBranchBalancer::ID = 0;

INITIALIZE_PASS(RISCVBranchBalancer, "riscv-branch-balancer",
                RISCV_BRANCH_BALANCER_NAME, false, false)
//INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)


/*unsigned RISCVInstrCycleCount(MachineInstr *MI) {
	
}*/

unsigned int computeCostToLeaf(MachineBasicBlock *DestMBB, MachineBasicBlock *SourceMBB) {
	MachineBasicBlock *MidMBB = SourceMBB;
	unsigned int sum = 0;
	
	for(; MidMBB != DestMBB && MidMBB->succ_size() != 0;) {
		sum += MidMBB->size();
		MidMBB = *MidMBB->succ_begin();
	}
//	errs() << SourceMBB->getParent()->getName() << "FROM  : " << SourceMBB->getName() << "  TO: " << DestMBB->getName() <<"  COST: " << sum << "\n";
	return sum;
}

bool isReachableFrom(MachineBasicBlock *DestMBB, MachineBasicBlock *SourceMBB) {
	MachineBasicBlock *MidMBB = SourceMBB;
	
	for(; MidMBB != DestMBB && MidMBB->succ_size() != 0;) {
		MidMBB = *MidMBB->succ_begin();
	}
	if(MidMBB == DestMBB)
		return true;
	
	return false;
}

MachineBasicBlock* firstNotDominatedSuccessor(MachineBasicBlock *MBB, MachineDominatorTree& MDT) {
	
	MachineBasicBlock *base = nullptr;
	
//	errs() << "++++++++ initial\n";
//	errs() << MBB->getParent()->getName() << "  &MBB content: " << MBB << "  " << MBB->getName() <<"\n";
//	errs() << "++++++++\n";
	
	for(base = MBB; MDT.dominates(MBB, base) && (base->succ_size() > 0);) {
		
		base = *base->succ_begin();		
/*		errs() << "--------------- depth successors search:\n";
		errs() << base->getParent()->getName() << "  &MBB content: " << base << "  " << base->getName() <<"\n";
		errs() << "---------------\n";
*/		
	}
	
	return base;
}

bool hasToBeErased(MachineBasicBlock *MBB, std::vector<MachineBasicBlock *> oldMDTNodes){
	for(unsigned int i=0; i < oldMDTNodes.size(); i++){
		if(oldMDTNodes.at(i) == MBB) {
	//	errs() << "IN-LIST: " << MBB->getParent()->getName() << "  " << MBB->getName() << "\n";
			return true;
			}
	}
	
	return false;
}

bool hasBeenErased(MachineBasicBlock *MBB, std::vector<MachineBasicBlock *> erasedMDTNodes) {
	for(unsigned int i=0; i < erasedMDTNodes.size(); i++){
		if(erasedMDTNodes.at(i) == MBB) {
	//	errs() << "IN-LIST: " << MBB->getParent()->getName() << "  " << MBB->getName() << "\n";
			return true;
			}
	}
	
	return false;
}

bool hasMDTNode(MachineBasicBlock *MBB, std::vector<MachineBasicBlock *> notMDTNodes) {

	for(unsigned int i=0; i < notMDTNodes.size(); i++){
		if(notMDTNodes.at(i) == MBB) {
	//	errs() << "IN-LIST: " << MBB->getParent()->getName() << "  " << MBB->getName() << "\n";
			return false;
			}
	}
	
	return true;
}


void RISCVBranchBalancer::balanceBranchSizes(MachineFunction& MF, MachineDominatorTree& MDT) {
	
	std::vector<struct CostFromMBBToLeaf> CostsFromMBBToLeaves;
	std::vector<MachineBasicBlock *> oldMDTNodes;
	std::vector<MachineBasicBlock *> notMDTNodes;
	std::vector<MachineBasicBlock *> erasedMDTNodes;
	const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
	
	unsigned int costToLeaf = 0;
	unsigned int maxInstr = 0;
	unsigned int instrCount = 0;
	unsigned int nestLevel = 0;
	
	for(MachineBasicBlock& MBB : MF) {
		if(MDT.getNode(&MBB)->getLevel() > nestLevel)
			nestLevel = MDT.getNode(&MBB)->getLevel();
	}
	
	for(; nestLevel > 0; nestLevel--) {
	
		for(MachineBasicBlock& MBB : MF) {
//	errs() << "Initial check:  " << MBB.getParent()->getName() << "  &MBB content: " << &MBB << "  " << MBB.getName() <<"\n";
			costToLeaf = maxInstr = instrCount = 0;
			
			/*Finding the leaves: nodes with no children in the dominator 
			  tree and just one predecessor in the control flow graph.*/

			if(hasMDTNode(&MBB, notMDTNodes)) {
				if(MDT.getNode(&MBB)->getLevel() == nestLevel) {
					
					
					/*Balancing operations: to be performed by considering all 
					  concurrent children. If the node has more predecessors in
					  the CFG such balancing must be made on all the predecerrors 
					  together or on each predecessor separatelly. According to
					  that the max number of instruction will need to be reset
					  to 0 at each iteration*/
					  
					for(MachineBasicBlock *pred : MBB.predecessors()){ //what when we have multiple entry points?
						costToLeaf = maxInstr = instrCount = 0;

						struct CostFromMBBToLeaf NewCostFromMBBToLeaf;
		
						MachineBasicBlock *BalanceBase = firstNotDominatedSuccessor(&MBB, MDT);
						
						for(MachineBasicBlock *succ : pred->successors()){
							if(isReachableFrom(BalanceBase, succ)) {
								costToLeaf = computeCostToLeaf(BalanceBase, succ);
								if(costToLeaf > maxInstr)
									maxInstr = costToLeaf; 
							}
						}
						
	
						/*Searching for direct paths between the predecessor 
						  and the successor of the current block, if any, there's 
						  a chance to skip the current block according to some 
						  conditions. Such chance must be eliminated by interleaving 
						  a dummy block equal in size*/
						for(MachineBasicBlock *succ : pred->successors()){
							if(succ != &MBB && isReachableFrom(succ, &MBB)) {
								MachineBasicBlock *dummyMBB = MF.CreateMachineBasicBlock(MBB.getBasicBlock());
//		errs() << "NEED DUMMY  " << MBB.getParent()->getName() << "  " << MBB.getName() << "  succ:  " << succ->getName() << "\n";
						//dummyMBB->addSuccessor(succ);
						//pred->removeSuccessor(succ);
						//pred->addSuccessor(dummyMBB);
						//dummyMB->transferSuccessorsAndUpdatePHIs(pred);
	
								MF.insert(++pred->getIterator(), dummyMBB);
								MachineInstr& MI = pred->back();
//			errs() << "here  maxInstr:  " << maxInstr << "  dummy size:  " << dummyMBB->size() << "\n";			
						/*The cost of previously processed nested nodes is considered too (costToLeaf)*/
								for(instrCount = dummyMBB->size(); instrCount+1 < maxInstr;) { //to avoid negative maxInstr that would be a problem for unsigned values
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
								dummyMBB->addSuccessor(succ);
										
								for(MachineInstr &MTerm : pred->terminators()){
									if(MTerm.isBranch()){
										if(succ == MTerm.getOperand(MTerm.getNumOperands()-1).getMBB())
											MTerm.getOperand(MTerm.getNumOperands()-1).setMBB(dummyMBB);
									}
								}
								notMDTNodes.push_back(dummyMBB);
							} //need a dummy block?
						} //check brothers
				
						/*Balancing all the brothers, also the dummy ones*/
						for(MachineBasicBlock *succ : pred->successors()){
							if(isReachableFrom(BalanceBase, succ)) {
								MachineInstr& MI = succ->instr_front();
					
								for(unsigned int instrCount = computeCostToLeaf(BalanceBase, succ); 
												 instrCount < maxInstr; 
												 instrCount++)
								{
									BuildMI(*succ, MI, MI.getDebugLoc(), TII.get(RISCV::ADDI))		
										.addReg(RISCV::X0)
										.addReg(RISCV::X0)
										.addImm(0);			//RISC-V NOOP OPERATION: ADDI $X0, $X0, 0
								}
							}
						}
				
						/*Keeping record of the costs of the leaves*/
						NewCostFromMBBToLeaf.MBB = pred;
						NewCostFromMBBToLeaf.cost = maxInstr;
						CostsFromMBBToLeaves.push_back(NewCostFromMBBToLeaf);

					} //for all the predecessors
				} //check nest level
			} //hasMDTNode
		} //MBB:MF
	} //nest level
}


void RISCVBranchBalancer::balanceBlockSizes(MachineFunction& MF) {
	unsigned int InstrCount = 0;
	unsigned int maxInstrPerBlock = 0;
	const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
	
	
	for(MachineBasicBlock& MBB : MF) {
		errs() << InstrCount << "\t" << MBB.getFullName() << "\n";
		if(MBB.size() > maxInstrPerBlock)
			maxInstrPerBlock = MBB.size();
		}
			
			
	for(MachineBasicBlock& MBB : MF){
		MachineInstr& MI = MBB.instr_front();
		for(InstrCount = MBB.size(); InstrCount < maxInstrPerBlock; InstrCount++) {
			BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(RISCV::ADDI))		//RISC-V NOOP OPERATION: ADDI $X0, $X0, 0
				.addReg(RISCV::X0)
				.addReg(RISCV::X0)
				.addImm(0);
		}
	}
	
	for(MachineBasicBlock& MBB : MF) {
		errs() << MBB.size() << "\t" << MBB.getFullName() << "\n";
	}	

}


void RISCVBranchBalancer::displayInfo(MachineFunction& MF) {
	/*const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
	const InstrItineraryData *ItinData = MF.getSubtarget().getInstrItineraryData();
	const InstrStage *IST;
	*/
	//unsigned int cycles = 0;
	//const MCInstrDesc & 	getDesc ();
	//MI.getDesc().getSchedClass();
	errs() << MF.getName() << "\n";
	for(MachineBasicBlock& MBB : MF) {
		errs() << "  " << MBB.getName() << "\n";
	//	cycles = 0;
		for(MachineInstr& MI : MBB) {
			errs() << "    " << MI.getOpcode() << "  " << ((MI.getOpcode()/10)%14)+1 << "  ";
			MI.dump();
		//	errs() << "      " << MI.getOpcode() << ", cycles: " << "\n";
		}
	}
}


bool RISCVBranchBalancer::runOnMachineFunction(MachineFunction& MF) {
			
			MDT = &getAnalysis<MachineDominatorTree>();
			
			//balanceBlockSizes(MF);
			
			balanceBranchSizes(MF, *MDT);
			displayInfo(MF);
			
			return true;
}




FunctionPass *llvm::createRISCVBranchBalancerPass() {
  return new RISCVBranchBalancer();
}
