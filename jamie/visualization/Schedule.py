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

# Representation of a libuv callback schedule
class Schedule (object):
	# CBs of these types are asynchronous, and can be moved from one loop to another provided that they do not violate
	# happens-before order
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
		logging.debug("Schedule::__init__: scheduleFile {}".format(scheduleFile))
		self.scheduleFile = scheduleFile

		self.cbTree = CB.CallbackNodeTree(self.scheduleFile)
		self.execSchedule = self._genExecSchedule(self.cbTree) # List of annotated ScheduleEvents

		self.markerEventIx = 0 					 # The next marker event should be this ix in LIBUV_LOOP_STAGE_MARKER_ORDER
		self.currentLibuvRunStage = None # Tracks the marker event we're currently processing. Either one of LIBUV_RUN_STAGES or None.

		self.assertValid()

		self.prevCB = None   # To check if we short-circuited a UV_RUN loop and just have MARKER_UV_RUN_{BEGIN,END}. TODO This is a hack.
		self.exiting = False # If we've seen the EXIT event.

	# input: (cbTree)
	# output: (execSchedule) List of ScheduleEvents
	def _genExecSchedule (self, cbTree):
		# ScheduleEvents in the order of execution
		execSchedule = [ScheduleEvent(cb) for cb in cbTree.getTreeNodes() if cb.executed()]
		execSchedule.sort(key=lambda n: int(n.getCB().getExecID()))

		libuvLoopCount = -1
		libuvLoopStage = None
		exiting = False

		for ix, event in enumerate(execSchedule):
			cb = event.getCB()
			logging.debug("Schedule::_genExecSchedule: ix {} cbType {}".format(ix, cb.getCBType()))

		# Annotate each event with its loop iter and libuv stage
		for ix, event in enumerate(execSchedule):
			cb = event.getCB()
			logging.debug("Schedule::_genExecSchedule: ix {} cbType {} libuvLoopCount {} libuvLoopStage {}".format(ix, cb.getCBType(), libuvLoopCount, libuvLoopStage))
			if cb.getCBType() == "INITIAL_STACK":
				pass
			elif cb.getCBType() == "EXIT":
				exiting = True
				libuvLoopStage = "EXITING"
			elif cb.isMarkerNode():
				assert(not exiting)
				if cb.getCBType() == "MARKER_UV_RUN_BEGIN":
					libuvLoopCount += 1

				if self._isBeginMarkerEvent(cb):
					libuvLoopStage = Schedule.MARKER_EVENT_TO_STAGE[cb.getCBType()]
				else:
					assert(self._isEndMarkerEvent(cb))
					libuvLoopStage = None

			else:
				assert(libuvLoopStage is not None)
				event.setLibuvLoopCount(libuvLoopCount)
				event.setLibuvLoopStage(libuvLoopStage)

		return execSchedule

	# input: ()
	# output: (regList) returns list of CallbackNodes in increasing registration order.
	def _regList(self):
		return self.cbTree.getRegOrder()

	# input: ()
	# output: () Asserts if this schedule does not "looks valid" (i.e. like a legal libuv schedule)
	# TODO Change this to isValid and don't use class variables to control the results of the _helperFunctions
	def assertValid(self):
		for execID, event in enumerate(self.execSchedule):
			cb = event.getCB()

			# execIDs must go 0, 1, 2, ...
			logging.debug("Schedule::assertValid: node %s, execID {}, expectedExecID {}".format(cb, int(cb.getExecID()), execID))
			assert(int(cb.getExecID()) == execID)

			if (self._isEndMarkerEvent(cb)):
				logging.debug("Schedule::assertValid: cb is an end marker event, updating libuv run stage")
				self._updateLibuvRunStage(cb)

			# Threadpool CBs: nothing to check
			if (cb.isThreadpoolCB()):
				logging.debug("Schedule::assertValid: threadpool cb type {}, next....".format(cb.getCBType()))
			else:
				# If we're in a libuv stage, verify that we're looking at a valid cb type
				# cf. unified-callback-enums.c::is_X_cb
				if (self.currentLibuvRunStage):
					logging.debug("Schedule::assertValid: We're in a libuvStage; verifying current cb type {} is appropriate to the stage".format(cb.getCBType()))

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
					logging.debug("Schedule::assertValid: We're not in a libuvStage; verifying current cb type {} is a marker event, INITIAL_STACK, or EXIT".format(cb.getCBType()))
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
	def _updateLibuvRunStage(self, cb):
		assert(0 <= self.markerEventIx and self.markerEventIx < len(Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER))

		if (cb.isMarkerNode()):
			logging.debug("Schedule::_updateLibuvRunStage: Found a marker node (type {})".format(cb.getCBType()))

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
				if self.currentLibuvRunStage is not None:
					logging.debug("Schedule::_updateLibuvRunStage: In libuvRunStage {}, should be leaving it now".format(self.currentLibuvRunStage))
					assert(cb.getCBType() == "MARKER_%s_END" % (self.currentLibuvRunStage))
					self.currentLibuvRunStage = None
				elif (self._isBeginMarkerEvent(cb)):
					self.currentLibuvRunStage = Schedule.MARKER_EVENT_TO_STAGE[cb.getCBType()]
					logging.debug("Schedule::_updateLibuvRunStage: Not in a libuvRunStage, should be entering {} now".format(self.currentLibuvRunStage))
					assert(cb.getCBType() == "MARKER_%s_BEGIN" % (self.currentLibuvRunStage))

			logging.debug("Schedule::_updateLibuRunStage: currentLibuvRunStage {}".format(self.currentLibuvRunStage))

		else:
			logging.debug("Schedule::_updateLibuvRunStage: Non-marker node of type {}, nothing to do".format(cb.getCBType()))


	def _isBeginMarkerEvent(self, cb):
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
	def _nextMarkerEventType(self):
		logging.debug("Schedule::_nextMarkerEventType: markerEventIx {} type {}".format(self.markerEventIx, Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER[self.markerEventIx]))
		return Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER[self.markerEventIx]

	# input: (raceyNodeIDs)
	#	 raceyNodeIDs: list of Callback registration IDs
	# output: ()
	# Modifies the schedule so that the specified Callbacks are executed in reverse order compared to how they actually executed in this Schedule
	# Throws a ScheduleException if the requested exchange is not feasible
	def reschedule(self, raceyNodeIDs):

		eventsToFlip = [e for e in self.execSchedule if int(e.getCB().getRegID()) in raceyNodeIDs]
		logging.debug("reschedule: eventsToFlip {}".format(eventsToFlip))

		# Validate the requested eventsToFlip
		for e in eventsToFlip:
			cb = e.getCB()
			if (not e):
				raise ScheduleException('Error, could not find node by reg ID')

			logging.debug("reschedule: raceyNode {}".format(cb))
			if (cb.isMarkerNode()):
				raise ScheduleException('Error, one of the nodes to flip is an (unflippable) marker node: {}'.format(cb.getCBType()))
			if (not cb.isAsync()):
				raise ScheduleException('Error, one of the nodes to flip is not an async node. Type not yet supported: {}'.format(cb.getCBType()))
			if (not cb.executed()):
				raise ScheduleException('Error, one of the nodes to flip was not executed')

		origRaceyExecOrder = sorted(eventsToFlip, key=lambda e: int(e.getCB().getExecID()))
		newRaceyExecOrder = list(reversed(eventsToFlip))
		raceyEventsToMove = newRaceyExecOrder[1:] # Leave the final one in place, move the others after it

		insertIx = self.execSchedule.index(newRaceyExecOrder[0]) # Must insert after insertIx

		for event in raceyEventsToMove:
			# Identify all events that need to be relocated: event and its descendants
			executedEventDescendants_CBs = [cb for cb in self.cbTree.getDescendants(event.getCB()) if cb.executed()]
			eventDescendants_IDs = [int(cb.getRegID()) for cb in executedEventDescendants_CBs]
			allEventsToMove = [event] + [e for e in self.execSchedule if int(e.getCB().getRegID()) in eventDescendants_IDs]
			logging.debug("reschedule: eventDescendants_IDs {}".format(eventDescendants_IDs))

			# Remove them
			logging.debug("reschedule: len before: {}".format(len(self.execSchedule)))
			for eventToMove in allEventsToMove:
				self.execSchedule.remove(eventToMove)
			logging.debug("reschedule: len after: {}".format(len(self.execSchedule)))

			# Replace them, no earlier in the list than insertIx
			for eventToMove in allEventsToMove:
				logging.debug("reschedule: Re-inserting event {} of type {} (looking for the next end marker of type {})".format(eventToMove.getCB().getRegID(), eventToMove.getCB().getCBType(), eventToMove.getLibuvLoopStage()))
				inserted = False
				# Locate the next MARKER_*_END event of the appropriate libuv stage
				for i in range(insertIx+1, len(self.execSchedule)):
					candEvent = self.execSchedule[i]
					logging.debug("reschedule: Testing event {}: {}".format(candEvent.getCB().getRegID(), candEvent.getCB().getCBType()))
					if (self._isEndMarkerEvent(candEvent.getCB()) and Schedule.MARKER_EVENT_TO_STAGE[candEvent.getCB().getCBType()] == eventToMove.getLibuvLoopStage()):
						# Insert eventToMove here
						logging.debug("reschedule: inserting at ix {}".format(i))
						self.execSchedule.insert(i, eventToMove)
						inserted = True
						insertIx = i  # Must insert after this node
						break
				assert(inserted) # TODO May not always be possible, we may have to add a new loop and new marker events

		# Update the execID of the events in the schedule
		for ix, e in enumerate(self.execSchedule):
			e.getCB().setExecID(ix)

		self.assertValid()

	# input: (file)
	#	 place to write the schedule
	# output: ()
	# Emits this schedule to the specified file.
	# May raise IOError
	def emit (self, file):
		logging.info("Schedule::emit: Emitting schedule to file {}".format(file))
		regOrder = self._regList()
		with open(file, 'w') as f:
			for cb in regOrder:
				f.write("%s\n" % (cb)) # TODO Timestamps mildly off?

#############################
# ScheduleEvent
#############################

# Represents events in a schedule
class ScheduleEvent(object):
	def __init__(self, cb):
		self.cb = cb
		self.libuvLoopCount = -1
		self.libuvLoopStage = None

	def getCB(self):
		return self.cb

	def setLibuvLoopCount(self, count):
		self.libuvLoopCount = count

	def getLibuvLoopCount(self):
		assert (0 <= self.libuvLoopCount)
		return self.libuvLoopCount

	def setLibuvLoopStage(self, stage):
		self.libuvLoopStage = stage

	def getLibuvLoopStage(self):
		assert (self.libuvLoopCount is not None)
		return self.libuvLoopStage