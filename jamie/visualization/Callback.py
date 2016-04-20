#Author: Jamie Davis (davisjam@vt.edu)
#Description: Python description of libuv Callbacks
# Defines the following public classes: 
#	 Callback
#  CallbackTree
# Python version: 2.7.6

import re
import logging

#############################
# CallbackNode
#############################

#Representation of a Callback Node from libuv
#self.REQUIRED_KEYS describes the members set in the constructor
#Other members that can be set via methods:	children, parent		
class CallbackNode (object):
	REQUIRED_KEYS = ["name", "context", "context_type", "cb_type", "cb_behavior", "tree_number", "tree_level", "level_entry", "exec_id", "reg_id", "callback_info", "registrar", "tree_parent", "registration_time", "start_time", "end_time", "executing_thread", "active", "finished", "extra_info", "dependencies"]
	
	# CallbackString (a string of fields formatted as: 'Callback X: | <key> <value> | <key> <value> | .... |'
	#   A CallbackString must a key-value pair for all of the members of self.REQUIRED_KEYS
	#     'start', 'end' must have value of the form '<Xs Yns>' 
	def __init__ (self, callbackString=""):
		assert(0 < len(callbackString))
		kvs = callbackString.split("|")
		for kv in kvs:
			match = re.search('<(?P<key>.*?)>\s+<(?P<value>.*)>', kv)
			if (match):
				setattr(self, match.group('key'), match.group('value'))
		for key in self.REQUIRED_KEYS:
			logging.debug("CallbackNode::__init__: Verifying that required field '%s' is defined" % (key))
			value = getattr(self, key, None)
			assert(value is not None)
			logging.debug("CallbackNode::__init__: '%s' -> '%s'" %(key, value)) 					
						
		#convert times in s,ns to ns
		for timeKey in ["registration_time", "start_time", "end_time"]:
			timeStr = getattr(self, timeKey, None)
			match = re.search('(?P<sec>\d+)s\s+(?P<nsec>\d+)ns', timeStr)
			assert(match)
			ns = int(match.group('sec'))*1e9 + int(match.group('nsec'))
			setattr(self, timeKey, ns)
		#time should go forward
		if (self.executed()):
			assert(self.registration_time <= self.start_time)
			assert(self.start_time <= self.end_time)
		
		#convert 'dependencies' to a list of CallbackNode names
		if (len(self.dependencies)):
			self.dependencies = self.dependencies.split(" ")
		else:
			self.dependencies = []
		logging.debug("CallbackNode::__init__: dependencies %s" % (self.dependencies))
		
		self.children = []
		self.parent = None
		

	def __str__ (self):
		string = "Callback node: "
		for key in self.REQUIRED_KEYS:
			string += "<%s> <%s> |" % (key, getattr(self, key, None))
		return string
	
	#setParent(parent)
	#Set self's parent member to PARENT		
	def setParent (self, parent):
		assert(isinstance(parent, CallbackNode))
		self.parent = parent
	
	#getParent()
	#Returns the parent of this CBN. Must have been set using setParent.		
	def getParent (self):
		return self.parent
		
	#addChild(child)
	#Adds CHILD to self's list of children
	def addChild (self, child):
		assert(isinstance(child, CallbackNode))
		#must be in same tree
		assert(int(self.tree_number) == int(child.tree_number))
		#must be a direct child
		assert(int(self.tree_level) + 1 == int(child.tree_level))
		self.children.append(child)
		
	#getTreeRoot()
	#Returns the CallbackNode at the root of the tree
	def getTreeRoot (self):
		curr = self
		parent = curr.getParent()
		while (parent):
			curr = parent
			parent = curr.getParent()
		return curr
		
	#isDescendant(other)
	#True if OTHER is a descendant (child, grand-child, etc.) of self, else false
	def isDescendant (self, other):
		assert(isinstance(other, CallbackNode))
		for child in self.children:
			if (child.reg_id == other.reg_id):
				return True
			if (child.isDescendant(other)):
				return True
		return False		
	
	# == and != based on name
	def __eq__ (self, other):
		assert(isinstance(other, CallbackNode))
		return (self.name == other.name)
		
	def __ne__ (self, other):
		assert(isinstance(other, CallbackNode))
		return (self.name != other.name)
	
	#returns true if this node was executed or being executed, else false
	def executed (self):
		return (int(self.active) != 0 or int(self.finished) != 0)
		
	def getExecutingThread (self):
		return self.executing_thread
	
	def getBehavior (self):
		return self.cb_behavior
	
	def getRegistrationTime (self):
		return self.registration_time
	
	def getStartTime (self):
		return self.start_time
	
	def getEndTime (self):
		return self.end_time
			
	def getName (self):
		return self.name
	
	def getExecID (self):
		return self.exec_id
	
	def getRegID (self):
		return self.reg_id
	
	def getCBType (self):
		return self.cb_type
	
	def getContext (self):
		return self.context_type
	
	def getTreeNumber (self):
		return self.tree_number
		
	def getTreeLevel (self):
		return self.tree_level
		
	def getLevelEntry (self):
		return self.level_entry

	def isMarkerNode (self):
		if (re.search('^MARKER_.*', self.getCBType())):
			return True
		else:
			return False

	def getExtraInfo (self):
		return self.extra_info

	def getChildren (self):
		return self.children


#############################
# CallbackNodeTree
#############################

#Representation of a tree of CallbackNodes from libuv
#Members: callbackNodes (nodes of the tree; list of CallbackNode)
#					callbackNodeDict (maps CallbackNode.name to CallbackNode)
#					root (root node of the tree)
class CallbackNodeTree (object):
	#inputFile must contain lines matching the requirements of the constructor for CallbackNode, one CallbackNode per line
	def __init__ (self, inputFile):
		#construct callbackNodes
		self.callbackNodes = []
		with open(inputFile) as f:
			lines = f.readlines()
			for l in lines:
				node = CallbackNode(l)
				self.callbackNodes.append(node)
				
		#construct callbackNodeDict
		self.callbackNodeDict = {}
		for node in self.callbackNodes:
			self.callbackNodeDict[node.name] = node
			
		#Set the parent-child relationship for each node
		self.root = None
		for node in self.callbackNodes:
			if (node.registrar in self.callbackNodeDict):
				parent = self.callbackNodeDict[node.registrar]
				parent.addChild(node)
				node.setParent(parent)
			else:
				assert(not self.root)
				self.root = node
				
		#Does the tree look valid?
		assert(self.root)		
		for node in self.callbackNodes:
			assert(node.getTreeRoot() == self.root)
		
		self._updateDependencies()
	
	#Replace the array of string node names in each Node with an array of the corresponding CallbackNodes 
	def _updateDependencies (self):
		for node in self.callbackNodes:
			for n in node.dependencies:
				logging.debug("CallbackNodeTree::_updateDependencies: dependency %s" % (n))
			#logging.debug("CallbackNodeTree::_updateDependencies: callbackNodeDict %s" % (self.callbackNodeDict))
			node.dependencies = [self.callbackNodeDict[n] for n in node.dependencies]
			logging.debug("CallbackNodeTree::_updateDependencies: Node %s's dependencies: %s" % (node.getName(), node.dependencies))
		
	#walk(node, func, funcArg)
	#apply FUNC to each member of the tree, starting at NODE (defaults to tree root)
	#FUNC will be invoked as FUNC(callbackNode, FUNCARG)	
	def walk (self, func, funcArg, node=None):
		if (not node):
			node = self.root
		func(node, funcArg)
		for child in node.children:			
			self.walk(func, funcArg, node=child)
			
	#removeNodes(func)
	#remove all nodes from this tree for which FUNC evaluates to True 
	def removeNodes (self, func):		
		#Wrapper for self.walk 
		def remove_walk (node, arg):
			if (func(node)):
				logging.debug("CallbackNodeTree::removeNodes: removing node: %s" % (node))
				#Remove NODE from the tree
				if (node.parent):					
					node.parent.children = [x for x in node.parent.children if x.name != node.name]
					node.parent = None
				
		self.walk(remove_walk, None)
		self.callbackNodes = [n for n in self.callbackNodes if self.contains(n)]
	
	#return the tree nodes
	def getTreeNodes (self):
		return self.callbackNodes

	def contains (self, node):
		if (not node):
			return False
		assert (isinstance(node, CallbackNode))
		if (node.getName() == self.root.getName()):
			return True
		else:
			return self.contains(node.getParent())

	#return the tree nodes in execution order
	def getExecOrder (self):
		return sorted(self.getTreeNodes(), key=lambda node: int(node.getExecID()))
	
	#return the node preceding NODE in the tree execution order
	def getNodeExecPredecessor (self, node):
		execOrder = self.getExecOrder()
		if (int(node.getExecID()) <= 0):
			return None
		
		pred = execOrder[0]
		for curr in execOrder:
			if (curr.getExecID() == node.getExecID()):
				assert(pred != curr)
				return pred
			pred = curr
		assert("CallbackNodeTree::getNodeExecPredecessor: Error, did not find node with execID %s in tree" % (node.getExecID()))
	
	# #Transform the tree into an intermediate format for more convenient schedule re-arrangement.
	# #This may add new nodes and change the exec_id of many nodes.
	# #The external behavior of the application under the original and unwrapped schedules should match.
	# def expandSchedule (self):
	# 	return
	#
	# 	#convert a single ASYNC_CB followed by multiple AFTER_WORK_CB's into a series of
	# 	#ASYNC_CB's each followed by one AFTER_WORK_CB
	# 	nodesExecOrder = self.getExecOrder()
	# 	#These are the AFTER_WORK_CBs preceded by other AFTER_WORK_CBs.
	# 	#assert(not "TODO Need to test for UV_AFTER_WORK_CB or any of its nested dependents")
	# 	nodesNeedingWrapper = [n for n in nodesExecOrder if n.getCBType() == 'UV_AFTER_WORK_CB' and self.getNodeExecPredecessor(n).getCBType() == 'UV_AFTER_WORK_CB']
	#
	# 	#loop invariant: at the beginning of each iter, NODE must be preceded by an AFTER_WORK_CB that is preceded by a UV_ASYNC_CB
	# 	for node in nodesNeedingWrapper:
	# 		pred = node.getNodeExecPredecessor()
	# 		async = pred.getNodeExecPredecessor()
	# 		assert(pred.getCBType() == 'UV_AFTER_WORK_CB')
	# 		assert(async.getCBType() == 'UV_ASYNC_CB')
	# 		logging.debug("LOOKS OK")
			
	def write (self, fileName):
		assert(not "CallbackNodeTree::write: Not yet implemented")
