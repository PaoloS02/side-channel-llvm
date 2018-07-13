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

void RISCVBranchBalancer::findDomTreeLeaves(MachineFunction& MF, MachineDominatorTree& MDT, MachineDominatorTree& MDTREF) {
	
	std::vector<struct CostFromMBBToLeaf> CostsFromMBBToLeaves;
	std::vector<MachineBasicBlock *> oldMDTNodes;
	std::vector<MachineBasicBlock *> notMDTNodes;
	std::vector<MachineBasicBlock *> erasedMDTNodes;
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
	unsigned int nestLevel = 0;
	
	bool waitForBranch = false;
	bool waitForLevel = false;
	
	for(MachineBasicBlock& MBB : MF) {
		if(MDT.getNode(&MBB)->getLevel() > nestLevel)
			nestLevel = MDT.getNode(&MBB)->getLevel();
	}
	
	for(; nestLevel > 0; nestLevel--) {
	
	for(MachineBasicBlock& MBB : MF) {
//	errs() << MBB.getParent()->getName() << "  &MBB content: " << &MBB << "  " << MBB.getName() <<"\n";
	
		/*for(MachineInstr &MTerm : MBB.terminators()){
			errs() << MF.getName() << "\tterminator\n";
			MTerm.dump();
		}*/
		costToLeaf = maxInstr = instrCount = 0;
	//errs() << MBB.getParent()->getName() << "  " << MBB.getName() << "  pre\n\n";
		/*Finding the leaves: nodes with no children in the dominator 
		  tree and just one predecessor in the control flow graph.*/
		if(hasMDTNode(&MBB, notMDTNodes)){
	errs() << MBB.getName() << "  hasMDTNode\n";
	
	for(unsigned int ci = 0; ci < MDT.getNode(&MBB)->getChildren().size(); ci++){
		errs() << "  " << MDT.getNode(&MBB)->getChildren().at(ci)->getBlock()->getName();
	}	
	
	errs() << "\n";
	
		if((MDT.getNode(&MBB)->getNumChildren() == 0) && (MBB.pred_size() == 1)){ //invalidating multiple entry points, for now
		
//			errs() << "pre\n\n";
			
			/*Balancing operations: to be performed by considering all 
			  concurrent children. If the node has more predecessors in
			  the CFG such balancing must be made on all the predecerrors 
			  together or on each predecessor separatelly. According to
			  that the max number of instruction will need to be reset
			  to 0 at each iteration*/
			for(MachineBasicBlock *pred : MBB.predecessors()){
		//	errs() << "pre\n\n";
				//maxInstr = instrCount = 0; //what when we have multiple entry points?
		//	errs() << "BEGINNING  " << MBB.getParent()->getName() << "  " << MBB.getName() << "\n\tcost: " << costToLeaf << "\n\tmax: " << maxInstr << "\n\n";
		errs() << "BEGINNING  " << MBB.getParent()->getName() << "  " << MBB.getName() << "\n";
				waitForBranch = false;
				waitForLevel = false;
				costToLeaf = maxInstr = instrCount = 0;
				//std::vector<DomTreeNodeBase *>::iterator child;
				
				//for(unsigned int child = 0; child < (MDT.getNode(pred))->getChildren().size(); child++){
				//	if(((MDT.getNode(pred))->getChildren().at(child))->getNumChildren() != 0)
				for(MachineBasicBlock *succ : pred->successors()){
					if(hasMDTNode(succ, notMDTNodes)) {
						if(MDT.getNode(succ)->getNumChildren() != 0)
							waitForBranch = true;
					}
				}

				
				if(MDT.getNode(&MBB)->getLevel() < nestLevel)
					waitForLevel = true;
				
				
				if(!waitForBranch && !waitForLevel) {
//		errs() << "preB\n\n";	
		errs() << "DON'T WAIT  " << MBB.getParent()->getName() << "  " << MBB.getName() << "\n";
				
				struct CostFromMBBToLeaf NewCostFromMBBToLeaf;
		
				/*Searching previously computed costs. If this leaf 
				  is original it will have no previous cost*/
			
				
				
				/*Searching the maximum number of instructions among the 'brothers'*/
				for(MachineBasicBlock *succ : pred->successors()){
					if(!MBB.isSuccessor(succ)){
						costToLeaf = 0;
						for(CostFromMBBToLeaf CL : CostsFromMBBToLeaves){
							if(CL.MBB == succ){
								costToLeaf = CL.cost;
		//					errs() << "FOUND:  " << CL.MBB->getParent()->getName() << "  " << MBB.getName() << "\n";
							}
						}
						if((succ->size()+costToLeaf > maxInstr) && (!MBB.isSuccessor(succ))){
							maxInstr = succ->size()+costToLeaf;
						}
					}

				}
				
				/*Searching for direct paths between the predecessor 
				  and the successor of the current block, if any, there's 
				  a chance to skip the current block according to some 
				  conditions. Such chance must be eliminated by interleaving 
				  a dummy block equal in size*/
				for(MachineBasicBlock *succ : pred->successors()){
					if(MBB.isSuccessor(succ)) { //FIXME: might need to recurse this on the 
												//successor chain in order to be applied to all the levels
//					errs() << "brothers:  " << succ->getParent()->getName() << "  " << succ->getName() << "\n\n";
						MachineBasicBlock *dummyMBB = MF.CreateMachineBasicBlock(MBB.getBasicBlock());
		errs() << "NEED DUMMY  " << MBB.getParent()->getName() << "  " << MBB.getName() << "\n";
						//dummyMBB->addSuccessor(succ);
						//pred->removeSuccessor(succ);
						//pred->addSuccessor(dummyMBB);
						//dummyMB->transferSuccessorsAndUpdatePHIs(pred);
//		errs() << "preC\n\n";	
						MF.insert(++pred->getIterator(), dummyMBB);
						
						MachineInstr& MI = pred->back();
						
						/*The cost of previously processed nested nodes is considered too (costToLeaf)*/
						for(instrCount = dummyMBB->size(); instrCount < maxInstr-1;) {
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
						
						//createNopBlock();
					}
				}
				
				/*Balancing all the brothers, also the dummy ones*/
				for(MachineBasicBlock *succ : pred->successors()){
					MachineInstr& MI = succ->instr_front();
					
					for(unsigned int instrCount = succ->size(); instrCount < maxInstr; instrCount++){
						BuildMI(*succ, MI, MI.getDebugLoc(), TII.get(RISCV::ADDI))		
							.addReg(RISCV::X0)
							.addReg(RISCV::X0)
							.addImm(0);			//RISC-V NOOP OPERATION: ADDI $X0, $X0, 0
					}
				}
				
				/*Keeping record of the costs of the leaves*/
				NewCostFromMBBToLeaf.MBB = pred;
				NewCostFromMBBToLeaf.cost = maxInstr;
				CostsFromMBBToLeaves.push_back(NewCostFromMBBToLeaf);
			//	errs() << MBB.getParent()->getName() << "  " << MBB.getName() << "\n\tcost: " << costToLeaf << "\n\tmax: " << maxInstr << "\n\n";
				
//		errs() << "\n";
				//for(MachineBasicBlock *pred : MBB.predecessors()){
					//for(MachineBasicBlock *succ : pred->successors()){
				
						oldMDTNodes.push_back(&MBB);
		errs() << "ADDED TO ERASE-LIST: " << MBB.getParent()->getName() << " " << MBB.getName() << " level: " << nestLevel << " blockLevel: " << MDT.getNode(&MBB)->getLevel() << "\n";
						notMDTNodes.push_back(&MBB);
				//	}
				//}
					
//		errs() << "\n!!!!!\n";
			
				} //waitForBranch
			
			
			}
			
			
			
		}
		
		}
			
	}
	
	
	for(MachineBasicBlock& MBB : MF) {	
					
		if(hasToBeErased(&MBB, oldMDTNodes) && !hasBeenErased(&MBB, erasedMDTNodes)){
//errs() << "ERASING: " << MBB.getParent()->getName() << "  " << MBB.getName() << "\n";
			MDT.eraseNode(&MBB);
//errs() << "ERASED: " << MBB.getParent()->getName() << "  " << MBB.getName() << "\n";
/*			if(hasMDTNode(&MBB, notMDTNodes))
				errs() << MBB.getName() << "  not in the old list\n";
			else
				errs() << MBB.getName() << "  still in the old list\n";
*/			erasedMDTNodes.push_back(&MBB);
		}
	}
	
	errs() << "Iteration " << nestLevel << "\n";
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
