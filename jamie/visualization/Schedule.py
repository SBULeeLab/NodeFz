#Author: Jamie Davis (davisjam@vt.edu)
#Description: Python description of libuv callback schedule
# Defines the following public classes: 
#	 Schedule
# Defines the following exceptions: 
#	ScheduleExceptio
# Python version: 2.7.6

import re
import logging
import Callback as CB


#############################
# ScheduleException
#############################

class ScheduleException(Exception):
	pass

#############################
# Schedule
#############################

#Representation of a libuv callback schedule	
class Schedule (object):
	# CBs of these types are asynchronous, and can be moved from one loop to another provided that they do not violate happens-before order
	ASYNC_CB_TYPES = ["UV_TIMER_CB", "UV_WORK_CB", "UV_AFTER_WORK_CB"]
	# Order in which marker events occur
	LIBUV_LOOP_STAGE_MARKER_ORDER = []
		
	LIBUV_RUN_STAGES = ["RUN_TIMERS_1", "RUN_PENDING", "RUN_IDLE", "RUN_PREPARE", "IO_POLL", "RUN_CHECK", "RUN_CLOSING", "RUN_TIMERS_2"]
	LIBUV_STAGES = ["UV_RUN"] + LIBUV_RUN_STAGES
	MARKER_EVENT_TO_STAGE = {} # Map LIBUV_LOOP_STAGE_MARKER_ORDER elements to one of LIBUV_STAGES
	
	# MARKER_UV_RUN_BEGIN begins
	runBegin, runEnd = ("MARKER_%s_BEGIN" % ("UV_RUN"), "MARKER_%s_END" % ("UV_RUN"))
	LIBUV_LOOP_STAGE_MARKER_ORDER.append(runBegin)
	MARKER_EVENT_TO_STAGE[runBegin] = "UV_RUN"
	
	# Followed by begin/end for each of the UV_RUN stages
	for stage in LIBUV_RUN_STAGES:
		begin, end = ("MARKER_%s_BEGIN" % (stage), "MARKER_%s_END" % (stage))				
		LIBUV_LOOP_STAGE_MARKER_ORDER.append(begin)
		LIBUV_LOOP_STAGE_MARKER_ORDER.append(end)
		MARKER_EVENT_TO_STAGE[begin] = stage
		MARKER_EVENT_TO_STAGE[end] = stage
		
	# MARKER_UV_RUN_END follows
	LIBUV_LOOP_STAGE_MARKER_ORDER.append(runEnd)
	MARKER_EVENT_TO_STAGE[runEnd] = "UV_RUN"
		
	 
	def __init__ (self, scheduleFile):
		logging.debug("Schedule::__init__: scheduleFile %s" % (scheduleFile))
		self.scheduleFile = scheduleFile
		
		self.cbTree = CB.CallbackNodeTree(self.scheduleFile)
		
		self.markerEventIx = 0 # The next marker event should be this ix in LIBUV_LOOP_STAGE_MARKER_ORDER
		self.currentLibuvRunStage = None # Tracks the marker event we're currently processing. Either one of LIBUV_RUN_STAGES or None.		
		
		self.assertValid()
		
		self.prevCB = None
		self.exiting = False
		
	# input: ()
	# output: (execList) returns list of CallbackNodes in increasing execution order. Unexecuted CBs are omitted
	def _execList (self):
		return [cb for cb in self.cbTree.getExecOrder() if cb.executed()]

	# input: ()
	# output: (regList) returns list of CallbackNodes in increasing registration order.
	def _regList (self):
		return self.cbTree.getRegOrder()
		
	# input: ()
	# output: () Asserts if this schedule does not "looks valid" -- like a legal libuv schedule
	def assertValid (self):
		execList = self._execList()
		
		for execID, cb in enumerate(execList):
			# execIDs must go 0, 1, 2, ...
			logging.debug("Schedule::assertValid: node %s, execID %d, expectedExecID %d" % (cb, int(cb.getExecID()), execID))
			assert(int(cb.getExecID()) == execID)
						
			if (self._isEndMarkerEvent(cb)):
				logging.debug("Schedule::assertValid: cb is an end marker event, updating libuv run stage")
				self._updateLibuvRunStage(cb)
			
			# Threadpool CBs: nothing to check
			if (cb.isThreadpoolCB()):
				logging.debug("Schedule::assertValid: threadpool cb type %s, next...." % (cb.getCBType()))
			else:	
				# If we're in a libuv stage, verify that we're looking at a valid cb type
				# cf. unified-callback-enums.c::is_X_cb
				if (self.currentLibuvRunStage):
					logging.debug("Schedule::assertValid: We're in a libuvStage; verifying current cb type %s is appropriate to the stage" % (cb.getCBType()))
					
					if (self.currentLibuvRunStage == "RUN_TIMERS_1"):
						assert(cb.isRunTimersCB())
					elif (self.currentLibuvRunStage == "RUN_PENDING"):
						assert(cb.isRunPendingCB())
					elif (self.currentLibuvRunStage == "RUN_IDLE"):
						assert(cb.isRunIdleCB())
					elif (self.currentLibuvRunStage == "RUN_PREPARE"):
						assert(cb.isRunPrepareCB())
					elif (self.currentLibuvRunStage == "IO_POLL"):
						assert(cb.isIOPollCB())
					elif (self.currentLibuvRunStage == "RUN_CHECK"):
						assert(cb.isRunCheckCB())
					elif (self.currentLibuvRunStage == "RUN_CLOSING"):
						assert(cb.isRunClosingCB())
					elif (self.currentLibuvRunStage == "RUN_TIMERS_2"):
						assert(cb.isRunTimersCB())
				else:
					# TODO We don't maintain a stack of currentLibuvRunStages, so UV_RUN gets lost. Update _updateLibuvRunStage...
					# However, this is good enough for now.
					logging.debug("Schedule::assertValid: We're not in a libuvStage; verifying current cb type %s is a marker event, INITIAL_STACK, or EXIT" % (cb.getCBType()))
					if (cb.getCBType() == "EXIT"):
						# When we're exiting, we allow any event to occur
						self.exiting = True
					assert(cb.getCBType() in Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER or
								 (cb.getCBType() == "INITIAL_STACK" or self.exiting))					
			
			if (self._isBeginMarkerEvent(cb)):
				logging.debug("Schedule::assertValid: cb is a begin marker event, updating libuv run stage")
				self._updateLibuvRunStage(cb)
							
			logging.debug("Schedule::assertValid: Looks valid")
			self.prevCB = cb
			
	# input: (cb) Latest CB in the schedule
	# output: ()
	# If cbType is a marker event, updates our place in the libuv run loop, affecting:
	# 	- self.currentLibuvRunStage
	#	  - self.markerEventIx
	def _updateLibuvRunStage (self, cb):
		assert(0 <= self.markerEventIx and self.markerEventIx < len(Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER))
		
		if (cb.isMarkerNode()):
			logging.debug("Schedule::_updateLibuvRunStage: Found a marker node (type %s)" % (cb.getCBType()))
			
			# Did we bypass the libuv run loop?
			# This would mean that we just saw MARKER_UV_RUN_BEGIN and now see MARKER_UV_RUN_END
			if (self._isUVRunEvent(cb) and self._isEndMarkerEvent(cb) and self.prevCB.getCBType() == "MARKER_UV_RUN_BEGIN"):				 
				self.markerEventIx = len(Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER) - 1
			 
			# Must match the next marker event type
			assert(cb.getCBType() == self._nextMarkerEventType())
			# Update markerEventIx
			self.markerEventIx += 1
			self.markerEventIx %= len(Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER)
						
			if (self._isUVRunEvent(cb)):
				self.currentLibuvRunStage = None
			else:
				# Verify LibuvRunStages		
				if (self.currentLibuvRunStage):
					logging.debug("Schedule::_updateLibuvRunStage: In libuvRunStage %s, should be leaving it now" % (self.currentLibuvRunStage))
					assert(cb.getCBType() == "MARKER_%s_END" % (self.currentLibuvRunStage))
					self.currentLibuvRunStage = None
				elif (self._isBeginMarkerEvent(cb)):					
					self.currentLibuvRunStage = Schedule.MARKER_EVENT_TO_STAGE[cb.getCBType()]
					logging.debug("Schedule::_updateLibuvRunStage: Not in a libuvRunStage, should be entering %s now" % (self.currentLibuvRunStage))
					assert(cb.getCBType() == "MARKER_%s_BEGIN" % (self.currentLibuvRunStage))
			
			logging.debug("Schedule::_updateLibuRunStage: currentLibuvRunStage %s" % (self.currentLibuvRunStage))
								
		else:
			logging.debug("Schedule::_updateLibuvRunStage: Non-marker node of type %s, nothing to do" % (cb.getCBType()))
		
				
	def _isBeginMarkerEvent (self, cb):
		if (re.search('_BEGIN$', cb.getCBType())):
			return True
		return False
	
	def _isEndMarkerEvent (self, cb):
		if (re.search('_END$', cb.getCBType())):
			return True
		return False
	
	# MARKER_UV_RUN_BEGIN or MARKER_UV_RUN_END
	def _isUVRunEvent (self, cb):
		if (re.match("^MARKER_UV_RUN_(?:BEGIN|END)$", cb.getCBType())):
			return True
		return False
	
	# input: ()
	# output: (eventType) type of the next marker event 
	def _nextMarkerEventType (self):
		logging.debug("Schedule::_nextMarkerEventType: markerEventIx %d type %s" % (self.markerEventIx, Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER[self.markerEventIx]))
		return Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER[self.markerEventIx]
	
	# input: (raceyNodeIDs)
	#	 raceyNodeIDs: list of Callback registration IDs
	# output: ()
	# Modifies the schedule so that the specified Callbacks are executed in reverse order compared to how they actually executed in this Schedule
	# Throws a ScheduleException if the requested exchange is not feasible
	def reschedule (self, raceyNodeIDs):
		nodesToFlip = [self.cbTree.getNodeByRegID(nodeID) for nodeID in raceyNodeIDs]
		logging.debug("reschedule: nodesToFlip %s" % (nodesToFlip))

		# Validate nodes1
		for n in nodesToFlip:
			if (not n):
				raise ScheduleException('Error, could not find node by reg ID')

			logging.debug("reschedule: raceyNode %s" % (n))
			if (n.isMarkerNode()):
				raise ScheduleException('Error, one of the nodes to flip is an (unflippable) marker node: %s' % (n.getCBType()))
			if (n.getCBType() != "UV_TIMER_CB"):
				raise ScheduleException('Error, one of the nodes to flip is not a timer node, not yet supported: %s' % (n.getCBType()))
			if (not n.executed()):
				raise ScheduleException('Error, one of the nodes to flip was not executed')
			executedChildren = [ch for ch in n.getChildren() if ch.executed()]
			if (executedChildren):
				raise ScheduleException('Error, one of the nodes to flip has executed children, not yet supported')

		# Now we have childless timer CBs. We can just swap their exec IDs and it should all work out.
		nodesToFlip.sort(key=lambda n: n.getExecID())
		newExecIDs = list(reversed([n.getExecID() for n in nodesToFlip]))
		for ix, n in enumerate(nodesToFlip):
			n.setExecID(newExecIDs[ix])


		# TODO I AM HERE -- Adding tree climbing to find async ancestor and so on
		#raise ScheduleException("foo")
	
	# input: (file)
	#	 place to write the schedule
	# output: ()
	# Emits this schedule to the specified file.
	# May raise IOError
	def emit (self, file):
		logging.info("Schedule::emit: Emitting schedule to file %s" % (file))
		regOrder = self._regList()
		with open(file, 'w') as f:
			for cb in regOrder:
				f.write("%s\n" % (cb))
