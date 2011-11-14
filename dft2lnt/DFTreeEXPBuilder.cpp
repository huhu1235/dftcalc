#include "DFTreeEXPBuilder.h"
#include "FileWriter.h"

#include <map>

std::string DFT::DFTreeEXPBuilder::getFileForNode(const DFT::Nodes::Node& node) const {
	std::stringstream ss;
	ss << node.getTypeStr();
	if(node.isBasicEvent()) {
		const DFT::Nodes::BasicEvent& be = *static_cast<const DFT::Nodes::BasicEvent*>(&node);
	} else if(node.isGate()) {
		const DFT::Nodes::Gate& gate = *static_cast<const DFT::Nodes::Gate*>(&node);
		ss << "_" << gate.getChildren().size();
	} else {
		assert(0 && "getFileForNode(): Unknown node type");
	}
	return ss.str();
}

std::string DFT::DFTreeEXPBuilder::getBEProc(const DFT::Nodes::BasicEvent& be) const {
	std::stringstream ss;
	ss << "total rename ";
	ss << "\"FRATE !1 !2\" -> \"rate " << be.getLambda() << "\", \"FRATE !1 !1\" -> \"rate " << be.getMu() << "\" in \"";
	ss << getFileForNode(be);
	ss << ".bcg\" end rename";
	return ss.str();
}

void DFT::DFTreeEXPBuilder::printSyncLine(const EXPSyncRule& rule, const vector<unsigned int>& columnWidths) {
	std::map<unsigned int,EXPSyncItem*>::const_iterator it = rule.label.begin();
	size_t c=0;
	for(; it!=rule.label.end();++it) {
		while(c<it->first) {
			if(c>0) exp_body << " * ";
			exp_body.outlineLeftNext(columnWidths[c],' ');
			exp_body << "_";
			++c;
		}
		if(c>0) exp_body << " * ";
		exp_body.outlineLeftNext(columnWidths[c],' ');
		exp_body << it->second->toStringQuoted();
		++c;
	}
	while(c<dft->getNodes().size()) {
		if(c>0) exp_body << " * ";
		exp_body.outlineLeftNext(columnWidths[c],' ');
		exp_body << "_";
		++c;
	}
	exp_body << " -> " << rule.toLabel;
}

DFT::DFTreeEXPBuilder::DFTreeEXPBuilder(std::string root, std::string tmp, std::string nameBCG, std::string nameEXP, DFT::DFTree* dft, CompilerContext* cc):
	root(root),
	tmp(tmp),
	nameBCG(nameBCG),
	nameEXP(nameEXP),
	dft(dft),
	cc(cc) {
	
}
int DFT::DFTreeEXPBuilder::build() {

	// Check all the nodes in the DFT, adding BasicEvents to basicEvents and
	// Gates to gates. Also keep track of what Lotos NT files are needed by
	// adding them to neededFiles.
	basicEvents.clear();
	gates.clear();
	neededFiles.clear();
	nodeIDs.clear();
	for(size_t i=0; i<dft->getNodes().size(); ++i) {
		DFT::Nodes::Node* node = dft->getNodes().at(i);
		neededFiles.insert( (node->getType()) );
		if(DFT::Nodes::Node::typeMatch(node->getType(),DFT::Nodes::BasicEventType)) {
			DFT::Nodes::BasicEvent* be = static_cast<DFT::Nodes::BasicEvent*>(node);
			basicEvents.push_back(be);
		} else if(DFT::Nodes::Node::typeMatch(node->getType(),DFT::Nodes::GateType)) {
			DFT::Nodes::Gate* gate = static_cast<DFT::Nodes::Gate*>(node);
			gates.push_back(gate);
		} else {
			assert(0);
		}
		nodeIDs.insert( pair<const DFT::Nodes::Node*, unsigned int>(node,i) );
	}

	// Build the EXP file
	svl_header.clearAll();
	svl_body.clearAll();
	exp_header.clearAll();
	exp_body.clearAll();
	buildEXPHeader();
	buildEXPBody();
	
	// Build SVL file
	svl_body << "\"" << nameBCG << "\" = smart stochastic branching reduction of \"" << nameEXP << "\"" << svl_body.applypostfix;
	
	
	// Temporary: print SVL script
//	std::cout << svl_options.toString();
//	std::cout << svl_dependencies.toString();
//	std::cout << svl_body.toString();
	return 0;
}

void DFT::DFTreeEXPBuilder::printEXP(std::ostream& out) {
	out << exp_header.toString();
	out << exp_body.toString();
}

void DFT::DFTreeEXPBuilder::printSVL(std::ostream& out) {
	out << svl_header.toString();
	out << svl_body.toString();
}

unsigned int DFT::DFTreeEXPBuilder::getIDOfNode(const DFT::Nodes::Node& node) const {

	std::map<const DFT::Nodes::Node*, unsigned int>::const_iterator it = nodeIDs.find(&node);
	assert( (it != nodeIDs.end()) && "getIDOfNode() did not know the ID of the node");
	
	return it->second;
//	for(size_t i=0; i<nodes.size();++i) {
//		//std::remove(notDefinedButReferencedNodes.begin(),notDefinedButReferencedNodes.end(),nodes.at(i));
//		std::vector<Nodes::Node*>::iterator it = std::find(notDefinedButReferencedNodes.begin(),notDefinedButReferencedNodes.end(),nodes.at(i));
//		if(it != notDefinedButReferencedNodes.end()) notDefinedButReferencedNodes.erase(it);
//	}
}

int DFT::DFTreeEXPBuilder::buildEXPHeader() {
	return 0;
}

int DFT::DFTreeEXPBuilder::buildEXPBody() {
	
	/* Create synchronization rules for all nodes in the DFT */

	vector<DFT::EXPSyncRule*> activationRules;
	vector<DFT::EXPSyncRule*> failRules;

	// Create synchronization rules for the Top node in the DFT
	createSyncRuleTop(activationRules,failRules);

	// Create synchronization rules for all nodes in the DFT
	{
		std::vector<DFT::Nodes::Node*>::const_iterator it = dft->getNodes().begin();
		for(;it!=dft->getNodes().end();++it) {
			if((*it)->isGate()) {
				const DFT::Nodes::Gate& gate = static_cast<const DFT::Nodes::Gate&>(**it);
				createSyncRule(activationRules,failRules,gate,getIDOfNode(gate));
			}
		}
	}

	/* Generate the EXP based on the generated synchronization rules */

	exp_body << exp_body.applyprefix << "(* Number of rules: " << (activationRules.size()+failRules.size()) << "*)" << exp_body.applypostfix;
	exp_body << exp_body.applyprefix << "hide" << exp_body.applypostfix;
	exp_body.indent();
	
		// Hide rules
		for(size_t s=0; s<activationRules.size(); ++s) {
			EXPSyncRule& rule = *activationRules.at(s);
			if(rule.hideToLabel) {
				exp_body << exp_body.applyprefix << rule.toLabel << "," << exp_body.applypostfix;
			}
		}
		for(size_t s=0; s<failRules.size(); ++s) {
			EXPSyncRule& rule = *failRules.at(s);
			if(rule.hideToLabel) {
				exp_body << exp_body.applyprefix << rule.toLabel;
				if(s<failRules.size()-1) exp_body << ",";
				exp_body << exp_body.applypostfix;
			}
		}
	
	exp_body.outdent();
	exp_body.appendLine("in");
	exp_body.indent();
	{
		// Synchronization rules
		vector<unsigned int> columnWidths(dft->getNodes().size(),0);
		calculateColumnWidths(columnWidths,activationRules);
		calculateColumnWidths(columnWidths,failRules);

		exp_body << exp_body.applyprefix << "label par" << exp_body.applypostfix;
		exp_body << exp_body.applyprefix << "(*\t";
		std::vector<DFT::Nodes::Node*>::const_iterator it = dft->getNodes().begin();
		for(int c=0;it!=dft->getNodes().end();++it,++c) {
			if(c>0) exp_body << "   ";
			exp_body.outlineLeftNext(columnWidths[c],' ');
			exp_body << exp_body._push << (*it)->getTypeStr() << getIDOfNode(**it) << exp_body._pop;
		}
		exp_body << " *)" << exp_body.applypostfix;
		
		exp_body.indent();
		// Generate activation rules
		{
			for(size_t s=0; s<activationRules.size(); ++s) {
				exp_body << exp_body.applyprefix;
				printSyncLine(*activationRules.at(s),columnWidths);
				exp_body << ",";
				exp_body << exp_body.applypostfix;
			}
		}
		// Generate fail rules
		{
			for(size_t s=0; s<failRules.size(); ++s) {
				exp_body << exp_body.applyprefix;
				printSyncLine(*failRules.at(s),columnWidths);
				if(s<failRules.size()-1) exp_body << ",";
				exp_body << exp_body.applypostfix;
			}
		}
		exp_body.outdent();

		// Generate the parallel composition of all the nodes
		exp_body << exp_body.applyprefix << "in" << exp_body.applypostfix;
		exp_body.indent();
		{
			int c=0;
			{
				std::vector<DFT::Nodes::Node*>::iterator it = dft->getNodes().begin();
				for(;it!=dft->getNodes().end();++it,++c) {
					const DFT::Nodes::Node& node = **it;
					if(c>0) exp_body << exp_body.applyprefix << "||" << exp_body.applypostfix;
					if(node.isBasicEvent()) {
						const DFT::Nodes::BasicEvent& be = *static_cast<const DFT::Nodes::BasicEvent*>(&node);
						exp_body << exp_body.applyprefix << getBEProc(be) << exp_body.applypostfix;
					} else if(node.isGate()) {
						exp_body << exp_body.applyprefix << "\"" << getFileForNode(node) << ".bcg\"" << exp_body.applypostfix;
					} else {
						assert(0 && "buildEXPBody(): Unknown node type");
					}
				}
			}
		}
		exp_body.outdent();
		exp_body << exp_body.applyprefix << "end par" << exp_body.applypostfix;
	}
	exp_body.outdent();
	exp_body.appendLine("end hide");
	return 0;
}

	int DFT::DFTreeEXPBuilder::createSyncRuleBE(vector<DFT::EXPSyncRule*>& activationRules, vector<DFT::EXPSyncRule*>& failRules, const DFT::Nodes::BasicEvent& node, unsigned int nodeID) {
		return 0;
	}
	int DFT::DFTreeEXPBuilder::createSyncRuleGateOr(vector<DFT::EXPSyncRule*>& activationRules, vector<DFT::EXPSyncRule*>& failRules, const DFT::Nodes::GateOr& node, unsigned int nodeID) {
		return 0;
	}
	int DFT::DFTreeEXPBuilder::createSyncRuleGateAnd(vector<DFT::EXPSyncRule*>& activationRules, vector<DFT::EXPSyncRule*>& failRules, const DFT::Nodes::GateAnd& node, unsigned int nodeID) {
		return 0;
	}
	int DFT::DFTreeEXPBuilder::createSyncRuleGateWSP(vector<DFT::EXPSyncRule*>& activationRules, vector<DFT::EXPSyncRule*>& failRules, const DFT::Nodes::GateWSP& node, unsigned int nodeID) {
		return 0;
	}
	int DFT::DFTreeEXPBuilder::createSyncRuleGatePAnd(vector<DFT::EXPSyncRule*>& activationRules, vector<DFT::EXPSyncRule*>& failRules, const DFT::Nodes::GatePAnd& node, unsigned int nodeID) {
		return 0;
	}
	int DFT::DFTreeEXPBuilder::createSyncRuleTop(vector<DFT::EXPSyncRule*>& activationRules, vector<DFT::EXPSyncRule*>& failRules) {
		std::stringstream ss;
		
		// Obtain the Top Node
		std::map<const DFT::Nodes::Node*, unsigned int>::iterator it = nodeIDs.find(dft->getTopNode());
		assert( (it != nodeIDs.end()) );

		// Generate the Top Node Activate rule
		ss << "A_A";
		DFT::EXPSyncRule* ruleA = new EXPSyncRule(ss.str(),false);
		ss.str("");
		ruleA->label.insert( pair<unsigned int,EXPSyncItem*>(it->second,syncActivate(0,false)) );
		
		// Generate the Top Node Fail rule
		ss << "F_A";
		DFT::EXPSyncRule* ruleF = new EXPSyncRule(ss.str(),false);
		ruleF->label.insert( pair<unsigned int,EXPSyncItem*>(it->second,syncFail(0)) );
		
		// Add the generated rules to the lists
		activationRules.push_back(ruleA);
		failRules.push_back(ruleF);
		
		return 0;
	}
	int DFT::DFTreeEXPBuilder::createSyncRule(vector<DFT::EXPSyncRule*>& activationRules, vector<DFT::EXPSyncRule*>& failRules, const DFT::Nodes::Gate& node, unsigned int nodeID) {
		
		/* Generate non-specific rules for the node */
		
		// Go through all the children
		for(size_t n = 0; n<node.getChildren().size(); ++n) {
			
			// Get the current child and associated childID
			const DFT::Nodes::Node& child = *node.getChildren().at(n);
			std::map<const DFT::Nodes::Node*, unsigned int>::iterator it = nodeIDs.find(&child);
			assert( (it != nodeIDs.end()) && "createSyncRule() was looking for nonexistent node");
			unsigned int childID = it->second;
			
			/** ACTIVATION RULE **/
			{
				// Create labelTo string
				std::stringstream ss;
				ss << "a_" << node.getTypeStr() << nodeID << "_" << child.getTypeStr() << childID;
				EXPSyncRule* ruleA = new EXPSyncRule(ss.str());
				
				// Set synchronization node
				ruleA->syncOnNode = &child;
				
				// Add synchronization of THIS node to the synchronization rule
				ruleA->label.insert( pair<unsigned int,EXPSyncItem*>(nodeID,syncActivate(n+1,true)) );
				
				// Go through all the existing activation rules
				std::vector<DFT::EXPSyncRule*>::iterator ita = activationRules.begin();
				for(;ita != activationRules.end();++ita) {
					
					// If there is a rule that also synchronizes on the same node,
					// we have come across a child with another parent.
					if((*ita)->syncOnNode == &child) {
						EXPSyncRule* otherRuleA = *ita;
						std::cerr << "Found matching rule: " << otherRuleA->toLabel << std::endl;
						
						// The synchronization depends if THIS node uses
						// dynamic activation or not. The prime example of
						// this is the Spare gate.
						if(node.usesDynamicActivation()) {
							
							// If the THIS node used dynamic activation, the
							// node is activated by an other parent.

							// Thus, add a synchronization item to the
							// existing synchronization rule, specifying that
							// the THIS node also receives a sent Activate.
							otherRuleA->label.insert( pair<unsigned int,EXPSyncItem*>(nodeID,syncActivate(n+1,false)) );
							
							// This is not enough, because the other way
							// around also has to be added: the other node
							// wants to listen to Activates of the THIS node
							// as well.
							
							// First, we look up the sending Node of the
							// current activation rule...
							std::map<unsigned int,EXPSyncItem*>::const_iterator it = otherRuleA->label.begin();
							unsigned int otherNodeID = 0;
							unsigned int otherLocalNodeID = 0;
							for(;it != otherRuleA->label.end(); ++it) {
								if(it->second->getArg(1)) {
									otherNodeID = it->first;
									otherLocalNodeID = it->second->getArg(0);
									break;
								}
							}
							
							// ... then we add a synchronization item to the
							// new rule we create for the THIS node, specifying
							// the other node wants to listen to activates of
							// the THIS node.
							std::cerr << "  Adding sync for: " << otherNodeID << std::endl;
							assert( (otherNodeID < otherRuleA->label.size()) && "The other rule sync should have a sending process");
							ruleA->label.insert( pair<unsigned int,EXPSyncItem*>(otherNodeID,syncActivate(otherLocalNodeID,false)) );
							
							// TODO: primary is a special case??????
						} else {
							
							// If the THIS node does not use dynamic
							// activation, we can simply add the THIS node
							// as a sender to the other synchronization rule.
							// FIXME: Possibly this is actually never wanted,
							// as it could allow multiple senders to synchronize
							// with each other.
							otherRuleA->label.insert( pair<unsigned int,EXPSyncItem*>(nodeID,syncActivate(n+1,true)) );
						}
					}
				}
				
				// Add the child Node to the synchronization rule
				// Create synchronization rules a_<nodetype><nodeid>_<childtype><childid>
				ruleA->label.insert( pair<unsigned int,EXPSyncItem*>(childID,syncActivate(0,false)) );
				activationRules.push_back(ruleA);
			}
			
			/** FAIL RULE **/
			{
				// Go through all the existing fail rules
				std::vector<EXPSyncRule*>::iterator itf = failRules.begin();
				bool areOtherRules = false;
				for(;itf != failRules.end();++itf) {
					
					// If there is a rule that also synchronizes on the same node,
					// we have come across a child with another parent.
					if((*itf)->syncOnNode == &child) {
						assert(!areOtherRules && "there should be only one FAIL rule per node");
						areOtherRules = true;
						// We can simply add the THIS node as a sender to
						// the other synchronization rule.
						// FIXME: Possibly this is actually never wanted,
						// as it could allow multiple senders to synchronize
						// with each other.
						(*itf)->label.insert( pair<unsigned int,EXPSyncItem*>(nodeID,syncFail(n+1)) );
					}
				}
				
				// If there are other rules that synchronize on the same node,
				// we do not need to synchronize any further
				// FIXME: This is probably not true!
				if(!areOtherRules) {
					// Add the child Node to the synchronization rule
					// Create synchronization rules a_<nodetype><nodeid>_<childtype><childid>
					std::stringstream ss;
					ss << "f_" << child.getTypeStr() << childID;
					EXPSyncRule* ruleF = new EXPSyncRule(ss.str());
					ruleF->syncOnNode = &child;
					ruleF->label.insert( pair<unsigned int,EXPSyncItem*>(nodeID,syncFail(n+1)) );
					ruleF->label.insert( pair<unsigned int,EXPSyncItem*>(childID,syncFail(0)) );
					failRules.push_back(ruleF);
				}
			}
			
		}
		
		/* Generate node-specific rules for the node */

		switch(node.getType()) {
		case DFT::Nodes::BasicEventType: {
//			const DFT::Nodes::BasicEvent* be = static_cast<const DFT::Nodes::BasicEvent*>(&node);
//			createSyncRuleBE(syncRules,*be,nodeID);
			break;
		}
		case DFT::Nodes::GateType: {
			assert(0 && "A gate should have a specialized type, not the general GateType");
			break;
		}
		case DFT::Nodes::GatePhasedOrType: {
			cc->reportErrorAt(node.getLocation(),"DFTreeEXPBuilder: unsupported gate: " + node.getTypeStr());
			break;
		}
		case DFT::Nodes::GateOrType: {
			const DFT::Nodes::GateOr* g = static_cast<const DFT::Nodes::GateOr*>(&node);
			createSyncRuleGateOr(activationRules,failRules,*g,nodeID);
			break;
		}
		case DFT::Nodes::GateAndType: {
			const DFT::Nodes::GateAnd* g = static_cast<const DFT::Nodes::GateAnd*>(&node);
			createSyncRuleGateAnd(activationRules,failRules,*g,nodeID);
			break;
		}
		case DFT::Nodes::GateHSPType: {
			cc->reportErrorAt(node.getLocation(),"DFTreeEXPBuilder: unsupported gate: " + node.getTypeStr());
			break;
		}
		case DFT::Nodes::GateWSPType: {
			const DFT::Nodes::GateWSP* g = static_cast<const DFT::Nodes::GateWSP*>(&node);
			createSyncRuleGateWSP(activationRules,failRules,*g,nodeID);
			break;
		}
		case DFT::Nodes::GateCSPType: {
		}
		case DFT::Nodes::GatePAndType: {
			const DFT::Nodes::GatePAnd* g = static_cast<const DFT::Nodes::GatePAnd*>(&node);
			createSyncRuleGatePAnd(activationRules,failRules,*g,nodeID);
			break;
		}
		case DFT::Nodes::GateSeqType: {
		}
		case DFT::Nodes::GateVotingType: {
		}
		case DFT::Nodes::GateFDEPType: {
		}
		case DFT::Nodes::GateTransferType: {
			cc->reportErrorAt(node.getLocation(),"DFTreeEXPBuilder: unsupported gate: " + node.getTypeStr());
			break;
		}
		default: {
			cc->reportError("UnknownNode");
			break;
		}
		}
		
		return 0;
	}

	void DFT::DFTreeEXPBuilder::calculateColumnWidths(vector<unsigned int>& columnWidths,const vector<DFT::EXPSyncRule*>& syncRules) {
		for(size_t s=0; s<syncRules.size(); ++s) {
			const DFT::EXPSyncRule& rule = *syncRules.at(s);
			std::map<unsigned int,EXPSyncItem*>::const_iterator it = rule.label.begin();
			for(;it!=rule.label.end();++it) {
				assert( (0<=it->first) );
				assert( (it->first<columnWidths.size()) );
				std::string s = it->second->toStringQuoted();
				if(s.length() > columnWidths[it->first]) {
					columnWidths[it->first] = s.length();
				}
			}
		}
	}
