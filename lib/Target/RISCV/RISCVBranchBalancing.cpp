#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "RISCVSubtarget.h"
#include "RISCV.h"


using namespace llvm;


#define RISCV_BRANCH_BALANCER_NAME "RISCV Branch Balancer"

static cl::opt<bool>DisplayMode("riscv-cfg-balance-display-mode",
								cl::desc("Print the clock cicles of each block to stdout after balancing the cfg"),
								cl::init(false),
								cl::NotHidden);

struct CostFromMBBToLeaf {
	MachineBasicBlock *MBB;
	unsigned int cost;
};

struct costModel {
	unsigned opcode;
	unsigned cost;
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
		
		void displayInfo(MachineFunction& MF, MachineDominatorTree& MDT);
		void balanceBlockSizes(MachineFunction& MF);
		void balanceBranchSizes(MachineFunction& MF, MachineDominatorTree& MDT);
		void balanceBranchCycles(MachineFunction& MF, MachineDominatorTree& MDT);
		void balanceBranchCyclesWithNops(MachineFunction& MF, MachineDominatorTree& MDT);
		
		bool runOnMachineFunction(MachineFunction& MF) override ;
		
	};
		
}


char RISCVBranchBalancer::ID = 0;

INITIALIZE_PASS(RISCVBranchBalancer, "riscv-branch-balancer",
                RISCV_BRANCH_BALANCER_NAME, false, false)
//INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)


void detectInstr(MachineInstr& MI) {
	MCInstrDesc MID = MI.getDesc();
	if(MID.isAdd()) {
//		errs() << MI.getOpcode() << "  add\n";
//		MI.dump();
	} else if(MID.isMoveReg()) {
//		errs() << MI.getOpcode() << "  movereg\n";
//		MI.dump();
//	else if(MID.isTerminator()) {
//		errs() << MI.getOpcode() << "  terminator\n";
//		MI.dump();
//	} if(MID.isBranch()) {
//		errs() << MI.getOpcode() << "  branch\n";
//		MI.dump();
	} else if(MID.isIndirectBranch()) {
//		errs() << MI.getOpcode() << "  indirect branch\n";
//		MI.dump();
	} else if(MID.isConditionalBranch()) {
//		errs() << MI.getOpcode() << "  conditional branch\n";
//		MI.dump();
	} else if(MID.isUnconditionalBranch()) {
//		errs() << MI.getOpcode() << "  unconditional branch\n";
//		MI.dump();
	} else if(MID.isCompare()) {
//		errs() << MI.getOpcode() << "  compare\n";
//		MI.dump();
	} else if(MID.isMoveImmediate()) {
//		errs() << MI.getOpcode() << "  move immediate\n";
//		MI.dump();
	} else if(MID.isBitcast()) {
//		errs() << MI.getOpcode() << "  bitcast\n";
//		MI.dump();
	} else if(MID.isSelect()) {
//		errs() << MI.getOpcode() << "  select\n";
//		MI.dump();
	} else if(MID.mayLoad()) {
//		errs() << MI.getOpcode() << "  may load\n";
//		MI.dump();
	} else if(MID.mayStore()) {
//		errs() << MI.getOpcode() << "  may store\n";
//		MI.dump();
	} else if(MID.isCall()) {
	} else if(MID.isReturn()) {
//		errs() << MI.getOpcode() << "  may store\n";
//		MI.dump();
	} else {
//		errs() << MI.getOpcode() << "  unknown\n";
//		MI.dump();
errs() << MI.getOpcode() << "  ";
MI.dump();
	}
}


/*---------fake cycle cost functions---------*/


/*unsigned instrCyclesCount(MachineInstr& MI) {
	return (unsigned)((MI.getOpcode()/10)%14)+1;
}*/


/*if you have a custom cost model for your architecture
  you should inserti it here. Load and Store instructions
  might need a proper model that takes into account the 
  delays caused by memory accesses.
  The else case covers unspecified instructions and
  arithmetic operators. If you have a model that
  corresponds to an unspecified instructions you should
  add an else if voice to the BOTTOM of the list BEFORE 
  the else case.
  */
/*
unsigned instrCyclesCount(MachineInstr& MI) {
	MCInstrDesc MID = MI.getDesc();
	if(MID.isAdd()) {
		return 1;
	} else if(MID.isMoveReg()) {
		return 2;
	} else if(MID.isIndirectBranch()) {
		return 5;
	} else if(MID.isConditionalBranch()) {
		return 6;
	} else if(MID.isUnconditionalBranch()) {
		return 4;
	} else if(MID.isCompare()) {
		return 2;
	} else if(MID.isMoveImmediate()) {
		return 4;
	} else if(MID.isBitcast()) {
		return 4;
	} else if(MID.isSelect()) {
		return 5;
	} else if(MID.mayLoad()) {
		return 10;
	} else if(MID.mayStore()) {
		return 15;
	} else if(MID.isCall()) {
		return 50;
	} else if(MID.isReturn()) {
		return 4;
	} else {
		return (unsigned)((MI.getOpcode()/10)%14)+1;
	}
}
*/

unsigned instrCyclesCount(MachineInstr& MI) {
	switch(MI.getOpcode()) {
		case RISCV::ADDI:
			if(MI.getOperand(0).getReg() == RISCV::X0 &&
			   MI.getOperand(1).getReg() == RISCV::X0 &&
			   MI.getOperand(2).getImm() == 0)
			   	return 1;
		case RISCV::ADD:
		case RISCV::SUB:
			return 3;
		case RISCV::AND:
		case RISCV::ANDI:
		case RISCV::OR:
		case RISCV::ORI:
		case RISCV::XOR:
		case RISCV::XORI:
			return 2;
		case RISCV::LW:
		case RISCV::LH:
		case RISCV::LB:
		case RISCV::LBU:
		case RISCV::LUI:
			return 5;
		case RISCV::SW:
		case RISCV::SH:
		case RISCV::SB:
			return 4;
		case RISCV::BEQ:
		case RISCV::BNE:
			return 4;
		case RISCV::PseudoBR:
			return 3;
		case RISCV::JAL:	//call instructions
			return 10;
		case RISCV::PseudoCALL:
			return 20;
		case RISCV::PseudoRET:
			return 2;
		default:
			return MI.getOpcode()%11;
	}
}



unsigned blockCyclesCount(MachineBasicBlock& MBB) {
	unsigned cycles = 0;
	for(MachineInstr &MI : MBB) {
	//	cycles += ((MI.getOpcode()/10)%14)+1;
		cycles += instrCyclesCount(MI);
	}
	return cycles;
}

/*-------------------------------------------*/


std::vector<MachineInstr *> getIntermInstr(MachineBasicBlock *DestMBB, MachineBasicBlock *SourceMBB) {
	std::vector<MachineInstr *> instrList;
	MachineBasicBlock *MidMBB = SourceMBB;
	
	for(; MidMBB != DestMBB && MidMBB->succ_size() != 0;) {
		for(MachineInstr& MI : *MidMBB) {
			instrList.push_back(&MI);
		}
		MidMBB = *MidMBB->succ_begin();
	}
	
	return instrList;
} 


unsigned int computeCyclesToLeaf(MachineBasicBlock *DestMBB, MachineBasicBlock *SourceMBB) {
	MachineBasicBlock *MidMBB = SourceMBB;
	unsigned int sum = 0;
	
	for(; MidMBB != DestMBB && MidMBB->succ_size() != 0;) {
		sum += blockCyclesCount(*MidMBB);
		MidMBB = *MidMBB->succ_begin();
	}
//	errs() << SourceMBB->getParent()->getName() << "FROM  : " << SourceMBB->getName() << "  TO: " << DestMBB->getName() <<"  COST: " << sum << "\n";
	return sum;
}

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



void RISCVBranchBalancer::balanceBranchCycles(MachineFunction& MF, MachineDominatorTree& MDT) {
	
	std::vector<struct CostFromMBBToLeaf> CostsFromMBBToLeaves;
	std::vector<MachineBasicBlock *> oldMDTNodes;
	std::vector<MachineBasicBlock *> notMDTNodes;
	std::vector<MachineBasicBlock *> erasedMDTNodes;
	const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
	
	unsigned int cyclesToLeaf = 0;
	unsigned int maxCycles = 0;
	unsigned int cyclesCount = 0;
	unsigned int nestLevel = 0;
	
	for(MachineBasicBlock& MBB : MF) {
		if(MDT.getNode(&MBB)->getLevel() > nestLevel)
			nestLevel = MDT.getNode(&MBB)->getLevel();
	}
	
	for(; nestLevel > 0; nestLevel--) {
	
		for(MachineBasicBlock& MBB : MF) {
//	errs() << "Initial check:  " << MBB.getParent()->getName() << "  &MBB content: " << &MBB << "  " << MBB.getName() <<"\n";
			cyclesToLeaf = maxCycles = cyclesCount = 0;
			
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
						cyclesToLeaf = maxCycles = cyclesCount = 0;

						struct CostFromMBBToLeaf NewCostFromMBBToLeaf;
		
						MachineBasicBlock *BalanceBase = firstNotDominatedSuccessor(&MBB, MDT);
						MachineBasicBlock *maxSucc = &MBB;
						
						for(MachineBasicBlock *succ : pred->successors()){
							if(isReachableFrom(BalanceBase, succ)) {
								cyclesToLeaf = computeCyclesToLeaf(BalanceBase, succ);
								if(cyclesToLeaf > maxCycles) {
									maxCycles = cyclesToLeaf;
									maxSucc = succ;
								}
							}
						}
						
						std::vector<MachineInstr *> InstrOnPathA = getIntermInstr(BalanceBase, &MBB);
						std::vector<MachineInstr *> InstrOnPathB = getIntermInstr(BalanceBase, maxSucc);
						
						std::vector<MachineInstr *> InstrOnPath;
						InstrOnPath.reserve(InstrOnPathA.size() + InstrOnPathB.size());
						InstrOnPath.insert(InstrOnPath.end(), InstrOnPathA.begin(), InstrOnPathA.end());
						InstrOnPath.insert(InstrOnPath.end(), InstrOnPathB.begin(), InstrOnPathB.end());
						
					/*	errs() << MF.getName() << "\n";
						errs() << "form " << MBB.getName() << " & " << maxSucc->getName() << "  to  " << BalanceBase->getName() << "\n";
						
						for(MachineInstr *MMI : InstrOnPath) {
							errs() << "check vector:  " << MMI << "  " << MMI->getOpcode() << "  " << TII.getName(MMI->getOpcode()) << "\n";
						}
					*/	
						
						/*Searching for direct paths between the predecessor 
						  and the successor of the current block, if any, there's 
						  a chance to skip the current block according to some 
						  conditions. Such chance must be eliminated by interleaving 
						  a dummy block equal in size*/
						for(MachineBasicBlock *succ : pred->successors()){
							if(succ != &MBB && isReachableFrom(succ, &MBB)) {
							/*	BasicBlock *BB = MBB.getBasicBlock();
								BB->setName(Twine(BB->getName()).concat(Twine(".copy")));
								MachineBasicBlock *dummyMBB = MF.CreateMachineBasicBlock(BB);
							*/	
								MachineBasicBlock *dummyMBB = MF.CreateMachineBasicBlock(MBB.getBasicBlock());
	//	errs() << "NEED DUMMY  " << MBB.getParent()->getName() << "  " << MBB.getName() << "  succ:  " << succ->getName() << "\n";
						//dummyMBB->addSuccessor(succ);
						//pred->removeSuccessor(succ);
						//pred->addSuccessor(dummyMBB);
						//dummyMB->transferSuccessorsAndUpdatePHIs(pred);
								
								MF.insert(++pred->getIterator(), dummyMBB);
								MachineInstr& MI = pred->back();
	//		errs() << "here  maxCycles:  " << maxCycles << "  dummy size:  " << blockCyclesCount(*dummyMBB) << "\n";			
						/*The cost of previously processed nested nodes is considered too (costToLeaf)*/
								
								/*Setting the links with the new block*/ 
								/*FIXME: need to copy the exact branch 
								type from the original block. Is it ever 
								different from a PseudoBR?*/
								BuildMI(*dummyMBB, dummyMBB->end(), MI.getDebugLoc(), TII.get(RISCV::PseudoBR))
									.addMBB(succ);
	//				errs() << "  dummy size:  " << blockCyclesCount(*dummyMBB) << "\n";			
								for(cyclesCount = blockCyclesCount(*dummyMBB); cyclesCount < maxCycles;) {
				//					errs() << "  dummy size:  " << blockCyclesCount(*dummyMBB) << "\n";
				
									MachineInstrBuilder MIB = BuildMI(*dummyMBB, dummyMBB->front(), 
																	  MI.getDebugLoc(), TII.get(RISCV::ADDI))
										.addReg(RISCV::X0)
										.addReg(RISCV::X0)
										.addImm(0);			//RISC-V NOOP OPERATION: ADDI $X0, $X0, 0
									
									cyclesCount += instrCyclesCount(*(MIB.getInstr()));
									
		//							errs() << "dummy cycle incr: " << instrCyclesCount(*(MIB.getInstr())) << "  max: " << maxCycles << "\n";
								}
		//			errs() << "here  maxCycles:  " << maxCycles << "  dummy size:  " << blockCyclesCount(*dummyMBB) << "\n";			
								//pred->replaceSuccessor(succ, dummyMBB);
								pred->removeSuccessor(succ);
								pred->addSuccessor(dummyMBB);
								dummyMBB->addSuccessor(succ);
										
								for(MachineInstr &MTerm : pred->terminators()){
									if(MTerm.isBranch()){
										for(MachineOperand& MOP : MTerm.operands()){
											if(MOP.isMBB()){
												if(succ == MOP.getMBB())
													MOP.setMBB(dummyMBB);
											}
										}
									}
								}
								
								//dummyMBB->setName(Twine(dummyMBB->getName()).concat(Twine(".copy")));
								MDT.addNewBlock(dummyMBB, pred);
								//notMDTNodes.push_back(dummyMBB);
							} //need a dummy block?
						} //check brothers
				
						/*Balancing all the brothers, also the dummy ones*/
						for(MachineBasicBlock *succ : pred->successors()){
							if(isReachableFrom(BalanceBase, succ)) {
								MachineInstr& MI = succ->instr_front();
					
								for(unsigned int cyclesCount = computeCyclesToLeaf(BalanceBase, succ); 
												 cyclesCount < maxCycles;)
								{
									MachineInstrBuilder MIB = BuildMI(*succ, MI, MI.getDebugLoc(), TII.get(RISCV::ADDI))		
										.addReg(RISCV::X0)
										.addReg(RISCV::X0)
										.addImm(0);			//RISC-V NOOP OPERATION: ADDI $X0, $X0, 0
									cyclesCount += instrCyclesCount(*(MIB.getInstr()));
								}
							}
						}
				
						/*Keeping record of the costs of the leaves*/
						NewCostFromMBBToLeaf.MBB = pred;
						NewCostFromMBBToLeaf.cost = maxCycles;
						CostsFromMBBToLeaves.push_back(NewCostFromMBBToLeaf);

					} //for all the predecessors
				} //check nest level
			} //hasMDTNode
		} //MBB:MF
	} //nest level
}



void RISCVBranchBalancer::balanceBranchCyclesWithNops(MachineFunction& MF, MachineDominatorTree& MDT) {
	
	std::vector<struct CostFromMBBToLeaf> CostsFromMBBToLeaves;
	std::vector<MachineBasicBlock *> oldMDTNodes;
	std::vector<MachineBasicBlock *> notMDTNodes;
	std::vector<MachineBasicBlock *> erasedMDTNodes;
	const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
	
	unsigned int cyclesToLeaf = 0;
	unsigned int maxCycles = 0;
	unsigned int cyclesCount = 0;
	unsigned int nestLevel = 0;
	
	for(MachineBasicBlock& MBB : MF) {
		if(MDT.getNode(&MBB)->getLevel() > nestLevel)
			nestLevel = MDT.getNode(&MBB)->getLevel();
	}
	
	for(; nestLevel > 0; nestLevel--) {
	
		for(MachineBasicBlock& MBB : MF) {
//	errs() << "Initial check:  " << MBB.getParent()->getName() << "  &MBB content: " << &MBB << "  " << MBB.getName() <<"\n";
			cyclesToLeaf = maxCycles = cyclesCount = 0;
			
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
						cyclesToLeaf = maxCycles = cyclesCount = 0;

						struct CostFromMBBToLeaf NewCostFromMBBToLeaf;
		
						MachineBasicBlock *BalanceBase = firstNotDominatedSuccessor(&MBB, MDT);
						
						for(MachineBasicBlock *succ : pred->successors()){
							if(isReachableFrom(BalanceBase, succ)) {
								cyclesToLeaf = computeCyclesToLeaf(BalanceBase, succ);
								if(cyclesToLeaf > maxCycles)
									maxCycles = cyclesToLeaf; 
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
	//	errs() << "NEED DUMMY  " << MBB.getParent()->getName() << "  " << MBB.getName() << "  succ:  " << succ->getName() << "\n";
						//dummyMBB->addSuccessor(succ);
						//pred->removeSuccessor(succ);
						//pred->addSuccessor(dummyMBB);
						//dummyMB->transferSuccessorsAndUpdatePHIs(pred);
	
								MF.insert(++pred->getIterator(), dummyMBB);
								MachineInstr& MI = pred->back();
	//		errs() << "here  maxCycles:  " << maxCycles << "  dummy size:  " << blockCyclesCount(*dummyMBB) << "\n";			
						/*The cost of previously processed nested nodes is considered too (costToLeaf)*/
								
								/*Setting the links with the new block*/ 
								/*FIXME: need to copy the exact branch 
								type from the original block. Is it ever 
								different from a PseudoBR?*/
								BuildMI(*dummyMBB, dummyMBB->end(), MI.getDebugLoc(), TII.get(RISCV::PseudoBR))
								.addMBB(succ);
	//				errs() << "  dummy size:  " << blockCyclesCount(*dummyMBB) << "\n";			
								for(cyclesCount = blockCyclesCount(*dummyMBB); cyclesCount < maxCycles;) {
				//					errs() << "  dummy size:  " << blockCyclesCount(*dummyMBB) << "\n";
									MachineInstrBuilder MIB = BuildMI(*dummyMBB, dummyMBB->front(), MI.getDebugLoc(), TII.get(RISCV::ADDI))
									.addReg(RISCV::X0)
									.addReg(RISCV::X0)
									.addImm(0);			//RISC-V NOOP OPERATION: ADDI $X0, $X0, 0
									cyclesCount += instrCyclesCount(*(MIB.getInstr()));
									
		//							errs() << "dummy cycle incr: " << instrCyclesCount(*(MIB.getInstr())) << "  max: " << maxCycles << "\n";
								}
		//			errs() << "here  maxCycles:  " << maxCycles << "  dummy size:  " << blockCyclesCount(*dummyMBB) << "\n";			
								//pred->replaceSuccessor(succ, dummyMBB);
								pred->removeSuccessor(succ);
								pred->addSuccessor(dummyMBB);
								dummyMBB->addSuccessor(succ);
										
								for(MachineInstr &MTerm : pred->terminators()){
									if(MTerm.isBranch()){
										for(MachineOperand& MOP : MTerm.operands()){
											if(MOP.isMBB()){
												if(succ == MOP.getMBB())
													MOP.setMBB(dummyMBB);
											}
										}
									}
								}
								MDT.addNewBlock(dummyMBB, pred);
								//notMDTNodes.push_back(dummyMBB);
							} //need a dummy block?
						} //check brothers
				
						/*Balancing all the brothers, also the dummy ones*/
						for(MachineBasicBlock *succ : pred->successors()){
							if(isReachableFrom(BalanceBase, succ)) {
								MachineInstr& MI = succ->instr_front();
					
								for(unsigned int cyclesCount = computeCyclesToLeaf(BalanceBase, succ); 
												 cyclesCount < maxCycles;)
								{
									MachineInstrBuilder MIB = BuildMI(*succ, MI, MI.getDebugLoc(), TII.get(RISCV::ADDI))		
										.addReg(RISCV::X0)
										.addReg(RISCV::X0)
										.addImm(0);			//RISC-V NOOP OPERATION: ADDI $X0, $X0, 0
									cyclesCount += instrCyclesCount(*(MIB.getInstr()));
								}
							}
						}
				
						/*Keeping record of the costs of the leaves*/
						NewCostFromMBBToLeaf.MBB = pred;
						NewCostFromMBBToLeaf.cost = maxCycles;
						CostsFromMBBToLeaves.push_back(NewCostFromMBBToLeaf);

					} //for all the predecessors
				} //check nest level
			} //hasMDTNode
		} //MBB:MF
	} //nest level
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
								BuildMI(*dummyMBB, dummyMBB->end(), MI.getDebugLoc(), TII.get(RISCV::PseudoBR))
								.addMBB(succ);
								
								//pred->replaceSuccessor(succ, dummyMBB);
								pred->removeSuccessor(succ);
								pred->addSuccessor(dummyMBB);
								dummyMBB->addSuccessor(succ);
										
								for(MachineInstr &MTerm : pred->terminators()){
									if(MTerm.isBranch()){
										for(MachineOperand& MOP : MTerm.operands()){
											if(MOP.isMBB()){
												if(succ == MOP.getMBB())
													MOP.setMBB(dummyMBB);
											}
										}
									}
								}
								MDT.addNewBlock(dummyMBB, pred);
								//notMDTNodes.push_back(dummyMBB);
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


void RISCVBranchBalancer::displayInfo(MachineFunction& MF, MachineDominatorTree& MDT) {
	//const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
	unsigned nestLevel = 0;
	/*const InstrItineraryData *ItinData = MF.getSubtarget().getInstrItineraryData();
	const InstrStage *IST;
	*/
	//unsigned int cycles = 0;
	//const MCInstrDesc & 	getDesc ();
	//MI.getDesc().getSchedClass();
	errs() << MF.getName() << "\n";
	for(MachineBasicBlock& MBB : MF) {
		errs() << "  ";
		nestLevel = MDT.getNode(&MBB)->getLevel();
		for(unsigned i=0; i<nestLevel; i++){
			errs() << "  ";
		}
		errs() << MBB.getName() << "  cycles: " << blockCyclesCount(MBB) << "\n";
		//errs() << MBB.getName() << "  cycles: " << MBB.size() << "\n";
		//for(MachineInstr& MI : MBB) {
	//		detectInstr(MI);
	//		errs() << "    " << MI.getOpcode() << "  " << ((MI.getOpcode()/10)%14)+1 << "  ";
	//		MI.dump();
		//	errs() << "      " << MI.getOpcode() << ", cycles: " << "\n";
		//	errs() << "      " << MI.getOpcode() << ", " << TII->getName(MI.getOpcode()) << "  operands  " << MI.getNumOperands() << "  cycles:  " << instrCyclesCount(MI) << "\n";
		//}
	}
}


bool RISCVBranchBalancer::runOnMachineFunction(MachineFunction& MF) {
			
			MDT = &getAnalysis<MachineDominatorTree>();
			
			std::vector<costModel> costVector;
			
			//balanceBlockSizes(MF);
			
		//	balanceBranchSizes(MF, *MDT);
		//	balanceBranchCyclesWithNops(MF, *MDT);
			balanceBranchCycles(MF, *MDT);
			if(DisplayMode)
				displayInfo(MF, *MDT);
			
			return true;
}




FunctionPass *llvm::createRISCVBranchBalancerPass() {
  return new RISCVBranchBalancer();
}
