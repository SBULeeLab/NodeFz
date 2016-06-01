# Author: Jamie Davis (davisjam@vt.edu)
# Description: Python description of libuv callback schedule
# Defines the following public classes: 
# Schedule
# Defines the following exceptions:
# ScheduleException
# Python version: 2.7.6

import re
import logging
import copy

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

	LIBUV_RUN_OUTER_STAGES = ["UV_RUN"]
	LIBUV_RUN_INNER_STAGES = ["RUN_TIMERS_1", "RUN_PENDING", "RUN_IDLE", "RUN_PREPARE", "IO_POLL", "RUN_CHECK", "RUN_CLOSING", "RUN_TIMERS_2"]
	LIBUV_RUN_ALL_STAGES = ["UV_RUN"] + LIBUV_RUN_INNER_STAGES
	MARKER_EVENT_TO_STAGE = {} # Map LIBUV_LOOP_STAGE_MARKER_ORDER elements to one of LIBUV_STAGES

	# MARKER_UV_RUN_BEGIN begins
	runBegin, runEnd = ("MARKER_%s_BEGIN" % (LIBUV_RUN_OUTER_STAGES[0]), "MARKER_%s_END" % (LIBUV_RUN_OUTER_STAGES[0]))
	LIBUV_LOOP_STAGE_MARKER_ORDER.append(runBegin)
	MARKER_EVENT_TO_STAGE[runBegin] = "UV_RUN"

	# Followed by begin/end for each of the UV_RUN stages
	for stage in LIBUV_RUN_INNER_STAGES:
		stageBegin, stageEnd = "MARKER_{}_BEGIN".format(stage), "MARKER_{}_END".format(stage)
		LIBUV_LOOP_STAGE_MARKER_ORDER.append(stageBegin)
		LIBUV_LOOP_STAGE_MARKER_ORDER.append(stageEnd)
		MARKER_EVENT_TO_STAGE[stageBegin] = stage
		MARKER_EVENT_TO_STAGE[stageEnd] = stage

	# MARKER_UV_RUN_END follows
	LIBUV_LOOP_STAGE_MARKER_ORDER.append(runEnd)
	MARKER_EVENT_TO_STAGE[runEnd] = "UV_RUN"

	LIBUV_LOOP_OUTER_STAGE_MARKER_ORDER = [runBegin, runEnd]
	LIBUV_LOOP_INNER_STAGE_MARKER_ORDER = LIBUV_LOOP_STAGE_MARKER_ORDER[1:-1]

	def __init__ (self, scheduleFile):
		logging.debug("scheduleFile {}".format(scheduleFile))
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
			logging.debug("ix {} cbType {}".format(ix, cb.getCBType()))

		# Annotate each event with its loop iter and libuv stage
		for ix, event in enumerate(execSchedule):
			cb = event.getCB()
			logging.debug("ix {} cbType {} libuvLoopCount {} libuvLoopStage {}".format(ix, cb.getCBType(), libuvLoopCount, libuvLoopStage))
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
				assert(cb.getCBType() in CB.CallbackNode.TP_TYPES or libuvLoopStage is not None)
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
			logging.debug("node {}, execID {}, expectedExecID {}".format(cb, int(cb.getExecID()), execID))
			assert(int(cb.getExecID()) == execID)

			if self._isEndMarkerEvent(cb):
				logging.debug("cb is an end marker event, updating libuv run stage")
				self._updateLibuvRunStage(cb)

			# Threadpool CBs: nothing to check
			if cb.isThreadpoolCB():
				logging.debug("threadpool cb type {}, next....".format(cb.getCBType()))
			else:
				# If we're in a libuv stage, verify that we're looking at a valid cb type
				# cf. unified-callback-enums.c::is_X_cb
				if self.currentLibuvRunStage is not None:
					logging.debug("We're in a libuvStage; verifying current cb type {} is appropriate to the stage".format(cb.getCBType()))

					if self.currentLibuvRunStage == "RUN_TIMERS_1":
						assert(cb.isRunTimersCB())
					elif self.currentLibuvRunStage == "RUN_PENDING":
						assert(cb.isRunPendingCB())
					elif self.currentLibuvRunStage == "RUN_IDLE":
						assert(cb.isRunIdleCB())
					elif self.currentLibuvRunStage == "RUN_PREPARE":
						assert(cb.isRunPrepareCB())
					elif self.currentLibuvRunStage == "IO_POLL":
						assert(cb.isIOPollCB())
					elif self.currentLibuvRunStage == "RUN_CHECK":
						assert(cb.isRunCheckCB())
					elif self.currentLibuvRunStage == "RUN_CLOSING":
						assert(cb.isRunClosingCB())
					elif self.currentLibuvRunStage == "RUN_TIMERS_2":
						assert(cb.isRunTimersCB())
				else:
					# TODO We don't maintain a stack of currentLibuvRunStages, so UV_RUN gets lost. Update _updateLibuvRunStage...
					# However, this is good enough for now.
					logging.debug("We're not in a libuvStage; verifying current cb type {} is a marker event, INITIAL_STACK, or EXIT".format(cb.getCBType()))
					if cb.getCBType() == "EXIT":
						# When we're exiting, we allow any event to occur
						self.exiting = True
					assert(cb.getCBType() in Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER or
								 (cb.getCBType() == "INITIAL_STACK" or self.exiting))

			if self._isBeginMarkerEvent(cb):
				logging.debug("cb is a begin marker event, updating libuv run stage")
				self._updateLibuvRunStage(cb)

			logging.debug("Looks valid")
			self.prevCB = cb

	# input: (cb) Latest CB in the schedule
	# output: ()
	# If cbType is a marker event, updates our place in the libuv run loop, affecting:
	# 	- self.currentLibuvRunStage
	#	  - self.markerEventIx
	def _updateLibuvRunStage(self, cb):
		assert(0 <= self.markerEventIx < len(Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER))

		if cb.isMarkerNode():
			logging.debug("Found a marker node (type {})".format(cb.getCBType()))

			# Did we bypass the libuv run loop?
			# This would mean that we just saw MARKER_UV_RUN_BEGIN and now see MARKER_UV_RUN_END
			if self._isUVRunEvent(cb) and self._isEndMarkerEvent(cb) and self.prevCB.getCBType() == "MARKER_UV_RUN_BEGIN":
				self.markerEventIx = len(Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER) - 1

			# Must match the next marker event type
			assert(cb.getCBType() == self._nextMarkerEventType())
			# Update markerEventIx
			self.markerEventIx += 1
			self.markerEventIx %= len(Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER)

			if self._isUVRunEvent(cb):
				self.currentLibuvRunStage = None
			else:
				# Verify LibuvRunStages		
				if self.currentLibuvRunStage is not None:
					logging.debug("In libuvRunStage {}, should be leaving it now".format(self.currentLibuvRunStage))
					assert(cb.getCBType() == "MARKER_%s_END" % (self.currentLibuvRunStage))
					self.currentLibuvRunStage = None
				elif self._isBeginMarkerEvent(cb):
					self.currentLibuvRunStage = Schedule.MARKER_EVENT_TO_STAGE[cb.getCBType()]
					logging.debug("Not in a libuvRunStage, should be entering {} now".format(self.currentLibuvRunStage))
					assert(cb.getCBType() == "MARKER_%s_BEGIN" % (self.currentLibuvRunStage))

			logging.debug("currentLibuvRunStage {}".format(self.currentLibuvRunStage))

		else:
			logging.debug("Non-marker node of type {}, nothing to do".format(cb.getCBType()))


	def _isBeginMarkerEvent(self, cb):
		if re.search('_BEGIN$', cb.getCBType()):
			return True
		return False

	def _isEndMarkerEvent (self, cb):
		if re.search('_END$', cb.getCBType()):
			return True
		return False

	# MARKER_UV_RUN_BEGIN or MARKER_UV_RUN_END
	def _isUVRunEvent (self, cb):
		if re.match("^MARKER_UV_RUN_(?:BEGIN|END)$", cb.getCBType()):
			return True
		return False

	# input: ()
	# output: (eventType) type of the next marker event 
	def _nextMarkerEventType(self):
		logging.debug("markerEventIx {} type {}".format(self.markerEventIx, Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER[self.markerEventIx]))
		return Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER[self.markerEventIx]

	# input: (racyNodeIDs)
	#	racyNodeIDs: list of Callback registration IDs
	# output: (events, eventsToReschedule, pivot)
	#	events: list, the events to which racyNodeIDs correspond, in original execution order
	#	eventsToReschedule: list, the events that need to be rescheduled, in original execution order. subset of events.
	# pivot: one of the events, the "pivot" around which to reschedule the eventsToReschedule
	#
	# Raises a ScheduleException on invalid or insupportable request
	def _verifyRescheduleIDs(self, racyNodeIDs):
		events = [e for e in self.execSchedule if int(e.getCB().getID()) in racyNodeIDs]

		# Validate the events
		for event in events:
			assert (event is not None)
			cb = event.getCB()

			logging.info("racy node: {}".format(cb))

			# We cannot flip unexecuted events
			if not cb.executed():
				raise ScheduleException('Error, one of the nodes to flip was not executed')

			# We cannot flip events if there is a happens-before relatinoship between them
			otherEvents = [e for e in events if e is not event]
			for other in otherEvents:
				if cb.isAncestorOf(other.getCB()):
					raise ScheduleException(
						'Error, one of the nodes to flip is an ancestor of another of the nodes to flip:\n  {}\n  {}'.format(cb,
																																																								 other.getCB()))

		origRacyExecOrder = sorted(events, key=lambda e: int(e.getCB().getExecID()))

		# We can only reschedule async events at the moment
		# In the future we could climb until we find network input (cannot flip) or an async node (can flip)
		asyncEvents = [e for e in events if e.getCB().isAsync()]

		# Identify the pivot: the event around which we "pivot" the other events as we reschedule
		pivot = None
		eventsToReschedule = None
		if len(asyncEvents) + 1 == len(events):
			eventsToReschedule = asyncEvents
			pivots = set(events) - set(asyncEvents)
			assert(len(pivots) == 1)
			pivot = pivots.pop()
		elif len(asyncEvents) == len(events):
			# If we can reschedule any of them, pivot defaults to the latest-scheduled event
			pivot = origRacyExecOrder[-1]
			eventsToReschedule = [e for e in origRacyExecOrder if e is not pivot]
		else:
			# Cannot handle more than one pivot
			types = [e.getCB().getCBType() for e in events]
			raise ScheduleException('Error, one of the nodes to flip is not an async node. Types of events: %s'.format(types))

		return origRacyExecOrder, eventsToReschedule, pivot

	# input: (racyNodeIDs)
	#	racyNodeIDs: list of Callback registration IDs
	# output: (origToNewIDs)
	# origToNewIDs: dict from racyNodeID entries to newNodeID entries after changing execution order
	# Modifies the schedule so that the specified Callbacks are executed in reverse order compared to how they actually
	# executed in this Schedule.
	# Throws a ScheduleException if the requested exchange is not feasible
	def reschedule(self, racyNodeIDs):
		assert(0 < len(racyNodeIDs))
		if len(racyNodeIDs) != 2:
			raise ScheduleException('Error, cannot reschedule more than 2 events')
		origToNewIDs = {}

		events, eventsToReschedule, pivot = self._verifyRescheduleIDs(racyNodeIDs)
		logging.info("events {}".format(events))

		# Save original IDs so that we can populate origToNewIDs later
		eventToOrigID = {}
		for event in events:
			eventToOrigID[event] = event.getCB().getID()

		# Map from an event to all events that descend from or depend on it
		eventToEventsToMove = {}

		# Remove all events and their children from the schedule
		for event in eventsToReschedule:
			# Identify all events that need to be relocated: event and its descendants and dependents, now referred to as descendants
			executedEventDescendants_CBs = [cb for cb in event.getCB().getDescendants(includeDependents=True) if
																			cb.executed()]
			eventDescendants_IDs = [int(cb.getID()) for cb in executedEventDescendants_CBs]
			# This is an expensive call for large lists
			allEventsToMove = [e for e in self.execSchedule if int(e.getCB().getID()) in eventDescendants_IDs]
			logging.info("eventDescendants_IDs {}".format(eventDescendants_IDs))

			# Save
			eventToEventsToMove[event] = allEventsToMove

			# Remove them
			for eventToMove in allEventsToMove:
				self.execSchedule.remove(eventToMove)

		#TODO I AM HERE

		# For nodes originally executed after the pivot, we must insert them at or before insertBefore_maxIx
		# For nodes originally executed before the pivot, we must insert them at or after insertAfter_minIx
		# When we traverse eventsToReschedule (sorted by orig execution order), we'll update one of these indices each time
		#	As a result, if we start with A B pivot X Y, we'll end up with Y X pivot B A
		insertBefore_maxIx = self.execSchedule.index(pivot)
		insertAfter_minIx  = self.execSchedule.index(pivot) + 1

		pivot_origExecID = pivot.getCB().getExecID()




			# Re-insert them in the schedule, no earlier in the list than minInsertIx
			# TODO I am here -- working on TP CBs
			for eventIx, eventToMove in enumerate(allEventsToMove):
				logging.info("Re-inserting event of type {} (looking for the next end marker of type {})".format(eventToMove.getCB().getCBType(), eventToMove.getLibuvLoopStage()))
				inserted = False
				addedNewLoop = False
				while not inserted:
					# Locate the next MARKER_*_END event of the appropriate libuv stage
					# TODO Should jump ahead N loops to honor relative timing?
					(insertIx, nextMarker) = self._findLaterCB(lambda e: self._isEndMarkerEvent(e.getCB()) and Schedule.MARKER_EVENT_TO_STAGE[e.getCB().getCBType()] == eventToMove.getLibuvLoopStage(), minInsertIx)
					if insertIx is not None:
						logging.info("inserting at ix {}".format(insertIx))
						self.execSchedule.insert(insertIx, eventToMove)
						inserted = True

						# Add parent to the map
						if eventIx == 0:
							origToNewIDs[eventToMove.getCB().getID()] = insertIx

						# Update loop variables
						minInsertIx = insertIx  # Next event must be inserted after this node
						addedNewLoop = False
					else:
						# If we cannot find an insertion point, it must be because we could not find an appropriate marker,
						# i.e. that we ran out of UV_RUN loops.
						# Add a new UVRun loop to the schedule.

						# ...unless we've already been here with this item. Adding one new loop should have been enough.
						# NB Won't work if insertIx occurs in a final incomplete loop; see _addUVRunLoop
						assert(not addedNewLoop)

						logging.info("Adding a UVRun loop")
						minInsertIx = self._addUVRunLoop(enterLoop=True)
						addedNewLoop = True

		# Update the execID of the events in the schedule
		# This also affects the IDs of subsequent events in racyEventsToMove, if any
		for insertIx, e in enumerate(self.execSchedule):
			e.getCB().setExecID(insertIx)

		# "stable" event's ID has changed because we've removed nodes before it
		for event in eventToOrigID:
			origToNewIDs[eventToOrigID[event]] = event.getCB().getID()

		self.assertValid()
		return origToNewIDs

	# input: (searchFunc, startIx)
	# startIx defaults to the end of self.execSchedule
	# output: (eventIx)
	# Find the latest (largest execID) ScheduleEvent in self.execSchedule, at or prior to startIx, for which searchFunc(e) evaluates to true
	# Returns (None, None) if not found
	def _findEarlierCB(self, searchFunc, startIx=None):
		maxIx = len(self.execSchedule) - 1
		if startIx is None:
			startIx = maxIx
		assert(0 <= startIx <= maxIx)

		# We search left(earlier)-wards, so invert startIx and reverse execSchedule
		startIx = maxIx - startIx
		eventsToSearch = enumerate(list(reversed(self.execSchedule))[startIx:])
		for i, event in eventsToSearch:
			if searchFunc(event):
				return (startIx-i, event)
		return (None, None)

	# input: (searchFunc, startIx)
	# startIx defaults to the beginning of self.execSchedule
	# output: (eventIx)
	# Find the earliest (smallest execID) ScheduleEvent in self.execSchedule, at or after startIx, for which searchFunc(e) evaluates to true
	# Returns (None, None) if not found
	def _findLaterCB(self, searchFunc, startIx=None):
		minIx = 0
		if startIx is None:
			startIx = minIx
		assert (minIx <= startIx < len(self.execSchedule))

		eventsToSearch = enumerate((self.execSchedule)[startIx:])
		for i, event in eventsToSearch:
			if searchFunc(event):
				return (startIx+i, event)
		return (None, None)

	# input: (enterLoop) if True, add MARKERS for all of the internal events. Else just add MARKER_UV_RUN_{BEGIN,END}
	# output: (ixOfBeginningOfNewLoop)
	# Adds a new UV_RUN loop after the end of the final complete UV_RUN loop in self.execSchedule
	# (i.e. adds MARKER_* ScheduleEvents to self.execSchedule and self.cbTree)
	# Returns the index of the MARKER_UV_RUN_BEGIN beginning the new loop
	def _addUVRunLoop(self, enterLoop):
		# Locate end of final complete UV_RUN loop
		(ixOfEndOfFinalCompleteLoop, end) = self._findEarlierCB(lambda e: e.getCB().getCBType() == "MARKER_UV_RUN_END")
		if ixOfEndOfFinalCompleteLoop is None:
			raise ScheduleException("Could not find the end of a complete UV_RUN loop. Did this application crash on its first loop iteration?")

		ixOfBeginningOfNewLoop = ixOfEndOfFinalCompleteLoop + 1
		# Markers events are in a giant tree INITIAL_STACK -> M1 -> M2 -> M3 -> ...
		# markerParent tracks the parent of the next marker we insert
		# NB: We find the final COMPLETE loop, so markerParent may have a child leading to marker events in a final partial loop.
		# In this case we'll have to correctly insert ourselves in between. However, we won't bother updating reg_ids
		# because replay depends only on relative tree position.
		# If we ever have to do that, though, this is most easily done by simulating an execution:
		# 	stepping through the exec_schedule and setting the reg_id for all children of each executed node in order.
		markerParent = self.execSchedule[ixOfEndOfFinalCompleteLoop]
		insertIx = ixOfBeginningOfNewLoop

		if enterLoop:
			stagesToInsert = Schedule.LIBUV_RUN_ALL_STAGES
		else:
			stagesToInsert = Schedule.LIBUV_RUN_OUTER_STAGES

		for stage in stagesToInsert:
			stageBegin, stageEnd = "MARKER_{}_BEGIN".format(stage), "MARKER_{}_END".format(stage)
			for type in [stageBegin, stageEnd]:
				# Create a new ScheduleEvent modeled after the previous ScheduleEvent of this type
				(templateIx, template) = self._findEarlierCB(lambda e: e.getCB().getCBType() == type, insertIx)
				assert(template is not None)
				newEvent = copy.deepcopy(template)

				# For valid REPLAY, need the following:
				newEvent.getCB().setName("0x{}".format(insertIx)) 		 # Unique name
				newEvent.getCB().setTreeParent(markerParent.getName()) # Correct parent

				# Fix up the CallbackNodeTree details (parent, children)
				# Maybe needed in the future
				newEvent.getCB().setParent(markerParent.getCB())
				newEvent.getCB().setChildren(markerParent.getChildren())
				markerParent.getCB().setChildren([newEvent])
				for c in newEvent.getCB().getChildren():
					c.setParent(newEvent.getCB())

				# Insert and update position info
				self.execSchedule.insert(insertIx, newEvent)
				markerParent = newEvent
				insertIx += 1

		# Re-wire the subsequent MARKER node, if any
		(nextMarkerEventIx, nextMarkerEvent) = self._findLaterCB(lambda x: x.getCB().isMarkerNode(), insertIx)
		if (nextMarkerEvent is not None):
			assert(nextMarkerEvent is not markerParent)
			nextMarkerEvent.getCB().setTreeParent(markerParent.getName())

		return ixOfBeginningOfNewLoop

	# input: (file)
	#	 place to write the schedule
	# output: ()
	# Emits this schedule to the specified file.
	# May raise IOError
	def emit(self, file):
		logging.info("Emitting schedule to file {}".format(file))
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