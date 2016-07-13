# Author: Jamie Davis (davisjam@vt.edu)
# Description: Python description of libuv Callbacks
# Defines the following public classes: 
# 	Callback
#  	CallbackTree
# Python version: 2.7.6

import re
import logging

#############################
# CallbackNodeGroups
#############################

#You specify a file containing groups of node IDs
#We parse it.
class CallbackNodeGroups(object):
	def __init__(self, groupFile):
		self.nodeGroups = self._parseGroupFile(groupFile)

	def getNodeGroups(self):
		return self.nodeGroups

	# input: (groupFile)
	# 	Sample file contents:
	# Group 1
	# 2
	# 3
	# Group 2
	# 4
	# 7
	# 9
	# Group 3
	# ...
	# output: List of lists. Each list is of node IDs and corresponds to a group of nodes.
	#         Each list is a set (no duplicates)
	# Throws any errors it encounters during file IO
	def _parseGroupFile(self, groupFile):
		nodeGroups = []
		foundGroup = False
		with open(groupFile) as f:
			group = None
			for line in f:
				line = line.rstrip()
				logging.info("CallbackNodeGroups::_parseGroupFile: line <{}>".format(line))
				# Ignore whitespace and lines beginning with a #
				if re.match('^\s*$', line) or re.match('^\s*#', line):
					continue

				# New group?
				if re.match('^\s*Group \d', line, re.IGNORECASE):
					foundGroup = True
					if group is not None:
						# Save the current group
						logging.debug("CallbackNodeGroups::_parseGroupFile: adding group <{}>".format(group))
						nodeGroups.append(group)
					group = []
				else:
					# Must be a node ID. Add to the current group.
					assert(re.match('^\s*\d+\s*$', line))
					assert(group is not None)
					nodeID = int(line.rstrip())
					if nodeID not in group:
						group.append(int(line.rstrip()))

			# Clean up final group
			if (group is not None):
				logging.debug("CallbackNodeGroups::_parseGroupFile: adding group <{}>".format(group))
				nodeGroups.append(group)

		assert(foundGroup)
		return nodeGroups

#############################
# CallbackNode
#############################

# Representation of a Callback Node from libuv
# self.REQUIRED_KEYS describes the members set in the constructor
# Other members that can be set via methods:	children, parent
# NB All fields returned by getX are strings
class CallbackNode (object):
	REQUIRED_KEYS = ["name", "context", "context_type", "cb_type", "cb_behavior", "tree_number", "tree_level", "level_entry", "exec_id", "reg_id", "callback_info", "registrar", "tree_parent", "registration_time", "start_time", "end_time", "executing_thread", "active", "finished", "extra_info", "dependencies"]

	TP_WORK_INITIAL_TYPES = ["UV_WORK_CB"]
	TP_WORK_NESTED_TYPES = ["UV_FS_WORK_CB", "UV_GETADDRINFO_WORK_CB", "UV_GETNAMEINFO_WORK_CB"]
	TP_WORK_TYPES = TP_WORK_INITIAL_TYPES + TP_WORK_NESTED_TYPES

	TP_DONE_INITIAL_TYPES = ["UV_AFTER_WORK_CB"] # TODO Also UV_FS_EVENT_CB, but that can come directly from IO poll too?
	TP_DONE_NESTED_TYPES = ["UV_FS_CB", "UV_GETADDRINFO_CB", "UV_GETNAMEINFO_CB"]
	TP_DONE_TYPES = TP_DONE_INITIAL_TYPES + TP_DONE_NESTED_TYPES

  # The types of all CBNs that might be invoked related to the threadpool,
  # either by a threadpool thread or by the looper thread handling 'done' items.
	TP_TYPES = TP_WORK_TYPES + TP_DONE_TYPES

	# UV_ASYNC_CB is not included here because its "async" behavior is actually well defined.
	# This matters if the application makes use of its own uv_async objects.
	# The internal uv_async associated with the TP can be manipulated freely.
	#
	# Network-driven asynchronous events (e.g. UV_READ_CB or UV_CONNECT[ION]_CB are also not included, because
	# changing when those occur relative to other nodes runs up against nodejsrr's requirement that all external inputs
	# be repeated precisely. We can, however, change the order of async events AROUND them.
	MISC_ASYNC_TYPES = ["UV_TIMER_CB"]

	ASYNC_TYPES = TP_TYPES + MISC_ASYNC_TYPES

	TIME_KEYS = ["registration_time", "start_time", "end_time"]
	
	# CallbackString (a string of fields formatted as: 'Callback X: | <key> <value> | <key> <value> | .... |'
	#   A CallbackString must a key-value pair for all of the members of self.REQUIRED_KEYS
	#     'start', 'end' must have value of the form '<Xs Yns>' 
	def __init__(self, callbackString=""):
		assert(0 < len(callbackString))
		self.callbackString = callbackString
		kvs = callbackString.split("|")
		for kv in kvs:
			match = re.search('<(?P<key>.*?)>\s+<(?P<value>.*)>', kv)
			if (match):
				setattr(self, match.group('key'), match.group('value'))
		for key in self.REQUIRED_KEYS:
			#logging.debug("CallbackNode::__init__: Verifying that required field '{}' is defined".format(key))
			value = getattr(self, key, None)
			assert(value is not None)
			#logging.debug("CallbackNode::__init__: '%s' -> '%s'" %(key, value)) 					
						
		# Convert times in s,ns to ns
		for timeKey in CallbackNode.TIME_KEYS:
			timeStr = getattr(self, timeKey, None)
			match = re.search('(?P<sec>\d+)s\s+(?P<nsec>\d+)ns', timeStr)
			assert(match)
			#TODO Is all of this long()'ing necessary? I'm guessing we only need it for long(1e9)
			ns = long(long(match.group('sec'))*long(1e9) + long(match.group('nsec')))
			setattr(self, timeKey, ns)
		# Time should go forward
		if (self.executed()):
			assert(self.registration_time <= self.start_time)
			assert(self.start_time <= self.end_time)
		
		# Convert 'dependencies' to a list of CallbackNode names
		# The owner may change self.dependencies to a list of CallbackNodes if he has a mapping from names to nodes.
		if (len(self.dependencies)):
			self.dependencies = self.dependencies.split(" ")
		else:
			self.dependencies = []
		#logging.debug("CallbackNode::__init__: dependencies {}".format(self.dependencies))
				
		self.children = []
		self.dependents = []
		self.parent = None

		#Used to eliminate duplicate computation in self.getDescendants
		self._knowAllDescendantsWithDependents = False
		self._allDescendantsWithDependents = []
		self._knowAllDescendantsWithoutDependents = False
		self._allDescendantsWithoutDependents = []

		#logging.debug("CallbackNode::__init__: {}".format(self))

	def __str__(self):
		kvStrings = []
		for key in self.REQUIRED_KEYS:
			kvStr = "<{}>".format(key)
			if key in CallbackNode.TIME_KEYS:
				# Convert times in ns to s,ns
				#TODO Is all of this long()'ing necessary? I'm guessing we only need it for long(1e9)
				time_ns = long(getattr(self, key))
				nsPerS = long(1e9)
				kvStr += " <{:d}s {:d}ns>".format(time_ns/nsPerS, long(time_ns % nsPerS))
			elif key == "dependencies":
				# Include dependencies. self.dependencies might be either a list of names or a list of CallbackNodes.
				# TODO This code would be cleaner if we clarified the contents of that list. It shouldn't be one of two types.
				deps = getattr(self, key)
				depStrs = []
				for d in deps:
					if type(d) == CallbackNode:
						depStrs.append(d.getName())
					else:
						depStrs.append(str(d))
				kvStr += " <{}>".format(" ".join(depStrs))
			else:
				kvStr += " <{}>".format(getattr(self, key))

			kvStrings.append(kvStr)
		string = " | ".join(kvStrings)
		return string
	
	#setParent(parent)
	# - Set self's parent member (a CallbackNode) to PARENT for tree traversal
	# - Update self's tree_parent field (the name of the parent)
	# - Update self's registrar field (the name of the parent)
	def setParent (self, parent):
		assert(not parent or isinstance(parent, CallbackNode))
		self.parent = parent
		if parent:
			parentName = parent.getName()
		else:
			parentName = "NONE"
		self.tree_parent, self.registrar = parentName, parentName
	
	#getParent()
	#Returns the parent of this CBN. Must have been set using setParent.		
	def getParent (self):
		return self.parent
		
	#addChild(child)
	#Adds CHILD to self's list of children
	def addChild (self, child):
		assert(isinstance(child, CallbackNode))

		logging.debug("me {}: tree_number {} tree_level {}; child {}: tree_number {} tree_level {}".format(self, self.tree_number, self.tree_level, child, child.tree_number, child.tree_level))
		# Must be in same tree
		assert(int(self.tree_number) == int(child.tree_number))
		# Must be a direct child
		assert(int(self.tree_level) + 1 == int(child.tree_level))

		self.children.append(child)

	def addDependent(self, dependent):
		self.dependents.append(dependent)

	def getDependents(self):
		return self.dependents

	# input: (maybeChild)
	# output: (True/False)
	#
	# Remove maybeChild from this node if it was a child; returns True if we did anything
	def removeChild (self, maybeChild):
		origLen = len(self.children)
		self.children = [c for c in self.children if c is not maybeChild]
		return (origLen == len(self.children))

	# input: ()
	# output: ()
	# Remove all children
	def remChildren (self):
		self.setChildren([])

	def getChildren(self):
		return self.children

	# Return this node's list of dependencies
	# This will either be a list of strings or (if via CallbackTree) a list of CallbackNodes
	def getDependencies(self):
		return self.dependencies

	def setChildren(self, children):
		self.children = children

	#getTreeRoot()
	#Returns the CallbackNode at the root of the tree
	def getTreeRoot (self):
		curr = self
		parent = curr.getParent()
		while (parent is not None):
			curr = parent
			parent = curr.getParent()
		return curr
		
	# input (potentialDescendant, includeDependencies)
	#	potentialDescendant: CallbackNode that is a potential descendant of self
	# includeDependents: Flag -- check only parent-child (registration) relationships, or also include program order (dependency) relationships?
	# output (True/False)
	#True if potentialDescendant is a descendant (child, grand-child, etc.) of self, else False
	def isAncestorOf (self, potentialDescendant, includeDependents=False):
		assert(isinstance(potentialDescendant, CallbackNode))

		nodesToCheck = self.children
		if includeDependents:
			nodesToCheck += self.dependents
		assert(self not in nodesToCheck)

		for node in nodesToCheck:
			if (node.getRegID() == potentialDescendant.getRegID()):
				return True
			if (node.isAncestorOf(potentialDescendant, includeDependents)):
				return True
		return False

	# input: (includeDependents)
	# includeDependents: Flag -- include only parent-child (registration) relationships, or also include program order (dependency) relationships?
	# output: (descendants) list of CallbackNodes
	# Returns the Set of all nodes (children, grand-children, etc.) descended from this node
	def getDescendants(self, includeDependents):
		# Short-circuit if we know the answer already
		if includeDependents and self._knowAllDescendantsWithDependents:
			return self._allDescendantsWithDependents
		elif not includeDependents and self._knowAllDescendantsWithoutDependents:
			return self._allDescendantsWithoutDependents

		directDescendants = self.children
		if includeDependents:
			directDescendants += self.dependents
		directDescendants = set(directDescendants) # No sense in visiting a child more than once
		assert (self not in directDescendants)  # Sanity check: avoid infinite recursion
		logging.info("CallbackNode::getDescendants: node {} calculating dependents of my {} children ({})".format(self.getID(), len(directDescendants), [n.getID() for n in directDescendants]))

		indirectDescendants = []
		for d in directDescendants:
			logging.info("CallbackNode::getDescendants: node {} calculating dependents of {}".format(self.getID(), d.getID()))
			indirectDescendants += d.getDescendants(includeDependents)
			logging.info("CallbackNode::getDescendants: node {} descendant {}: includeDependents {}, directDescendants {}, {} indirectDescendants: {}".format(self.getID(), d.getID(), includeDependents, [n.getID() for n in directDescendants], len(indirectDescendants), [n.getID() for n in indirectDescendants]))
		allDescendants = directDescendants | set(indirectDescendants) # There may be duplicates
		logging.info("CallbackNode::getDescendants: node {}, allDescendants {}".format(self.getID(), [n.getID() for n in allDescendants]))

		# Save state so we don't have to recurse
		if includeDependents:
			self._knowAllDescendantsWithDependents = True
			self._allDescendantsWithDependents = allDescendants
		else:
			self._knowAllDescendantsWithoutDependents = True
			self._allDescendantsWithoutDependents = allDescendants

		return allDescendants

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

	def setName(self, name):
		self.name = name
	
	def getExecID (self):
		return self.exec_id

	def setExecID(self, newID):
		self.exec_id = str(newID)
	
	def getRegID (self):
		return self.reg_id
	
	def setRegID (self, newID):
		self.reg_id = str(newID)

	# This is an "external" ID suitable for identifying events in a Schedule
	def getID (self):
		return self.getExecID()

	def getTreeParent (self):
		return self.tree_parent
	
	def getCBType (self):
		return self.cb_type
	
	def setCBType (self, type):
		self.cb_type = type
	
	def getContext (self):
		return self.context_type
	
	def getTreeNumber (self):
		return self.tree_number
		
	def getTreeLevel (self):
		return self.tree_level
	
	def setTreeLevel (self, treeLevel):
		self.tree_level = treeLevel		
		
	def getLevelEntry (self):
		return self.level_entry

	def isMarkerNode (self):
		if (re.search('^MARKER_.*', self.getCBType())):
			return True
		else:
			return False

	# Returns True if this CBN was executed by a threadpool thread.
	def isThreadpoolCB (self):		
		return (self.getCBType() in CallbackNode.TP_WORK_TYPES)
		
	def isRunTimersCB (self):		
		validOptions = ["UV_TIMER_CB"]
		return (self.getCBType() in validOptions)

	def isRunPendingCB (self):		
		validOptions = ["UV_WRITE_CB"]
		return (self.getCBType() in validOptions)	
		
	def isRunIdleCB (self):		
		validOptions = ["UV_IDLE_CB"]
		return (self.getCBType() in validOptions)
		
	def isRunPrepareCB (self):		
		validOptions = ["UV_PREPARE_CB"]
		return (self.getCBType() in validOptions)

	def isIOPollCB (self):
		cbType = self.getCBType()
		
		#TODO This is a hack.
		if (cbType != "MARKER_IO_POLL_END"):
			return True
		else:
			return False
		
	def isRunCheckCB (self):		
		validOptions = ["UV_CHECK_CB"]
		return (self.getCBType() in validOptions)

	def _legal_uv__stream_destroy_cbs (self):
		return ["UV_CONNECT_CB"] + self._legal_uv__write_callbacks() + ["UV_SHUTDOWN_CB"]

	def _legal_uv__write_callbacks (self):
		return ["UV_WRITE_CB"]

	def _legal_udp_finish_close_cbs (self):
		return self._legal_udp_run_completed_cbs()

	def _legal_udp_run_completed_cbs (self):
		return ["UV_UDP_SEND_CB"]

	def _legal_uv__finish_close_cbs (self):
		# See uv/src/unix/core.c
		return self._legal_uv__stream_destroy_cbs() + self._legal_udp_finish_close_cbs() + ["UV_CLOSE_CB"]

	def isRunClosingCB (self):
		validOptions =  self._legal_uv__finish_close_cbs()
		return (self.getCBType() in validOptions)
					
	def getExtraInfo (self):
		return self.extra_info

	# Return True if this is user code
	# Else False -- marker nodes or internal libuv mechanisms that just "look like" user code (e.g. the UV_ASYNC_CB used for the threadpool)
	def isUserCode (self):		
		if (self.isMarkerNode() or re.search('non-user', self.getExtraInfo())):
			return False
		else:
			isUserCodeInLooper = (self.isRunTimersCB() or self.isRunPendingCB() or self.isRunIdleCB() or self.isRunPrepareCB() or self.isIOPollCB() or self.isRunCheckCB() or self.isRunClosingCB() or self.isRunTimersCB())
			assert(self.isThreadpoolCB() or isUserCodeInLooper)
			return True

	# Returns True if this is an async CBN, else False
	def isAsync (self):
		return (self.getCBType() in CallbackNode.ASYNC_TYPES)

	# Returns the nearest reschedulable (async) node, ancestrally speaking, possibly including self
	# Returns None if no such node was found
	def findNearestReschedulable(self):
		if self.isAsync():
			return self.getCBType() not in CallbackNode.TP_WORK_NESTED_TYPES and self.getCBType() not in CallbackNode.TP_DONE_NESTED_TYPES
		elif self.getParent() is not None:
			return self.getParent().findNearestReschedulable()
		logging.debug("CallbackNode::findNearestReschedulable: Sorry, ran out of parents. This node is not reschedulable at all.")
		return None

#############################
# CallbackNodeTree
#############################

#Representation of a tree of CallbackNodes from libuv
#Members:   root    root node of the tree
class CallbackNodeTree (object):
	#inputFile must contain lines matching the requirements of the constructor for CallbackNode, one CallbackNode per line
	def __init__ (self, inputFile):
		callbackNodes = []
		try:
			with open(inputFile) as f:
				lines = f.readlines()
				for l in lines:
					node = CallbackNode(l)
					callbackNodes.append(node)
		except IOError:
			logging.error("CallbackNodeTree::__init__: Error, processing inputFile {} gave me an IOError".format(inputFile))
			raise

		callbackNodeDict = { n.name: n for n in callbackNodes }

		# Set the parent-child relationship for each node
		self.root = None
		for node in callbackNodes:
			logging.debug("Setting parent-child relationship for node {}".format(node))
			if (node.registrar in callbackNodeDict):
				parent = callbackNodeDict[node.registrar]
				logging.debug("node {} has registrar {}; adding node as child of parent {}".format(node, node.registrar, parent))
				parent.addChild(node)
				node.setParent(parent)
			else:
				assert(node.getCBType() == "INITIAL_STACK")
				assert(not self.root)
				self.root = node

		# Convert dependencies (strings) to dependencies (CallbackNodes)
		self._updateDependencies()

		# Tree must be valid
		assert(self.root)		
		for node in callbackNodes:
			assert(node.getTreeRoot() == self.root)
		assert(self.isValid())

	# input: ()
	# output: ()
	#
	# Repair this CallbackTree after the addition or removal of nodes using CallbackNode APIs.
	#   - recompute registration IDs
	# The exec IDs of the nodes in the tree MUST be correct prior to invoking this function.
	def repairAfterUpdates(self):
		# Repair registration IDs by simulating execution.
		# This relies on correct execution IDs.
		logging.debug("Repairing registration IDs")		
		
		# Helper function: simulate the execution of node by setting the regID of each of its children.
		# Returns the next unused regID.
		def simulateNodeExecution(node, nextRegID):
			# Register any children
			for child in node.getChildren():
				child.setRegID(nextRegID)
				nextRegID += 1
				
				# Verify that child's regID seems valid.
				if child.getParent() is not None:
					parentRegID = int(child.getParent().getRegID())
					childRegID = int(child.getRegID())
					assert(parentRegID < childRegID)
				else:
					assert(childRegID == 0)
			return nextRegID

		execOrder = [cb for cb in self.getExecOrder() if cb.executed()]
		rootNode = execOrder[0]
		assert(rootNode.getCBType() == "INITIAL_STACK")
		self.root = rootNode

		nextRegID = 1
		for cbn in execOrder:
			nextRegID = simulateNodeExecution(cbn, nextRegID)

		assert(self.isValid())
	
	# input: ()
	# output: (callbackNodeDict)
	#   callbackNodeDict     maps CallbackNode.name to CallbackNode for all nodes in this CallbackNodeTree
	#
	# Walks the tree starting at the root to generate this dictionary
	def _genCallbackNodeDict(self):
		callbackNodeDict = {}
		def walkFunc (node, dict):
			dict[node.name] = node
		self.walk(walkFunc, callbackNodeDict)
		return callbackNodeDict		
	
	# Replace the self.dependencies array of string node names in each Node with an array of the corresponding CallbackNodes (update node.dependencies).
	# Inform each antecedent about its dependent (append to each dependency's node.dependents).
	def _updateDependencies(self):
		callbackNodeDict = self._genCallbackNodeDict()
		for node in callbackNodeDict.values():
			# dependencies must be strings at this point
			for d in node.dependencies:
				assert(type(d) is str)
				logging.debug("CallbackNodeTree::_updateDependencies: dependency {}".format(d))
			# Turn node.dependencies from a list of strings to a list of CBNs.
			node.dependencies = [callbackNodeDict[n] for n in node.dependencies]
			for antecedent in node.dependencies:
				antecedent.addDependent(node)
			logging.debug("CallbackNodeTree::_updateDependencies: Node {}'s dependencies: {}".format(node.getName(), node.dependencies))
		
	#input: (regID)
	#output: (node) node with specified regID, or None
	def getNodeByRegID(self, regID):
		for node in self._genCallbackNodeDict().values():
			if (node.getRegID() == str(regID)):
				return node
		return None
	
	#input: (execID)
	#output: (node) node with specified execID, or None
	def getNodeByExecID(self, execID):
		for node in self._genCallbackNodeDict().values():
			if (node.getExecID() == str(execID)):
				return node
		return None
		
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
				logging.debug("CallbackNodeTree::removeNodes: removing node: {}".format(node))
				if (node.parent):
					node.parent.removeChild(node)
					node.parent = None
		self.walk(remove_walk, None)

	# input: ()
	# output: (isValid)
	#   isValid       True or False
	#
	# Assess this tree for validity
	#    - Nodes have an exec ID preceding their children and dependents
	#    - Each node's tree_level is one higher than its parent's
	def isValid(self):

		invalidNodes = []
		# invalidNodes: list of nodes for which something is not correct
		def valid_walkFunc (node, invalidNodes):
			#logging.debug("Assessing node {} (type {}) for validity".format(node, node.getCBType()))
			isInvalid = False
			childrenAndDependents = [n for n in node.getChildren() + node.getDependents()]
			treeLevel = int(node.getTreeLevel())
			for child in childrenAndDependents:
				# This is imprecise, but nested nodes have the same tree level.
				possibleTreeLevels = [treeLevel, treeLevel+1]
				if int(child.getTreeLevel()) not in possibleTreeLevels:
					logging.debug("My child or dependent cbn (type {}) has tree_level {}; valid tree levels {}; my child cannot precede me structurally".format(child.getCBType(), child.getTreeLevel(), possibleTreeLevels))
					isInvalid = True
					break
				if child.executed() and int(child.getExecID()) < int(node.getExecID()):
					logging.debug("My child or dependent cbn (type {}) has execID {} < my execID {}; my child cannot precede me executionally".format(child.getCBType(), child.getExecID(), node.getExecID()))
					isInvalid = True
					break

			if isInvalid:
				invalidNodes.append(node)

		self.walk(valid_walkFunc, invalidNodes)
		if invalidNodes:
			logging.debug("Found {} invalid nodes in tree of size {}".format(len(invalidNodes), len(self._genCallbackNodeDict().keys())))
			return False

		return True

	# Return the tree nodes
	def getTreeNodes (self):
		return self._genCallbackNodeDict().values()

	# input: (node)
	# output: (nodeIsInThisTree) True or False
	#
	# Determines whether or not node is in the tree by climbing
	# until we get to the root of this tree, or None
	def contains (self, node):
		while node:
			if (node.getName() == self.root.getName()):
				return True
			node = node.getParent()
		return False

	# Return the tree nodes in execution order
	# The returned list begins with any unexecuted nodes, whose execID is -1 
	def getExecOrder (self):
		return sorted(self.getTreeNodes(), key=lambda node: int(node.getExecID()))

	# Return the tree nodes in registration order
	def getRegOrder(self):
		return sorted(self.getTreeNodes(), key=lambda node: int(node.getRegID()))

	# Return the node preceding NODE in the tree execution order
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
		assert("CallbackNodeTree::getNodeExecPredecessor: Error, did not find node with execID {} in tree".format(node.getExecID()))

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
