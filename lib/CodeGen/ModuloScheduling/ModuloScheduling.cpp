//===-- ModuloScheduling.cpp - ModuloScheduling  ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// 
//  This ModuloScheduling pass is based on the Swing Modulo Scheduling 
//  algorithm. 
// 
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "ModuloSched"

#include "ModuloScheduling.h"
#include "llvm/Instructions.h"
#include "llvm/Function.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/CFG.h"
#include "llvm/Target/TargetSchedInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/ADT/StringExtras.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>
#include "../../Target/SparcV9/MachineCodeForInstruction.h"
#include "../../Target/SparcV9/SparcV9TmpInstr.h"
#include "../../Target/SparcV9/SparcV9Internals.h"
#include "../../Target/SparcV9/SparcV9RegisterInfo.h"
using namespace llvm;

/// Create ModuloSchedulingPass
///
FunctionPass *llvm::createModuloSchedulingPass(TargetMachine & targ) {
  DEBUG(std::cerr << "Created ModuloSchedulingPass\n");
  return new ModuloSchedulingPass(targ); 
}


//Graph Traits for printing out the dependence graph
template<typename GraphType>
static void WriteGraphToFile(std::ostream &O, const std::string &GraphName,
                             const GraphType &GT) {
  std::string Filename = GraphName + ".dot";
  O << "Writing '" << Filename << "'...";
  std::ofstream F(Filename.c_str());
  
  if (F.good())
    WriteGraph(F, GT);
  else
    O << "  error opening file for writing!";
  O << "\n";
};

//Graph Traits for printing out the dependence graph
namespace llvm {

  template<>
  struct DOTGraphTraits<MSchedGraph*> : public DefaultDOTGraphTraits {
    static std::string getGraphName(MSchedGraph *F) {
      return "Dependence Graph";
    }
    
    static std::string getNodeLabel(MSchedGraphNode *Node, MSchedGraph *Graph) {
      if (Node->getInst()) {
	std::stringstream ss;
	ss << *(Node->getInst());
	return ss.str(); //((MachineInstr*)Node->getInst());
      }
      else
	return "No Inst";
    }
    static std::string getEdgeSourceLabel(MSchedGraphNode *Node,
					  MSchedGraphNode::succ_iterator I) {
      //Label each edge with the type of dependence
      std::string edgelabel = "";
      switch (I.getEdge().getDepOrderType()) {
	
      case MSchedGraphEdge::TrueDep: 
	edgelabel = "True";
	break;
    
      case MSchedGraphEdge::AntiDep: 
	edgelabel =  "Anti";
	break;
	
      case MSchedGraphEdge::OutputDep: 
	edgelabel = "Output";
	break;
	
      default:
	edgelabel = "Unknown";
	break;
      }

      //FIXME
      int iteDiff = I.getEdge().getIteDiff();
      std::string intStr = "(IteDiff: ";
      intStr += itostr(iteDiff);

      intStr += ")";
      edgelabel += intStr;

      return edgelabel;
    }
  };
}

/// ModuloScheduling::runOnFunction - main transformation entry point
/// The Swing Modulo Schedule algorithm has three basic steps:
/// 1) Computation and Analysis of the dependence graph
/// 2) Ordering of the nodes
/// 3) Scheduling
/// 
bool ModuloSchedulingPass::runOnFunction(Function &F) {
  
  bool Changed = false;
  
  DEBUG(std::cerr << "Creating ModuloSchedGraph for each valid BasicBlock in" + F.getName() + "\n");
  
  //Get MachineFunction
  MachineFunction &MF = MachineFunction::get(&F);
  
  //Print out machine function
  DEBUG(MF.print(std::cerr));

  //Worklist
  std::vector<MachineBasicBlock*> Worklist;
  
  //Iterate over BasicBlocks and put them into our worklist if they are valid
  for (MachineFunction::iterator BI = MF.begin(); BI != MF.end(); ++BI)
    if(MachineBBisValid(BI)) 
      Worklist.push_back(&*BI);
  

  //Iterate over the worklist and perform scheduling
  for(std::vector<MachineBasicBlock*>::iterator BI = Worklist.begin(),  
	BE = Worklist.end(); BI != BE; ++BI) {
    
    MSchedGraph *MSG = new MSchedGraph(*BI, target);
    
    //Write Graph out to file
    DEBUG(WriteGraphToFile(std::cerr, F.getName(), MSG));
    
    //Print out BB for debugging
    DEBUG((*BI)->print(std::cerr));
    
    //Calculate Resource II
    int ResMII = calculateResMII(*BI);
    
    //Calculate Recurrence II
    int RecMII = calculateRecMII(MSG, ResMII);
    
    //Our starting initiation interval is the maximum of RecMII and ResMII
    II = std::max(RecMII, ResMII);
    
    //Print out II, RecMII, and ResMII
    DEBUG(std::cerr << "II starts out as " << II << " ( RecMII=" << RecMII << "and ResMII=" << ResMII << "\n");
    
    //Calculate Node Properties
    calculateNodeAttributes(MSG, ResMII);
    
    //Dump node properties if in debug mode
    DEBUG(for(std::map<MSchedGraphNode*, MSNodeAttributes>::iterator I =  nodeToAttributesMap.begin(), 
		E = nodeToAttributesMap.end(); I !=E; ++I) {
      std::cerr << "Node: " << *(I->first) << " ASAP: " << I->second.ASAP << " ALAP: " 
		<< I->second.ALAP << " MOB: " << I->second.MOB << " Depth: " << I->second.depth 
		<< " Height: " << I->second.height << "\n";
    });
    
    //Put nodes in order to schedule them
    computePartialOrder();
    
    //Dump out partial order
    DEBUG(for(std::vector<std::vector<MSchedGraphNode*> >::iterator I = partialOrder.begin(), 
		E = partialOrder.end(); I !=E; ++I) {
      std::cerr << "Start set in PO\n";
      for(std::vector<MSchedGraphNode*>::iterator J = I->begin(), JE = I->end(); J != JE; ++J)
	std::cerr << "PO:" << **J << "\n";
    });
    
    //Place nodes in final order
    orderNodes();
    
    //Dump out order of nodes
    DEBUG(for(std::vector<MSchedGraphNode*>::iterator I = FinalNodeOrder.begin(), E = FinalNodeOrder.end(); I != E; ++I) {
	  std::cerr << "FO:" << **I << "\n";
    });
    
    //Finally schedule nodes
    computeSchedule();
    
    //Print out final schedule
    DEBUG(schedule.print(std::cerr));
    

    //Final scheduling step is to reconstruct the loop
    reconstructLoop(*BI);
    
    //Print out new loop
    
    
    //Clear out our maps for the next basic block that is processed
    nodeToAttributesMap.clear();
    partialOrder.clear();
    recurrenceList.clear();
    FinalNodeOrder.clear();
    schedule.clear();
    
    //Clean up. Nuke old MachineBB and llvmBB
    //BasicBlock *llvmBB = (BasicBlock*) (*BI)->getBasicBlock();
    //Function *parent = (Function*) llvmBB->getParent();
    //Should't std::find work??
    //parent->getBasicBlockList().erase(std::find(parent->getBasicBlockList().begin(), parent->getBasicBlockList().end(), *llvmBB));
    //parent->getBasicBlockList().erase(llvmBB);
    
    //delete(llvmBB);
    //delete(*BI);
  }
  
 
  return Changed;
}


/// This function checks if a Machine Basic Block is valid for modulo
/// scheduling. This means that it has no control flow (if/else or
/// calls) in the block.  Currently ModuloScheduling only works on
/// single basic block loops.
bool ModuloSchedulingPass::MachineBBisValid(const MachineBasicBlock *BI) {

  bool isLoop = false;
  
  //Check first if its a valid loop
  for(succ_const_iterator I = succ_begin(BI->getBasicBlock()), 
	E = succ_end(BI->getBasicBlock()); I != E; ++I) {
    if (*I == BI->getBasicBlock())    // has single block loop
      isLoop = true;
  }
  
  if(!isLoop)
    return false;
    
  //Get Target machine instruction info
  const TargetInstrInfo *TMI = target.getInstrInfo();
    
  //Check each instruction and look for calls
  for(MachineBasicBlock::const_iterator I = BI->begin(), E = BI->end(); I != E; ++I) {
    //Get opcode to check instruction type
    MachineOpCode OC = I->getOpcode();
    if(TMI->isCall(OC))
      return false;
 
  }
  return true;

}

//ResMII is calculated by determining the usage count for each resource
//and using the maximum.
//FIXME: In future there should be a way to get alternative resources
//for each instruction
int ModuloSchedulingPass::calculateResMII(const MachineBasicBlock *BI) {
  
  const TargetInstrInfo *mii = target.getInstrInfo();
  const TargetSchedInfo *msi = target.getSchedInfo();

  int ResMII = 0;
  
  //Map to keep track of usage count of each resource
  std::map<unsigned, unsigned> resourceUsageCount;

  for(MachineBasicBlock::const_iterator I = BI->begin(), E = BI->end(); I != E; ++I) {

    //Get resource usage for this instruction
    InstrRUsage rUsage = msi->getInstrRUsage(I->getOpcode());
    std::vector<std::vector<resourceId_t> > resources = rUsage.resourcesByCycle;

    //Loop over resources in each cycle and increments their usage count
    for(unsigned i=0; i < resources.size(); ++i)
      for(unsigned j=0; j < resources[i].size(); ++j) {
	if( resourceUsageCount.find(resources[i][j]) == resourceUsageCount.end()) {
	  resourceUsageCount[resources[i][j]] = 1;
	}
	else {
	  resourceUsageCount[resources[i][j]] =  resourceUsageCount[resources[i][j]] + 1;
	}
      }
  }

  //Find maximum usage count
  
  //Get max number of instructions that can be issued at once. (FIXME)
  int issueSlots = msi->maxNumIssueTotal;

  for(std::map<unsigned,unsigned>::iterator RB = resourceUsageCount.begin(), RE = resourceUsageCount.end(); RB != RE; ++RB) {
    
    //Get the total number of the resources in our cpu
    int resourceNum = CPUResource::getCPUResource(RB->first)->maxNumUsers;
    
    //Get total usage count for this resources
    unsigned usageCount = RB->second;
    
    //Divide the usage count by either the max number we can issue or the number of
    //resources (whichever is its upper bound)
    double finalUsageCount;
    if( resourceNum <= issueSlots)
      finalUsageCount = ceil(1.0 * usageCount / resourceNum);
    else
      finalUsageCount = ceil(1.0 * usageCount / issueSlots);
    
    
    //Only keep track of the max
    ResMII = std::max( (int) finalUsageCount, ResMII);

  }

  return ResMII;

}

/// calculateRecMII - Calculates the value of the highest recurrence
/// By value we mean the total latency
int ModuloSchedulingPass::calculateRecMII(MSchedGraph *graph, int MII) {
  std::vector<MSchedGraphNode*> vNodes;
  //Loop over all nodes in the graph
  for(MSchedGraph::iterator I = graph->begin(), E = graph->end(); I != E; ++I) {
    findAllReccurrences(I->second, vNodes, MII);
    vNodes.clear();
  }

  int RecMII = 0;
  
  for(std::set<std::pair<int, std::vector<MSchedGraphNode*> > >::iterator I = recurrenceList.begin(), E=recurrenceList.end(); I !=E; ++I) {
    DEBUG(for(std::vector<MSchedGraphNode*>::const_iterator N = I->second.begin(), NE = I->second.end(); N != NE; ++N) {
      std::cerr << **N << "\n";
    });
    RecMII = std::max(RecMII, I->first);
  }
    
  return MII;
}

/// calculateNodeAttributes - The following properties are calculated for
/// each node in the dependence graph: ASAP, ALAP, Depth, Height, and
/// MOB.
void ModuloSchedulingPass::calculateNodeAttributes(MSchedGraph *graph, int MII) {

  //Loop over the nodes and add them to the map
  for(MSchedGraph::iterator I = graph->begin(), E = graph->end(); I != E; ++I) {
    //Assert if its already in the map
    assert(nodeToAttributesMap.find(I->second) == nodeToAttributesMap.end() && "Node attributes are already in the map");
    
    //Put into the map with default attribute values
    nodeToAttributesMap[I->second] = MSNodeAttributes();
  }

  //Create set to deal with reccurrences
  std::set<MSchedGraphNode*> visitedNodes;
  
  //Now Loop over map and calculate the node attributes
  for(std::map<MSchedGraphNode*, MSNodeAttributes>::iterator I = nodeToAttributesMap.begin(), E = nodeToAttributesMap.end(); I != E; ++I) {
    calculateASAP(I->first, MII, (MSchedGraphNode*) 0);
    visitedNodes.clear();
  }
  
  int maxASAP = findMaxASAP();
  //Calculate ALAP which depends on ASAP being totally calculated
  for(std::map<MSchedGraphNode*, MSNodeAttributes>::iterator I = nodeToAttributesMap.begin(), E = nodeToAttributesMap.end(); I != E; ++I) {
    calculateALAP(I->first, MII, maxASAP, (MSchedGraphNode*) 0);
    visitedNodes.clear();
  }

  //Calculate MOB which depends on ASAP being totally calculated, also do depth and height
  for(std::map<MSchedGraphNode*, MSNodeAttributes>::iterator I = nodeToAttributesMap.begin(), E = nodeToAttributesMap.end(); I != E; ++I) {
    (I->second).MOB = std::max(0,(I->second).ALAP - (I->second).ASAP);
   
    DEBUG(std::cerr << "MOB: " << (I->second).MOB << " (" << *(I->first) << ")\n");
    calculateDepth(I->first, (MSchedGraphNode*) 0);
    calculateHeight(I->first, (MSchedGraphNode*) 0);
  }


}

/// ignoreEdge - Checks to see if this edge of a recurrence should be ignored or not
bool ModuloSchedulingPass::ignoreEdge(MSchedGraphNode *srcNode, MSchedGraphNode *destNode) {
  if(destNode == 0 || srcNode ==0)
    return false;
  
  bool findEdge = edgesToIgnore.count(std::make_pair(srcNode, destNode->getInEdgeNum(srcNode)));
  
  return findEdge;
}


/// calculateASAP - Calculates the 
int  ModuloSchedulingPass::calculateASAP(MSchedGraphNode *node, int MII, MSchedGraphNode *destNode) {
    
  DEBUG(std::cerr << "Calculating ASAP for " << *node << "\n");

  //Get current node attributes
  MSNodeAttributes &attributes = nodeToAttributesMap.find(node)->second;

  if(attributes.ASAP != -1)
    return attributes.ASAP;
  
  int maxPredValue = 0;
  
  //Iterate over all of the predecessors and find max
  for(MSchedGraphNode::pred_iterator P = node->pred_begin(), E = node->pred_end(); P != E; ++P) {
    
    //Only process if we are not ignoring the edge
    if(!ignoreEdge(*P, node)) {
      int predASAP = -1;
      predASAP = calculateASAP(*P, MII, node);
    
      assert(predASAP != -1 && "ASAP has not been calculated");
      int iteDiff = node->getInEdge(*P).getIteDiff();
      
      int currentPredValue = predASAP + (*P)->getLatency() - (iteDiff * MII);
      DEBUG(std::cerr << "pred ASAP: " << predASAP << ", iteDiff: " << iteDiff << ", PredLatency: " << (*P)->getLatency() << ", Current ASAP pred: " << currentPredValue << "\n");
      maxPredValue = std::max(maxPredValue, currentPredValue);
    }
  }
  
  attributes.ASAP = maxPredValue;

  DEBUG(std::cerr << "ASAP: " << attributes.ASAP << " (" << *node << ")\n");
  
  return maxPredValue;
}


int ModuloSchedulingPass::calculateALAP(MSchedGraphNode *node, int MII, 
					int maxASAP, MSchedGraphNode *srcNode) {
  
  DEBUG(std::cerr << "Calculating ALAP for " << *node << "\n");
  
  MSNodeAttributes &attributes = nodeToAttributesMap.find(node)->second;
 
  if(attributes.ALAP != -1)
    return attributes.ALAP;
 
  if(node->hasSuccessors()) {
    
    //Trying to deal with the issue where the node has successors, but
    //we are ignoring all of the edges to them. So this is my hack for
    //now.. there is probably a more elegant way of doing this (FIXME)
    bool processedOneEdge = false;

    //FIXME, set to something high to start
    int minSuccValue = 9999999;
    
    //Iterate over all of the predecessors and fine max
    for(MSchedGraphNode::succ_iterator P = node->succ_begin(), 
	  E = node->succ_end(); P != E; ++P) {
      
      //Only process if we are not ignoring the edge
      if(!ignoreEdge(node, *P)) {
	processedOneEdge = true;
	int succALAP = -1;
	succALAP = calculateALAP(*P, MII, maxASAP, node);
	
	assert(succALAP != -1 && "Successors ALAP should have been caclulated");
	
	int iteDiff = P.getEdge().getIteDiff();
	
	int currentSuccValue = succALAP - node->getLatency() + iteDiff * MII;
	
	DEBUG(std::cerr << "succ ALAP: " << succALAP << ", iteDiff: " << iteDiff << ", SuccLatency: " << (*P)->getLatency() << ", Current ALAP succ: " << currentSuccValue << "\n");

	minSuccValue = std::min(minSuccValue, currentSuccValue);
      }
    }
    
    if(processedOneEdge)
    	attributes.ALAP = minSuccValue;
    
    else
      attributes.ALAP = maxASAP;
  }
  else
    attributes.ALAP = maxASAP;

  DEBUG(std::cerr << "ALAP: " << attributes.ALAP << " (" << *node << ")\n");

  if(attributes.ALAP < 0)
    attributes.ALAP = 0;

  return attributes.ALAP;
}

int ModuloSchedulingPass::findMaxASAP() {
  int maxASAP = 0;

  for(std::map<MSchedGraphNode*, MSNodeAttributes>::iterator I = nodeToAttributesMap.begin(),
	E = nodeToAttributesMap.end(); I != E; ++I)
    maxASAP = std::max(maxASAP, I->second.ASAP);
  return maxASAP;
}


int ModuloSchedulingPass::calculateHeight(MSchedGraphNode *node,MSchedGraphNode *srcNode) {
  
  MSNodeAttributes &attributes = nodeToAttributesMap.find(node)->second;

  if(attributes.height != -1)
    return attributes.height;

  int maxHeight = 0;
    
  //Iterate over all of the predecessors and find max
  for(MSchedGraphNode::succ_iterator P = node->succ_begin(), 
	E = node->succ_end(); P != E; ++P) {
    
    
    if(!ignoreEdge(node, *P)) {
      int succHeight = calculateHeight(*P, node);

      assert(succHeight != -1 && "Successors Height should have been caclulated");

      int currentHeight = succHeight + node->getLatency();
      maxHeight = std::max(maxHeight, currentHeight);
    }
  }
  attributes.height = maxHeight;
  DEBUG(std::cerr << "Height: " << attributes.height << " (" << *node << ")\n");
  return maxHeight;
}


int ModuloSchedulingPass::calculateDepth(MSchedGraphNode *node, 
					  MSchedGraphNode *destNode) {

  MSNodeAttributes &attributes = nodeToAttributesMap.find(node)->second;

  if(attributes.depth != -1)
    return attributes.depth;

  int maxDepth = 0;
      
  //Iterate over all of the predecessors and fine max
  for(MSchedGraphNode::pred_iterator P = node->pred_begin(), E = node->pred_end(); P != E; ++P) {

    if(!ignoreEdge(*P, node)) {
      int predDepth = -1;
      predDepth = calculateDepth(*P, node);
      
      assert(predDepth != -1 && "Predecessors ASAP should have been caclulated");

      int currentDepth = predDepth + (*P)->getLatency();
      maxDepth = std::max(maxDepth, currentDepth);
    }
  }
  attributes.depth = maxDepth;
  
  DEBUG(std::cerr << "Depth: " << attributes.depth << " (" << *node << "*)\n");
  return maxDepth;
}



void ModuloSchedulingPass::addReccurrence(std::vector<MSchedGraphNode*> &recurrence, int II, MSchedGraphNode *srcBENode, MSchedGraphNode *destBENode) {
  //Check to make sure that this recurrence is unique
  bool same = false;


  //Loop over all recurrences already in our list
  for(std::set<std::pair<int, std::vector<MSchedGraphNode*> > >::iterator R = recurrenceList.begin(), RE = recurrenceList.end(); R != RE; ++R) {
    
    bool all_same = true;
     //First compare size
    if(R->second.size() == recurrence.size()) {
      
      for(std::vector<MSchedGraphNode*>::const_iterator node = R->second.begin(), end = R->second.end(); node != end; ++node) {
	if(find(recurrence.begin(), recurrence.end(), *node) == recurrence.end()) {
	  all_same = all_same && false;
	  break;
	}
	else
	  all_same = all_same && true;
      }
      if(all_same) {
	same = true;
	break;
      }
    }
  }
  
  if(!same) {
    srcBENode = recurrence.back();
    destBENode = recurrence.front();
    
    //FIXME
    if(destBENode->getInEdge(srcBENode).getIteDiff() == 0) {
      //DEBUG(std::cerr << "NOT A BACKEDGE\n");
      //find actual backedge HACK HACK 
      for(unsigned i=0; i< recurrence.size()-1; ++i) {
	if(recurrence[i+1]->getInEdge(recurrence[i]).getIteDiff() == 1) {
	  srcBENode = recurrence[i];
	  destBENode = recurrence[i+1];
	  break;
	}
	  
      }
      
    }
    DEBUG(std::cerr << "Back Edge to Remove: " << *srcBENode << " to " << *destBENode << "\n");
    edgesToIgnore.insert(std::make_pair(srcBENode, destBENode->getInEdgeNum(srcBENode)));
    recurrenceList.insert(std::make_pair(II, recurrence));
  }
  
}

void ModuloSchedulingPass::findAllReccurrences(MSchedGraphNode *node, 
					       std::vector<MSchedGraphNode*> &visitedNodes,
					       int II) {

  if(find(visitedNodes.begin(), visitedNodes.end(), node) != visitedNodes.end()) {
    std::vector<MSchedGraphNode*> recurrence;
    bool first = true;
    int delay = 0;
    int distance = 0;
    int RecMII = II; //Starting value
    MSchedGraphNode *last = node;
    MSchedGraphNode *srcBackEdge = 0;
    MSchedGraphNode *destBackEdge = 0;
    


    for(std::vector<MSchedGraphNode*>::iterator I = visitedNodes.begin(), E = visitedNodes.end();
	I !=E; ++I) {

      if(*I == node) 
	first = false;
      if(first)
	continue;

      delay = delay + (*I)->getLatency();

      if(*I != node) {
	int diff = (*I)->getInEdge(last).getIteDiff();
	distance += diff;
	if(diff > 0) {
	  srcBackEdge = last;
	  destBackEdge = *I;
	}
      }

      recurrence.push_back(*I);
      last = *I;
    }


      
    //Get final distance calc
    distance += node->getInEdge(last).getIteDiff();
   

    //Adjust II until we get close to the inequality delay - II*distance <= 0
    
    int value = delay-(RecMII * distance);
    int lastII = II;
    while(value <= 0) {
      
      lastII = RecMII;
      RecMII--;
      value = delay-(RecMII * distance);
    }
    
    
    DEBUG(std::cerr << "Final II for this recurrence: " << lastII << "\n");
    addReccurrence(recurrence, lastII, srcBackEdge, destBackEdge);
    assert(distance != 0 && "Recurrence distance should not be zero");
    return;
  }

  for(MSchedGraphNode::succ_iterator I = node->succ_begin(), E = node->succ_end(); I != E; ++I) {
    visitedNodes.push_back(node);
    findAllReccurrences(*I, visitedNodes, II);
    visitedNodes.pop_back();
  }
}





void ModuloSchedulingPass::computePartialOrder() {
  
  
  //Loop over all recurrences and add to our partial order
  //be sure to remove nodes that are already in the partial order in
  //a different recurrence and don't add empty recurrences.
  for(std::set<std::pair<int, std::vector<MSchedGraphNode*> > >::reverse_iterator I = recurrenceList.rbegin(), E=recurrenceList.rend(); I !=E; ++I) {
    
    //Add nodes that connect this recurrence to the previous recurrence
    
    //If this is the first recurrence in the partial order, add all predecessors
    for(std::vector<MSchedGraphNode*>::const_iterator N = I->second.begin(), NE = I->second.end(); N != NE; ++N) {

    }


    std::vector<MSchedGraphNode*> new_recurrence;
    //Loop through recurrence and remove any nodes already in the partial order
    for(std::vector<MSchedGraphNode*>::const_iterator N = I->second.begin(), NE = I->second.end(); N != NE; ++N) {
      bool found = false;
      for(std::vector<std::vector<MSchedGraphNode*> >::iterator PO = partialOrder.begin(), PE = partialOrder.end(); PO != PE; ++PO) {
	if(find(PO->begin(), PO->end(), *N) != PO->end())
	  found = true;
      }
      if(!found) {
	new_recurrence.push_back(*N);
	 
	if(partialOrder.size() == 0)
	  //For each predecessors, add it to this recurrence ONLY if it is not already in it
	  for(MSchedGraphNode::pred_iterator P = (*N)->pred_begin(), 
		PE = (*N)->pred_end(); P != PE; ++P) {
	    
	    //Check if we are supposed to ignore this edge or not
	    if(!ignoreEdge(*P, *N))
	      //Check if already in this recurrence
	      if(find(I->second.begin(), I->second.end(), *P) == I->second.end()) {
		//Also need to check if in partial order
		bool predFound = false;
		for(std::vector<std::vector<MSchedGraphNode*> >::iterator PO = partialOrder.begin(), PEND = partialOrder.end(); PO != PEND; ++PO) {
		  if(find(PO->begin(), PO->end(), *P) != PO->end())
		    predFound = true;
		}
		
		if(!predFound)
		  if(find(new_recurrence.begin(), new_recurrence.end(), *P) == new_recurrence.end())
		     new_recurrence.push_back(*P);
		
	      }
	  }
      }
    }

        
    if(new_recurrence.size() > 0)
      partialOrder.push_back(new_recurrence);
  }
  
  //Add any nodes that are not already in the partial order
  std::vector<MSchedGraphNode*> lastNodes;
  for(std::map<MSchedGraphNode*, MSNodeAttributes>::iterator I = nodeToAttributesMap.begin(), E = nodeToAttributesMap.end(); I != E; ++I) {
    bool found = false;
    //Check if its already in our partial order, if not add it to the final vector
    for(std::vector<std::vector<MSchedGraphNode*> >::iterator PO = partialOrder.begin(), PE = partialOrder.end(); PO != PE; ++PO) {
      if(find(PO->begin(), PO->end(), I->first) != PO->end())
	found = true;
    }
    if(!found)
      lastNodes.push_back(I->first);
  }

  if(lastNodes.size() > 0)
    partialOrder.push_back(lastNodes);
  
}


void ModuloSchedulingPass::predIntersect(std::vector<MSchedGraphNode*> &CurrentSet, std::vector<MSchedGraphNode*> &IntersectResult) {
  
  //Sort CurrentSet so we can use lowerbound
  sort(CurrentSet.begin(), CurrentSet.end());
  
  for(unsigned j=0; j < FinalNodeOrder.size(); ++j) {
    for(MSchedGraphNode::pred_iterator P = FinalNodeOrder[j]->pred_begin(), 
	  E = FinalNodeOrder[j]->pred_end(); P != E; ++P) {
   
      //Check if we are supposed to ignore this edge or not
      if(ignoreEdge(*P,FinalNodeOrder[j]))
	continue;
	 
      if(find(CurrentSet.begin(), 
		     CurrentSet.end(), *P) != CurrentSet.end())
	if(find(FinalNodeOrder.begin(), FinalNodeOrder.end(), *P) == FinalNodeOrder.end())
	  IntersectResult.push_back(*P);
    }
  } 
}

void ModuloSchedulingPass::succIntersect(std::vector<MSchedGraphNode*> &CurrentSet, std::vector<MSchedGraphNode*> &IntersectResult) {

  //Sort CurrentSet so we can use lowerbound
  sort(CurrentSet.begin(), CurrentSet.end());
  
  for(unsigned j=0; j < FinalNodeOrder.size(); ++j) {
    for(MSchedGraphNode::succ_iterator P = FinalNodeOrder[j]->succ_begin(), 
	  E = FinalNodeOrder[j]->succ_end(); P != E; ++P) {

      //Check if we are supposed to ignore this edge or not
      if(ignoreEdge(FinalNodeOrder[j],*P))
	continue;

      if(find(CurrentSet.begin(), 
		     CurrentSet.end(), *P) != CurrentSet.end())
	if(find(FinalNodeOrder.begin(), FinalNodeOrder.end(), *P) == FinalNodeOrder.end())
	  IntersectResult.push_back(*P);
    }
  }
}

void dumpIntersection(std::vector<MSchedGraphNode*> &IntersectCurrent) {
  std::cerr << "Intersection (";
  for(std::vector<MSchedGraphNode*>::iterator I = IntersectCurrent.begin(), E = IntersectCurrent.end(); I != E; ++I)
    std::cerr << **I << ", ";
  std::cerr << ")\n";
}



void ModuloSchedulingPass::orderNodes() {
  
  int BOTTOM_UP = 0;
  int TOP_DOWN = 1;

  //Set default order
  int order = BOTTOM_UP;


  //Loop over all the sets and place them in the final node order
  for(std::vector<std::vector<MSchedGraphNode*> >::iterator CurrentSet = partialOrder.begin(), E= partialOrder.end(); CurrentSet != E; ++CurrentSet) {

    DEBUG(std::cerr << "Processing set in S\n");
    DEBUG(dumpIntersection(*CurrentSet));

    //Result of intersection
    std::vector<MSchedGraphNode*> IntersectCurrent;

    predIntersect(*CurrentSet, IntersectCurrent);

    //If the intersection of predecessor and current set is not empty
    //sort nodes bottom up
    if(IntersectCurrent.size() != 0) {
      DEBUG(std::cerr << "Final Node Order Predecessors and Current Set interesection is NOT empty\n");
      order = BOTTOM_UP;
    }
    //If empty, use successors
    else {
      DEBUG(std::cerr << "Final Node Order Predecessors and Current Set interesection is empty\n");

      succIntersect(*CurrentSet, IntersectCurrent);

      //sort top-down
      if(IntersectCurrent.size() != 0) {
	 DEBUG(std::cerr << "Final Node Order Successors and Current Set interesection is NOT empty\n");
	order = TOP_DOWN;
      }
      else {
	DEBUG(std::cerr << "Final Node Order Successors and Current Set interesection is empty\n");
	//Find node with max ASAP in current Set
	MSchedGraphNode *node;
	int maxASAP = 0;
	DEBUG(std::cerr << "Using current set of size " << CurrentSet->size() << "to find max ASAP\n");
	for(unsigned j=0; j < CurrentSet->size(); ++j) {
	  //Get node attributes
	  MSNodeAttributes nodeAttr= nodeToAttributesMap.find((*CurrentSet)[j])->second;
	  //assert(nodeAttr != nodeToAttributesMap.end() && "Node not in attributes map!");
	  DEBUG(std::cerr << "CurrentSet index " << j << "has ASAP: " << nodeAttr.ASAP << "\n");
	  if(maxASAP < nodeAttr.ASAP) {
	    maxASAP = nodeAttr.ASAP;
	    node = (*CurrentSet)[j];
	  }
	}
	assert(node != 0 && "In node ordering node should not be null");
	IntersectCurrent.push_back(node);
	order = BOTTOM_UP;
      }
    }
      
    //Repeat until all nodes are put into the final order from current set
    while(IntersectCurrent.size() > 0) {

      if(order == TOP_DOWN) {
	DEBUG(std::cerr << "Order is TOP DOWN\n");

	while(IntersectCurrent.size() > 0) {
	  DEBUG(std::cerr << "Intersection is not empty, so find heighest height\n");
	  
	  int MOB = 0;
	  int height = 0;
	  MSchedGraphNode *highestHeightNode = IntersectCurrent[0];
	  	  
	  //Find node in intersection with highest heigh and lowest MOB
	  for(std::vector<MSchedGraphNode*>::iterator I = IntersectCurrent.begin(), 
		E = IntersectCurrent.end(); I != E; ++I) {
	    
	    //Get current nodes properties
	    MSNodeAttributes nodeAttr= nodeToAttributesMap.find(*I)->second;

	    if(height < nodeAttr.height) {
	      highestHeightNode = *I;
	      height = nodeAttr.height;
	      MOB = nodeAttr.MOB;
	    }
	    else if(height ==  nodeAttr.height) {
	      if(MOB > nodeAttr.height) {
		highestHeightNode = *I;
		height =  nodeAttr.height;
		MOB = nodeAttr.MOB;
	      }
	    }
	  }
	  
	  //Append our node with greatest height to the NodeOrder
	  if(find(FinalNodeOrder.begin(), FinalNodeOrder.end(), highestHeightNode) == FinalNodeOrder.end()) {
	    DEBUG(std::cerr << "Adding node to Final Order: " << *highestHeightNode << "\n");
	    FinalNodeOrder.push_back(highestHeightNode);
	  }

	  //Remove V from IntersectOrder
	  IntersectCurrent.erase(find(IntersectCurrent.begin(), 
				      IntersectCurrent.end(), highestHeightNode));


	  //Intersect V's successors with CurrentSet
	  for(MSchedGraphNode::succ_iterator P = highestHeightNode->succ_begin(),
		E = highestHeightNode->succ_end(); P != E; ++P) {
	    //if(lower_bound(CurrentSet->begin(), 
	    //	   CurrentSet->end(), *P) != CurrentSet->end()) {
	    if(find(CurrentSet->begin(), CurrentSet->end(), *P) != CurrentSet->end()) {  
	      if(ignoreEdge(highestHeightNode, *P))
		continue;
	      //If not already in Intersect, add
	      if(find(IntersectCurrent.begin(), IntersectCurrent.end(), *P) == IntersectCurrent.end())
		IntersectCurrent.push_back(*P);
	    }
	  }
     	} //End while loop over Intersect Size

	//Change direction
	order = BOTTOM_UP;

	//Reset Intersect to reflect changes in OrderNodes
	IntersectCurrent.clear();
	predIntersect(*CurrentSet, IntersectCurrent);
	
      } //End If TOP_DOWN
	
	//Begin if BOTTOM_UP
      else {
	DEBUG(std::cerr << "Order is BOTTOM UP\n");
	while(IntersectCurrent.size() > 0) {
	  DEBUG(std::cerr << "Intersection of size " << IntersectCurrent.size() << ", finding highest depth\n");

	  //dump intersection
	  DEBUG(dumpIntersection(IntersectCurrent));
	  //Get node with highest depth, if a tie, use one with lowest
	  //MOB
	  int MOB = 0;
	  int depth = 0;
	  MSchedGraphNode *highestDepthNode = IntersectCurrent[0];
	  
	  for(std::vector<MSchedGraphNode*>::iterator I = IntersectCurrent.begin(), 
		E = IntersectCurrent.end(); I != E; ++I) {
	    //Find node attribute in graph
	    MSNodeAttributes nodeAttr= nodeToAttributesMap.find(*I)->second;
	    
	    if(depth < nodeAttr.depth) {
	      highestDepthNode = *I;
	      depth = nodeAttr.depth;
	      MOB = nodeAttr.MOB;
	    }
	    else if(depth == nodeAttr.depth) {
	      if(MOB > nodeAttr.MOB) {
		highestDepthNode = *I;
		depth = nodeAttr.depth;
		MOB = nodeAttr.MOB;
	      }
	    }
	  }
	  
	  

	  //Append highest depth node to the NodeOrder
	   if(find(FinalNodeOrder.begin(), FinalNodeOrder.end(), highestDepthNode) == FinalNodeOrder.end()) {
	     DEBUG(std::cerr << "Adding node to Final Order: " << *highestDepthNode << "\n");
	     FinalNodeOrder.push_back(highestDepthNode);
	   }
	  //Remove heightestDepthNode from IntersectOrder
	  IntersectCurrent.erase(find(IntersectCurrent.begin(), 
				      IntersectCurrent.end(),highestDepthNode));
	  

	  //Intersect heightDepthNode's pred with CurrentSet
	  for(MSchedGraphNode::pred_iterator P = highestDepthNode->pred_begin(), 
		E = highestDepthNode->pred_end(); P != E; ++P) {
	    //if(lower_bound(CurrentSet->begin(), 
	    //	   CurrentSet->end(), *P) != CurrentSet->end()) {
	    if(find(CurrentSet->begin(), CurrentSet->end(), *P) != CurrentSet->end()) {
	    
	      if(ignoreEdge(*P, highestDepthNode))
		continue;
	    
	    //If not already in Intersect, add
	    if(find(IntersectCurrent.begin(), 
		      IntersectCurrent.end(), *P) == IntersectCurrent.end())
		IntersectCurrent.push_back(*P);
	    }
	  }
	  
	} //End while loop over Intersect Size
	
	  //Change order
	order = TOP_DOWN;
	
	//Reset IntersectCurrent to reflect changes in OrderNodes
	IntersectCurrent.clear();
	succIntersect(*CurrentSet, IntersectCurrent);
	} //End if BOTTOM_DOWN
	
    }
    //End Wrapping while loop
      
  }//End for over all sets of nodes
   
  //Return final Order
  //return FinalNodeOrder;
}

void ModuloSchedulingPass::computeSchedule() {

  bool success = false;
  
  while(!success) {
    
    //Loop over the final node order and process each node
    for(std::vector<MSchedGraphNode*>::iterator I = FinalNodeOrder.begin(), 
	  E = FinalNodeOrder.end(); I != E; ++I) {
      
      //CalculateEarly and Late start
      int EarlyStart = -1;
      int LateStart = 99999; //Set to something higher then we would ever expect (FIXME)
      bool hasSucc = false;
      bool hasPred = false;
      
      if(!(*I)->isBranch()) {
	//Loop over nodes in the schedule and determine if they are predecessors
	//or successors of the node we are trying to schedule
	for(MSSchedule::schedule_iterator nodesByCycle = schedule.begin(), nodesByCycleEnd = schedule.end(); 
	    nodesByCycle != nodesByCycleEnd; ++nodesByCycle) {
	  
	  //For this cycle, get the vector of nodes schedule and loop over it
	  for(std::vector<MSchedGraphNode*>::iterator schedNode = nodesByCycle->second.begin(), SNE = nodesByCycle->second.end(); schedNode != SNE; ++schedNode) {
	    
	    if((*I)->isPredecessor(*schedNode)) {
	      if(!ignoreEdge(*schedNode, *I)) {
		int diff = (*I)->getInEdge(*schedNode).getIteDiff();
		int ES_Temp = nodesByCycle->first + (*schedNode)->getLatency() - diff * II;
		DEBUG(std::cerr << "Diff: " << diff << " Cycle: " << nodesByCycle->first << "\n");
		DEBUG(std::cerr << "Temp EarlyStart: " << ES_Temp << " Prev EarlyStart: " << EarlyStart << "\n");
		EarlyStart = std::max(EarlyStart, ES_Temp);
		hasPred = true;
	      }
	    }
	    if((*I)->isSuccessor(*schedNode)) {
	      if(!ignoreEdge(*I,*schedNode)) {
		int diff = (*schedNode)->getInEdge(*I).getIteDiff();
		int LS_Temp = nodesByCycle->first - (*I)->getLatency() + diff * II;
		DEBUG(std::cerr << "Diff: " << diff << " Cycle: " << nodesByCycle->first << "\n");
		DEBUG(std::cerr << "Temp LateStart: " << LS_Temp << " Prev LateStart: " << LateStart << "\n");
		LateStart = std::min(LateStart, LS_Temp);
		hasSucc = true;
	      }
	    }
	  }
	}
      }
      else {
	//WARNING: HACK! FIXME!!!!
	EarlyStart = II-1;
	LateStart = II-1;
	hasPred = 1;
	hasSucc = 1;
      }
 
      
      DEBUG(std::cerr << "Has Successors: " << hasSucc << ", Has Pred: " << hasPred << "\n");
      DEBUG(std::cerr << "EarlyStart: " << EarlyStart << ", LateStart: " << LateStart << "\n");

      //Check if the node has no pred or successors and set Early Start to its ASAP
      if(!hasSucc && !hasPred)
	EarlyStart = nodeToAttributesMap.find(*I)->second.ASAP;
      
      //Now, try to schedule this node depending upon its pred and successor in the schedule
      //already
      if(!hasSucc && hasPred)
	success = scheduleNode(*I, EarlyStart, (EarlyStart + II -1));
      else if(!hasPred && hasSucc)
	success = scheduleNode(*I, LateStart, (LateStart - II +1));
      else if(hasPred && hasSucc)
	success = scheduleNode(*I, EarlyStart, std::min(LateStart, (EarlyStart + II -1)));
      else
	success = scheduleNode(*I, EarlyStart, EarlyStart + II - 1);
      
      if(!success) {
	++II; 
	schedule.clear();
	break;
      }
     
    }

    DEBUG(std::cerr << "Constructing Kernel\n");
    success = schedule.constructKernel(II);
    if(!success) {
      ++II;
      schedule.clear();
    }
  } 
}


bool ModuloSchedulingPass::scheduleNode(MSchedGraphNode *node, 
				      int start, int end) {
  bool success = false;

  DEBUG(std::cerr << *node << " (Start Cycle: " << start << ", End Cycle: " << end << ")\n");

  //Make sure start and end are not negative
  if(start < 0)
    start = 0;
  if(end < 0)
    end = 0;

  bool forward = true;
  if(start > end)
    forward = false;

  bool increaseSC = true;
  int cycle = start ;


  while(increaseSC) {
    
    increaseSC = false;

    increaseSC = schedule.insert(node, cycle);
    
    if(!increaseSC) 
      return true;

    //Increment cycle to try again
    if(forward) {
      ++cycle;
      DEBUG(std::cerr << "Increase cycle: " << cycle << "\n");
      if(cycle > end)
	return false;
    }
    else {
      --cycle;
      DEBUG(std::cerr << "Decrease cycle: " << cycle << "\n");
      if(cycle < end)
	return false;
    }
  }

  return success;
}

void ModuloSchedulingPass::writePrologues(std::vector<MachineBasicBlock *> &prologues, MachineBasicBlock *origBB, std::vector<BasicBlock*> &llvm_prologues, std::map<const Value*, std::pair<const MSchedGraphNode*, int> > &valuesToSave, std::map<Value*, std::map<int, std::vector<Value*> > > &newValues, std::map<Value*, MachineBasicBlock*> &newValLocation) {

  //Keep a map to easily know whats in the kernel
  std::map<int, std::set<const MachineInstr*> > inKernel;
  int maxStageCount = 0;

  MSchedGraphNode *branch = 0;

  for(MSSchedule::kernel_iterator I = schedule.kernel_begin(), E = schedule.kernel_end(); I != E; ++I) {
    maxStageCount = std::max(maxStageCount, I->second);
    
    //Ignore the branch, we will handle this separately
    if(I->first->isBranch()) {
      branch = I->first;
      continue;
    }

    //Put int the map so we know what instructions in each stage are in the kernel
    DEBUG(std::cerr << "Inserting instruction " << *(I->first->getInst()) << " into map at stage " << I->second << "\n");
    inKernel[I->second].insert(I->first->getInst());
  }

  //Get target information to look at machine operands
  const TargetInstrInfo *mii = target.getInstrInfo();

 //Now write the prologues
  for(int i = 0; i < maxStageCount; ++i) {
    BasicBlock *llvmBB = new BasicBlock("PROLOGUE", (Function*) (origBB->getBasicBlock()->getParent()));
    MachineBasicBlock *machineBB = new MachineBasicBlock(llvmBB);
  
    DEBUG(std::cerr << "i=" << i << "\n");
    for(int j = 0; j <= i; ++j) {
      for(MachineBasicBlock::const_iterator MI = origBB->begin(), ME = origBB->end(); ME != MI; ++MI) {
	if(inKernel[j].count(&*MI)) {
	  machineBB->push_back(MI->clone());
	  
	  Instruction *tmp;

	  //After cloning, we may need to save the value that this instruction defines
	  for(unsigned opNum=0; opNum < MI->getNumOperands(); ++opNum) {
	    //get machine operand
	    const MachineOperand &mOp = MI->getOperand(opNum);
	    if(mOp.getType() == MachineOperand::MO_VirtualRegister && mOp.isDef()) {


	      //Check if this is a value we should save
	      if(valuesToSave.count(mOp.getVRegValue())) {
		//Save copy in tmpInstruction
		tmp = new TmpInstruction(mOp.getVRegValue());
		
		DEBUG(std::cerr << "Value: " << mOp.getVRegValue() << " New Value: " << tmp << " Stage: " << i << "\n");
		newValues[mOp.getVRegValue()][i].push_back(tmp);
		newValLocation[tmp] = machineBB;

		DEBUG(std::cerr << "Machine Instr Operands: " << mOp.getVRegValue() << ", 0, " << tmp << "\n");
		
		//Create machine instruction and put int machineBB
		MachineInstr *saveValue = BuildMI(machineBB, V9::ORr, 3).addReg(mOp.getVRegValue()).addImm(0).addRegDef(tmp);
		
		DEBUG(std::cerr << "Created new machine instr: " << *saveValue << "\n");
	      }
	    }
	  }
	}
      }
    }


    //Stick in branch at the end
    machineBB->push_back(branch->getInst()->clone());

  (((MachineBasicBlock*)origBB)->getParent())->getBasicBlockList().push_back(machineBB);  
    prologues.push_back(machineBB);
    llvm_prologues.push_back(llvmBB);
  }
}

void ModuloSchedulingPass::writeEpilogues(std::vector<MachineBasicBlock *> &epilogues, const MachineBasicBlock *origBB, std::vector<BasicBlock*> &llvm_epilogues, std::map<const Value*, std::pair<const MSchedGraphNode*, int> > &valuesToSave, std::map<Value*, std::map<int, std::vector<Value*> > > &newValues,std::map<Value*, MachineBasicBlock*> &newValLocation ) {
  
  std::map<int, std::set<const MachineInstr*> > inKernel;
  int maxStageCount = 0;
  for(MSSchedule::kernel_iterator I = schedule.kernel_begin(), E = schedule.kernel_end(); I != E; ++I) {
    maxStageCount = std::max(maxStageCount, I->second);
    
    //Ignore the branch, we will handle this separately
    if(I->first->isBranch())
      continue;

    //Put int the map so we know what instructions in each stage are in the kernel
    inKernel[I->second].insert(I->first->getInst());
  }

  std::map<Value*, Value*> valPHIs;

  //Now write the epilogues
  for(int i = maxStageCount-1; i >= 0; --i) {
    BasicBlock *llvmBB = new BasicBlock("EPILOGUE", (Function*) (origBB->getBasicBlock()->getParent()));
    MachineBasicBlock *machineBB = new MachineBasicBlock(llvmBB);
   
    DEBUG(std::cerr << " i: " << i << "\n");

    //Spit out phi nodes
    for(std::map<Value*, std::map<int, std::vector<Value*> > >::iterator V = newValues.begin(), E = newValues.end();
	V != E; ++V) {

      DEBUG(std::cerr << "Writing phi for" << *(V->first));
      for(std::map<int, std::vector<Value*> >::iterator I = V->second.begin(), IE = V->second.end(); I != IE; ++I) {
	if(I->first == i) {
	  DEBUG(std::cerr << "BLAH " << i << "\n");
	  
	  //Vector must have two elements in it:
	  assert(I->second.size() == 2 && "Vector size should be two\n");
	  
	  Instruction *tmp = new TmpInstruction(I->second[0]);
	  MachineInstr *saveValue = BuildMI(machineBB, V9::PHI, 3).addReg(I->second[0]).addReg(I->second[1]).addRegDef(tmp);
	  valPHIs[V->first] = tmp;
	}
      }
      
    }

    for(MachineBasicBlock::const_iterator MI = origBB->begin(), ME = origBB->end(); ME != MI; ++MI) {
      for(int j=maxStageCount; j > i; --j) {
	if(inKernel[j].count(&*MI)) {
	  DEBUG(std::cerr << "Cloning instruction " << *MI << "\n");
	  MachineInstr *clone = MI->clone();
	  
	  //Update operands that need to use the result from the phi
	  for(unsigned i=0; i < clone->getNumOperands(); ++i) {
	    //get machine operand
	    const MachineOperand &mOp = clone->getOperand(i);
	    if((mOp.getType() == MachineOperand::MO_VirtualRegister && mOp.isUse())) {
	      if(valPHIs.count(mOp.getVRegValue())) {
		//Update the operand in the cloned instruction
		clone->getOperand(i).setValueReg(valPHIs[mOp.getVRegValue()]); 
	      }
	    }
	  }
	  machineBB->push_back(clone);
	}
      }
    }

    (((MachineBasicBlock*)origBB)->getParent())->getBasicBlockList().push_back(machineBB);
    epilogues.push_back(machineBB);
    llvm_epilogues.push_back(llvmBB);
  }
}

void ModuloSchedulingPass::writeKernel(BasicBlock *llvmBB, MachineBasicBlock *machineBB, std::map<const Value*, std::pair<const MSchedGraphNode*, int> > &valuesToSave, std::map<Value*, std::map<int, std::vector<Value*> > > &newValues, std::map<Value*, MachineBasicBlock*> &newValLocation) {
  
  //Keep track of operands that are read and saved from a previous iteration. The new clone
  //instruction will use the result of the phi instead.
  std::map<Value*, Value*> finalPHIValue;
  std::map<Value*, Value*> kernelValue;

    //Create TmpInstructions for the final phis
 for(MSSchedule::kernel_iterator I = schedule.kernel_begin(), E = schedule.kernel_end(); I != E; ++I) {

   //Clone instruction
   const MachineInstr *inst = I->first->getInst();
   MachineInstr *instClone = inst->clone();
   
   //If this instruction is from a previous iteration, update its operands
   if(I->second > 0) {
     //Loop over Machine Operands
     const MachineInstr *inst = I->first->getInst();
     for(unsigned i=0; i < inst->getNumOperands(); ++i) {
       //get machine operand
       const MachineOperand &mOp = inst->getOperand(i);

       if(mOp.getType() == MachineOperand::MO_VirtualRegister && mOp.isUse()) {
	 //If its in the value saved, we need to create a temp instruction and use that instead
	 if(valuesToSave.count(mOp.getVRegValue())) {
	   TmpInstruction *tmp = new TmpInstruction(mOp.getVRegValue());
	   
	   //Update the operand in the cloned instruction
	   instClone->getOperand(i).setValueReg(tmp);
	   
	   //save this as our final phi
	   finalPHIValue[mOp.getVRegValue()] = tmp;
	   newValLocation[tmp] = machineBB;
	 }
       }

     }
     //Insert into machine basic block
     machineBB->push_back(instClone);

   }
   //Otherwise we just check if we need to save a value or not
   else {
     //Insert into machine basic block
     machineBB->push_back(instClone);

     //Loop over Machine Operands
     const MachineInstr *inst = I->first->getInst();
     for(unsigned i=0; i < inst->getNumOperands(); ++i) {
       //get machine operand
       const MachineOperand &mOp = inst->getOperand(i);

       if(mOp.getType() == MachineOperand::MO_VirtualRegister && mOp.isDef()) {
	 if(valuesToSave.count(mOp.getVRegValue())) {
	   
	   TmpInstruction *tmp = new TmpInstruction(mOp.getVRegValue());
	   
	   //Create new machine instr and put in MBB
	   MachineInstr *saveValue = BuildMI(machineBB, V9::ORr, 3).addReg(mOp.getVRegValue()).addImm(0).addRegDef(tmp);
	   
	   //Save for future cleanup
	   kernelValue[mOp.getVRegValue()] = tmp;
	   newValLocation[tmp] = machineBB;
	 }
       }
     }
   }
 }

 //Clean up by writing phis
 for(std::map<Value*, std::map<int, std::vector<Value*> > >::iterator V = newValues.begin(), E = newValues.end();
     V != E; ++V) {

   DEBUG(std::cerr << "Writing phi for" << *(V->first));
  
   //FIXME
   int maxStage = 1;

   //Last phi
   Instruction *lastPHI = 0;

   for(std::map<int, std::vector<Value*> >::iterator I = V->second.begin(), IE = V->second.end();
       I != IE; ++I) {
     
     int stage = I->first;

     DEBUG(std::cerr << "Stage: " << I->first << " vector size: " << I->second.size() << "\n");

     //Assert if this vector is ever greater then 1. This should not happen
     //FIXME: Get rid of vector if we convince ourselves this won't happn
     assert(I->second.size() == 1 && "Vector of values should be of size \n");

     //We must handle the first and last phi specially
     if(stage == maxStage) {
       //The resulting value must be the Value* we created earlier
       assert(lastPHI != 0 && "Last phi is NULL!\n");
       MachineInstr *saveValue = BuildMI(*machineBB, machineBB->begin(), V9::PHI, 3).addReg(lastPHI).addReg(I->second[0]).addRegDef(finalPHIValue[V->first]);
       I->second.push_back(finalPHIValue[V->first]);
     }
     else if(stage == 0) {
       lastPHI = new TmpInstruction(I->second[0]);
       MachineInstr *saveValue = BuildMI(*machineBB, machineBB->begin(), V9::PHI, 3).addReg(kernelValue[V->first]).addReg(I->second[0]).addRegDef(lastPHI);
       I->second.push_back(lastPHI);
       newValLocation[lastPHI] = machineBB;
     }
     else {
        Instruction *tmp = new TmpInstruction(I->second[0]);
	MachineInstr *saveValue = BuildMI(*machineBB, machineBB->begin(), V9::PHI, 3).addReg(lastPHI).addReg(I->second[0]).addRegDef(tmp);
	lastPHI = tmp;
	I->second.push_back(lastPHI);
       newValLocation[tmp] = machineBB;
     }
   }
 }
}

void ModuloSchedulingPass::removePHIs(const MachineBasicBlock *origBB, std::vector<MachineBasicBlock *> &prologues, std::vector<MachineBasicBlock *> &epilogues, MachineBasicBlock *kernelBB, std::map<Value*, MachineBasicBlock*> &newValLocation) {

  //Worklist to delete things
  std::vector<std::pair<MachineBasicBlock*, MachineBasicBlock::iterator> > worklist;
  
  const TargetInstrInfo *TMI = target.getInstrInfo();

  //Start with the kernel and for each phi insert a copy for the phi def and for each arg
  for(MachineBasicBlock::iterator I = kernelBB->begin(), E = kernelBB->end(); I != E; ++I) {
    //Get op code and check if its a phi
     if(I->getOpcode() == V9::PHI) {
       Instruction *tmp = 0;
       for(unsigned i = 0; i < I->getNumOperands(); ++i) {
	 //Get Operand
	 const MachineOperand &mOp = I->getOperand(i);
	 assert(mOp.getType() == MachineOperand::MO_VirtualRegister && "Should be a Value*\n");
	 
	 if(!tmp) {
	   tmp = new TmpInstruction(mOp.getVRegValue());
	 }

      	 //Now for all our arguments we read, OR to the new TmpInstruction that we created
	 if(mOp.isUse()) {
	   DEBUG(std::cerr << "Use: " << mOp << "\n");
	   //Place a copy at the end of its BB but before the branches
	   assert(newValLocation.count(mOp.getVRegValue()) && "We must know where this value is located\n");
	   //Reverse iterate to find the branches, we can safely assume no instructions have been
	   //put in the nop positions
	   for(MachineBasicBlock::iterator inst = --(newValLocation[mOp.getVRegValue()])->end(), endBB = (newValLocation[mOp.getVRegValue()])->begin(); inst != endBB; --inst) {
	     MachineOpCode opc = inst->getOpcode();
	     if(TMI->isBranch(opc) || TMI->isNop(opc))
	       continue;
	     else {
	       BuildMI(*(newValLocation[mOp.getVRegValue()]), ++inst, V9::ORr, 3).addReg(mOp.getVRegValue()).addImm(0).addRegDef(tmp);
	       break;
	     }
	       
	   }

	 }
	 else {
	   //Remove the phi and replace it with an OR
	   DEBUG(std::cerr << "Def: " << mOp << "\n");
	   BuildMI(*kernelBB, I, V9::ORr, 3).addReg(tmp).addImm(0).addRegDef(mOp.getVRegValue());
	   worklist.push_back(std::make_pair(kernelBB, I));
	 }

       }
     }
       
  }

  //Remove phis from epilogue
  for(std::vector<MachineBasicBlock*>::iterator MB = epilogues.begin(), ME = epilogues.end(); MB != ME; ++MB) {
    for(MachineBasicBlock::iterator I = (*MB)->begin(), E = (*MB)->end(); I != E; ++I) {
      //Get op code and check if its a phi
      if(I->getOpcode() == V9::PHI) {
	Instruction *tmp = 0;
	for(unsigned i = 0; i < I->getNumOperands(); ++i) {
	  //Get Operand
	  const MachineOperand &mOp = I->getOperand(i);
	  assert(mOp.getType() == MachineOperand::MO_VirtualRegister && "Should be a Value*\n");
	  
	  if(!tmp) {
	    tmp = new TmpInstruction(mOp.getVRegValue());
	  }
	  
	  //Now for all our arguments we read, OR to the new TmpInstruction that we created
	  if(mOp.isUse()) {
	    DEBUG(std::cerr << "Use: " << mOp << "\n");
	    //Place a copy at the end of its BB but before the branches
	    assert(newValLocation.count(mOp.getVRegValue()) && "We must know where this value is located\n");
	    //Reverse iterate to find the branches, we can safely assume no instructions have been
	    //put in the nop positions
	    for(MachineBasicBlock::iterator inst = --(newValLocation[mOp.getVRegValue()])->end(), endBB = (newValLocation[mOp.getVRegValue()])->begin(); inst != endBB; --inst) {
	      MachineOpCode opc = inst->getOpcode();
	      if(TMI->isBranch(opc) || TMI->isNop(opc))
		continue;
	      else {
		BuildMI(*(newValLocation[mOp.getVRegValue()]), ++inst, V9::ORr, 3).addReg(mOp.getVRegValue()).addImm(0).addRegDef(tmp);
		break;
	      }
	      
	    }
	    
	  }
	  else {
	    //Remove the phi and replace it with an OR
	    DEBUG(std::cerr << "Def: " << mOp << "\n");
	    BuildMI(**MB, I, V9::ORr, 3).addReg(tmp).addImm(0).addRegDef(mOp.getVRegValue());
	    worklist.push_back(std::make_pair(*MB,I));
	  }
	  
	}
      }
    }
  }

    //Delete the phis
  for(std::vector<std::pair<MachineBasicBlock*, MachineBasicBlock::iterator> >::iterator I =  worklist.begin(), E = worklist.end(); I != E; ++I) {
    I->first->erase(I->second);
		    
  }

}


void ModuloSchedulingPass::reconstructLoop(MachineBasicBlock *BB) {

  //First find the value *'s that we need to "save"
  std::map<const Value*, std::pair<const MSchedGraphNode*, int> > valuesToSave;

  //Loop over kernel and only look at instructions from a stage > 0
  //Look at its operands and save values *'s that are read
  for(MSSchedule::kernel_iterator I = schedule.kernel_begin(), E = schedule.kernel_end(); I != E; ++I) {

    if(I->second > 0) {
      //For this instruction, get the Value*'s that it reads and put them into the set.
      //Assert if there is an operand of another type that we need to save
      const MachineInstr *inst = I->first->getInst();
      for(unsigned i=0; i < inst->getNumOperands(); ++i) {
	//get machine operand
	const MachineOperand &mOp = inst->getOperand(i);
	
	if(mOp.getType() == MachineOperand::MO_VirtualRegister && mOp.isUse()) {
	  //find the value in the map
	  if (const Value* srcI = mOp.getVRegValue())
	    valuesToSave[srcI] = std::make_pair(I->first, i);
	  
	}
	
	if(mOp.getType() != MachineOperand::MO_VirtualRegister && mOp.isUse()) {
	  assert("Our assumption is wrong. We have another type of register that needs to be saved\n");
	}
      }
    }
  }

  //The new loop will consist of one or more prologues, the kernel, and one or more epilogues.

  //Map to keep track of old to new values
  std::map<Value*, std::map<int, std::vector<Value*> > > newValues;
 
  //Another map to keep track of what machine basic blocks these new value*s are in since
  //they have no llvm instruction equivalent
  std::map<Value*, MachineBasicBlock*> newValLocation;

  std::vector<MachineBasicBlock*> prologues;
  std::vector<BasicBlock*> llvm_prologues;


  //Write prologue
  writePrologues(prologues, BB, llvm_prologues, valuesToSave, newValues, newValLocation);

  BasicBlock *llvmKernelBB = new BasicBlock("Kernel", (Function*) (BB->getBasicBlock()->getParent()));
  MachineBasicBlock *machineKernelBB = new MachineBasicBlock(llvmKernelBB);
  
  writeKernel(llvmKernelBB, machineKernelBB, valuesToSave, newValues, newValLocation);
  (((MachineBasicBlock*)BB)->getParent())->getBasicBlockList().push_back(machineKernelBB);
 
  std::vector<MachineBasicBlock*> epilogues;
  std::vector<BasicBlock*> llvm_epilogues;

  //Write epilogues
  writeEpilogues(epilogues, BB, llvm_epilogues, valuesToSave, newValues, newValLocation);


  const TargetInstrInfo *TMI = target.getInstrInfo();

  //Fix up machineBB and llvmBB branches
  for(unsigned I = 0; I <  prologues.size(); ++I) {
   
    MachineInstr *branch = 0;
    
    //Find terminator since getFirstTerminator does not work!
    for(MachineBasicBlock::reverse_iterator mInst = prologues[I]->rbegin(), mInstEnd = prologues[I]->rend(); mInst != mInstEnd; ++mInst) {
      MachineOpCode OC = mInst->getOpcode();
      if(TMI->isBranch(OC)) {
	branch = &*mInst;
	DEBUG(std::cerr << *mInst << "\n");
	break;
      }
    }

   
 
    //Update branch
    for(unsigned opNum = 0; opNum < branch->getNumOperands(); ++opNum) {
      MachineOperand &mOp = branch->getOperand(opNum);
      if (mOp.getType() == MachineOperand::MO_PCRelativeDisp) {
	mOp.setValueReg(llvm_epilogues[(llvm_epilogues.size()-1-I)]);
      }
    }

    //Update llvm basic block with our new branch instr
    DEBUG(std::cerr << BB->getBasicBlock()->getTerminator() << "\n");
    const BranchInst *branchVal = dyn_cast<BranchInst>(BB->getBasicBlock()->getTerminator());
    TmpInstruction *tmp = new TmpInstruction(branchVal->getCondition());
    if(I == prologues.size()-1) {
      TerminatorInst *newBranch = new BranchInst(llvmKernelBB,
						 llvm_epilogues[(llvm_epilogues.size()-1-I)], 
						 tmp, 
						 llvm_prologues[I]);
    }
    else
      TerminatorInst *newBranch = new BranchInst(llvm_prologues[I+1],
						 llvm_epilogues[(llvm_epilogues.size()-1-I)], 
						 tmp, 
						 llvm_prologues[I]);

    assert(branch != 0 && "There must be a terminator for this machine basic block!\n");
  
    //Push nop onto end of machine basic block
    BuildMI(prologues[I], V9::NOP, 0);
    
    //Now since I don't trust fall throughs, add a unconditional branch to the next prologue
    if(I != prologues.size()-1)
      BuildMI(prologues[I], V9::BA, 1).addReg(llvm_prologues[I+1]);
    else
      BuildMI(prologues[I], V9::BA, 1).addReg(llvmKernelBB);

    //Add one more nop!
    BuildMI(prologues[I], V9::NOP, 0);
  }

  //Fix up kernel machine branches
  MachineInstr *branch = 0;
  for(MachineBasicBlock::reverse_iterator mInst = machineKernelBB->rbegin(), mInstEnd = machineKernelBB->rend(); mInst != mInstEnd; ++mInst) {
    MachineOpCode OC = mInst->getOpcode();
    if(TMI->isBranch(OC)) {
      branch = &*mInst;
      DEBUG(std::cerr << *mInst << "\n");
      break;
    }
  }

  assert(branch != 0 && "There must be a terminator for the kernel machine basic block!\n");
   
  //Update kernel self loop branch
  for(unsigned opNum = 0; opNum < branch->getNumOperands(); ++opNum) {
    MachineOperand &mOp = branch->getOperand(opNum);
    
    if (mOp.getType() == MachineOperand::MO_PCRelativeDisp) {
      mOp.setValueReg(llvmKernelBB);
    }
  }
  
  //Update kernelLLVM branches
  const BranchInst *branchVal = dyn_cast<BranchInst>(BB->getBasicBlock()->getTerminator());
  TerminatorInst *newBranch = new BranchInst(llvmKernelBB,
					     llvm_epilogues[0], 
					     new TmpInstruction(branchVal->getCondition()), 
					     llvmKernelBB);

  //Add kernel noop
   BuildMI(machineKernelBB, V9::NOP, 0);

   //Add unconditional branch to first epilogue
   BuildMI(machineKernelBB, V9::BA, 1).addReg(llvm_epilogues[0]);

   //Add kernel noop
   BuildMI(machineKernelBB, V9::NOP, 0);

   //Lastly add unconditional branches for the epilogues
   for(unsigned I = 0; I <  epilogues.size(); ++I) {
     
    //Now since I don't trust fall throughs, add a unconditional branch to the next prologue
     if(I != epilogues.size()-1) {
       BuildMI(epilogues[I], V9::BA, 1).addReg(llvm_epilogues[I+1]);
       //Add unconditional branch to end of epilogue
       TerminatorInst *newBranch = new BranchInst(llvm_epilogues[I+1], 
						  llvm_epilogues[I]);

     }
    else {
      MachineBasicBlock *origBlock = (MachineBasicBlock*) BB;
      for(MachineBasicBlock::reverse_iterator inst = origBlock->rbegin(), instEnd = origBlock->rend(); inst != instEnd; ++inst) {
	MachineOpCode OC = inst->getOpcode();
	if(TMI->isBranch(OC)) {
	  branch = &*inst;
	  DEBUG(std::cerr << *inst << "\n");
	  break;
	
	}
	
	for(unsigned opNum = 0; opNum < branch->getNumOperands(); ++opNum) {
	  MachineOperand &mOp = branch->getOperand(opNum);
	  
	  if (mOp.getType() == MachineOperand::MO_PCRelativeDisp) {
	    BuildMI(epilogues[I], V9::BA, 1).addReg(mOp.getVRegValue());
	    break;
	  }
	}
	
      }
      
      //Update last epilogue exit branch
      BranchInst *branchVal = (BranchInst*) dyn_cast<BranchInst>(BB->getBasicBlock()->getTerminator());
      //Find where we are supposed to branch to
      BasicBlock *nextBlock = 0;
      for(unsigned j=0; j <branchVal->getNumSuccessors(); ++j) {
	if(branchVal->getSuccessor(j) != BB->getBasicBlock())
	  nextBlock = branchVal->getSuccessor(j);
      }
	TerminatorInst *newBranch = new BranchInst(nextBlock, llvm_epilogues[I]);
    }
    //Add one more nop!
    BuildMI(epilogues[I], V9::NOP, 0);

   }

   //FIX UP Machine BB entry!!
   //We are looking at the predecesor of our loop basic block and we want to change its ba instruction
   

   //Find all llvm basic blocks that branch to the loop entry and change to our first prologue.
   const BasicBlock *llvmBB = BB->getBasicBlock();

   for(pred_const_iterator P = pred_begin(llvmBB), PE = pred_end(llvmBB); P != PE; ++PE) {
     if(*P == llvmBB)
       continue;
     else {
       DEBUG(std::cerr << "Found our entry BB\n");
       //Get the Terminator instruction for this basic block and print it out
       DEBUG(std::cerr << *((*P)->getTerminator()) << "\n");
       //Update the terminator
       TerminatorInst *term = ((BasicBlock*)*P)->getTerminator();
       for(unsigned i=0; i < term->getNumSuccessors(); ++i) {
	 if(term->getSuccessor(i) == llvmBB) {
	   DEBUG(std::cerr << "Replacing successor bb\n");
	   if(llvm_prologues.size() > 0) {
	     term->setSuccessor(i, llvm_prologues[0]);
	     //Also update its corresponding machine instruction
	     MachineCodeForInstruction & tempMvec =
	       MachineCodeForInstruction::get(term);
	     for (unsigned j = 0; j < tempMvec.size(); j++) {
	       MachineInstr *temp = tempMvec[j];
	       MachineOpCode opc = temp->getOpcode();
	       if(TMI->isBranch(opc)) {
		 DEBUG(std::cerr << *temp << "\n");
		 //Update branch
		 for(unsigned opNum = 0; opNum < temp->getNumOperands(); ++opNum) {
		   MachineOperand &mOp = temp->getOperand(opNum);
		   if (mOp.getType() == MachineOperand::MO_PCRelativeDisp) {
		     mOp.setValueReg(llvm_prologues[0]);
		   }
		 }
	       }
	     }        
	   }
	   else {
	     term->setSuccessor(i, llvmKernelBB);
	   //Also update its corresponding machine instruction
	     MachineCodeForInstruction & tempMvec =
	       MachineCodeForInstruction::get(term);
	     for (unsigned j = 0; j < tempMvec.size(); j++) {
	       MachineInstr *temp = tempMvec[j];
	       MachineOpCode opc = temp->getOpcode();
	       if(TMI->isBranch(opc)) {
		 DEBUG(std::cerr << *temp << "\n");
		 //Update branch
		 for(unsigned opNum = 0; opNum < temp->getNumOperands(); ++opNum) {
		   MachineOperand &mOp = temp->getOperand(opNum);
		   if (mOp.getType() == MachineOperand::MO_PCRelativeDisp) {
		     mOp.setValueReg(llvmKernelBB);
		   }
		 }
	       }
	     }
	   }
	 }
       }
       break;
     }
   }
   
   removePHIs(BB, prologues, epilogues, machineKernelBB, newValLocation);


    
  //Print out epilogues and prologue
  DEBUG(for(std::vector<MachineBasicBlock*>::iterator I = prologues.begin(), E = prologues.end(); 
      I != E; ++I) {
    std::cerr << "PROLOGUE\n";
    (*I)->print(std::cerr);
  });
  
  DEBUG(std::cerr << "KERNEL\n");
  DEBUG(machineKernelBB->print(std::cerr));

  DEBUG(for(std::vector<MachineBasicBlock*>::iterator I = epilogues.begin(), E = epilogues.end(); 
      I != E; ++I) {
    std::cerr << "EPILOGUE\n";
    (*I)->print(std::cerr);
  });


  DEBUG(std::cerr << "New Machine Function" << "\n");
  DEBUG(std::cerr << BB->getParent() << "\n");

  BB->getParent()->getBasicBlockList().erase(BB);

}

