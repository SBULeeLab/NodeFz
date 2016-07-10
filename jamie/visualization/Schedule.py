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
# RelocationClass
#############################

# Some helper types for Schedule
class RelocationClass:
	TP_WORK = "TP_WORK"
	TP_DONE = "TP_DONE"
	GENERAL_LOOPER = "GENERAL_LOOPER"

	tpClasses = [TP_WORK, TP_DONE]
	looperClasses = [GENERAL_LOOPER]
	relocationClasses = tpClasses + looperClasses

#############################
# Schedule
#############################

# Representation of a libuv callback schedule.
# The constructor and public functions always return leaving the Schedule with self.isValid() == True.
# Private functions (self._*) might not.
# Method calls may throw a ScheduleException.
class Schedule (object):
	# CBs of these types are asynchronous, and can be moved from one loop to another provided that they do not violate
	# happens-before order
	ASYNC_CB_TYPES = ["UV_TIMER_CB", "UV_WORK_CB", "UV_AFTER_WORK_CB"]
	# Order in which marker events occur.
	# This is also a list of all marker event types.
	LIBUV_LOOP_STAGE_MARKER_ORDER = []

	LIBUV_RUN_OUTER_STAGES = ["UV_RUN"]
	LIBUV_RUN_INNER_STAGES = ["RUN_TIMERS_1", "RUN_PENDING", "RUN_IDLE", "RUN_PREPARE", "IO_POLL", "RUN_CHECK", "RUN_CLOSING", "RUN_TIMERS_2"]
	LIBUV_RUN_ALL_STAGES = ["UV_RUN"] + LIBUV_RUN_INNER_STAGES
	MARKER_EVENT_TO_STAGE = {} # Map LIBUV_LOOP_STAGE_MARKER_ORDER elements to one of LIBUV_STAGES.

	THREADPOOL_STAGE = ["THREADPOOL"] # We label all threadpool events as being in this dummy stage.

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

	LIBUV_THREADPOOL_DONE_BEGINNING_TYPES = ["UV_ASYNC_CB"]

	def __init__ (self, scheduleFile):
		logging.debug("scheduleFile {}".format(scheduleFile))
		self.scheduleFile = scheduleFile
		self.cbTree = CB.CallbackNodeTree(self.scheduleFile)
		self.execSchedule = self._genExecSchedule(self.cbTree) # List of annotated ScheduleEvents

		# TODO DEBUGGING: This is either None or a ScheduleEvent
		self.tpDoneAsyncRoot = self._findTPDoneAsyncRoot()
		logging.debug("tpDoneAsyncRoot {}".format(self.tpDoneAsyncRoot))
		
		# nextNewEventID and all larger numbers are unique registration IDs for new nodes
		self.nextNewEventID = max([int(cb.getRegID()) for cb in self.cbTree.getTreeNodes()]) + 1
		
		if not self.isValid():
			raise ScheduleException("The input schedule was not valid")		

	# input: ()
	# output: (tpDoneAsyncRoot)
	#
	# The threadpool associates a uv_async object with its 'done' items.
	# When the threadpool finishes a work item, it places it on the 'done' queue and uv_async_send's to
	#   set the associated async fd, causing uv__io_poll to go off.
	# All 'TP done' events are thus nested under UV_ASYNC_CBs in the same chain.
	#
	# This method returns the first ScheduleEvent in this UV_ASYNC_CB chain, or None if there isn't one.
	# It can be called during the Schedule constructor, but self.execSchedule must have been initialized.
	def _findTPDoneAsyncRoot(self):
		assert(0 < len(self.execSchedule))

		# If there is an async root, it is an executed direct child of the INITIAL_STACK...
		asyncCBs = [cbn for cbn in self.cbTree.root.getChildren() if cbn.getCBType() in Schedule.LIBUV_THREADPOOL_DONE_BEGINNING_TYPES and cbn.executed()]

		# ...and the first non-threadpool event after it is in CB.CallbackNode.TP_DONE_INITIAL_TYPES
		# Search all of root's async children for candidates for the head of the TP UV_ASYNC_CB.
		asyncExecIDToAsyncCB = { int(asyncCB.getExecID()): asyncCB for asyncCB in asyncCBs }
		asyncCBExecIDs = [int(cbn.getExecID()) for cbn in asyncCBs if cbn.executed()]
		logging.debug("Candidate exec IDs: {} (len(execSchedule) == {})".format(asyncCBExecIDs, len(self.execSchedule)))

		tpDoneAsyncRootCandidates = []
		for id in asyncCBExecIDs:
			nextLooperIx, nextLooper = self._findNextLooperScheduleEvent(id + 1)
			if nextLooper is not None:
				logging.debug("Candidate id {}: nextLooper {} (type {})".format(id, nextLooper, nextLooper.getCB().getCBType()))
				if nextLooper.getCB().getCBType() in CB.CallbackNode.TP_DONE_TYPES:
					tpDoneAsyncRootCandidates.append(nextLooper)

		# There can't be more than one of these -- otherwise we have competing threadpool done chains
		assert(len(tpDoneAsyncRootCandidates) <= 1)

		# Did we find one?
		tpDoneAsyncRoot = None
		if len(tpDoneAsyncRootCandidates) == 1:
			tpDoneAsyncRoot = tpDoneAsyncRootCandidates[0]
			assert(tpDoneAsyncRoot.getCB().getCBType() in Schedule.LIBUV_THREADPOOL_DONE_BEGINNING_TYPES)
		return tpDoneAsyncRoot

	# input: ()
	# output: (newEventID)
	#   newEventID			A probably-unique ID	
	def _getNewEventID(self):
		newID = self.nextNewEventID
		self.nextNewEventID += 1
		return newID

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
			if cb.isThreadpoolCB():
				event.setLibuvLoopStage(Schedule.THREADPOOL_STAGE)
			elif cb.getCBType() == "INITIAL_STACK":
				pass
			elif cb.getCBType() == "EXIT":
				exiting = True
				libuvLoopStage = "EXITING"
			elif cb.isMarkerNode():
				assert(not exiting)
				if cb.getCBType() == "MARKER_UV_RUN_BEGIN":
					libuvLoopCount += 1

				event.setLibuvLoopStage(Schedule.MARKER_EVENT_TO_STAGE[cb.getCBType()])				
				if self._isBeginMarkerEvent(cb):
					libuvLoopStage = Schedule.MARKER_EVENT_TO_STAGE[cb.getCBType()]
				else:
					assert(self._isEndMarkerEvent(cb))
					libuvLoopStage = Schedule.MARKER_EVENT_TO_STAGE[cb.getCBType()]
			else:
				# Non-marker looper thread CB. These belong to the current libuvLoopStage, whatever that is.
				assert(cb.getCBType() in CB.CallbackNode.TP_TYPES or libuvLoopStage is not None)
				assert(libuvLoopStage is not "UV_RUN")
				event.setLibuvLoopCount(libuvLoopCount)
				event.setLibuvLoopStage(libuvLoopStage)

		return execSchedule

	# input: ()
	# output: (regList) returns list of CallbackNodes in increasing registration order.
	def _regList(self):
		return self.cbTree.getRegOrder()

	# input: ([checkCBTree])
	#    checkCBTree        Check validity of self.cbTree? Default is True
	# output: (isValid) True if this schedule "looks valid" (i.e. like a legal libuv schedule)
	# Goes through each event and verifies that it occurs in a legal place in the schedule
	# Checks: 
	#	  - execID
	#   - libuv stages in the correct order
	#   - non-marker events occur within the correct stage
	#   - TP 'done' events (UV_AFTER_WORK_CB and children) are always preceded by another TP 'done' event or a UV_ASYNC_CB in the TP's async chain
	#   - that self.cbTree.isValid()
	# The validity of a schedule is an invariant to be maintained by each public Schedule method.
	def isValid(self, checkCBTree=True):

		if checkCBTree:
			logging.debug("Checking cbTree")
			if not self.cbTree.isValid():
				logging.debug("cbTree is not valid")
				return False

		# Stack of the current libuv run stage.
		# Each element is in LIBUV_RUN_ALL_STAGES.
		# A stage is append()'d when its BEGIN is encountered, and pop()'d when its END is encountered
		currentLibuvLoopStage = []
		# The UV_RUN loop inner stage we most recently ended
		# Starts set to the final inner stage so that we don't need special cases for "first time through the loop"
		lastEndedInnerStage = Schedule.LIBUV_RUN_INNER_STAGES[-1]
		# If we've encountered the EXIT event
		exiting = False
		# The previous looper event (i.e. not prevLooperEvent.isThreadpoolCB()) we encountered
		prevLooperEvent = None
		
		for actualExecID, event in enumerate(self.execSchedule):
			# Extract some details about this event for convenience			
			eventCB = event.getCB()			
			eventCBType = eventCB.getCBType()
			eventLoopStage = event.getLibuvLoopStage() # This is used to classify BEGIN and END markers.
			
			inAnyStage = (0 < len(currentLibuvLoopStage))
			
			logging.debug("actualExecID {} eventCBType {} eventLoopStage {} inAnyStage {} lastEndedInnerStage {} exiting {}".format(actualExecID, eventCBType, eventLoopStage, inAnyStage, lastEndedInnerStage, exiting))			

			# Event execID must be correct (execIDs must go 0, 1, 2, ...)			
			if int(eventCB.getExecID()) != actualExecID:
				logging.debug("Not valid: node {}: expectedExecID {} but have execID {}".format(eventCB, actualExecID, int(eventCB.getExecID())))
				return False
			
			# Event must be in the correct point of the schedule										
			if eventCB.isThreadpoolCB():
				# Threadpool CBs: nothing to check.
				# These are legal even if we're exiting.
				logging.debug("Threadpool eventCB (type {}) is always legal".format(eventCB.getCBType()))
			else:
				# This is a looper thread event.
				
				if exiting:
					#TODO Which of the two versions of this if statement are correct?
					if True:
						logging.debug("While exiting I encountered an event. How did this happen? It should all happen synchronously within the 'exit' event's callback")
						return False
					else:					
						# When we're exiting, we allow any non-marker looper or threadpool event to occur				
						isValidExitingEvent = (eventCB.isThreadpoolCB() or not eventCB.isMarkerNode() or eventCB)
						if not isValidExitingEvent:
							logging.debug("While exiting, encountered invalid exiting event of type {}".format(eventCBType))
							return False

				# This is a looper event and we're not exiting.
				# It could be a marker event indicating the beginning or ending of a stage, or a callback. 
				#   Marker events must follow the prescribed order for events.
				#   Callbacks must occur within the appropriate stage.
				if eventCBType == "INITIAL_STACK":
					# Every schedule must contain one INITIAL_STACK, as the first event.
					if actualExecID != 0:
						logging.debug("INITIAL_STACK must be the first event in the schedule; its execID is actually {}".format(actualExecID))
						return False
				elif self._isBeginMarkerEvent(eventCB):
					# There is a strict order for marker events
					logging.debug("eventCB is a begin marker event, eventLoopStage {}".format(eventLoopStage))
					
					# If we're beginning a stage, we should either be in no stage or in only the 'UV_RUN' stage (which allows nesting).					
					if inAnyStage and (len(currentLibuvLoopStage) != 1 or currentLibuvLoopStage[0] != "UV_RUN"):
						logging.debug("We're beginning a new stage ({}) but we haven't ended the current stage: {}".format(eventLoopStage, currentLibuvLoopStage))
						return False
										
					# Is this the begin event for a possible next stage?
					possibleNextStages = self._possibleNextStages(currentLibuvLoopStage, lastEndedInnerStage)
					if eventLoopStage not in possibleNextStages:
						logging.debug("eventLoopStage {} is not one of the possibleNextStages {}".format(eventLoopStage, possibleNextStages))
						return False
					
					# Enter the new stage.
					currentLibuvLoopStage.append(eventLoopStage)					
					logging.debug("Entered stage {}".format(currentLibuvLoopStage))			
				elif self._isEndMarkerEvent(eventCB):					 
					# Verify that the correct stage is ending
					if not inAnyStage:
						logging.debug("Not in any stage, but I see an endMarkerEvent {}".format(eventCBType))
						return False
														
					currentStage = currentLibuvLoopStage.pop()				
					if currentStage != eventLoopStage:
						logging.debug("I found an unexpected end marker event: currentStage {} eventLoopStage {}".format(currentStage, eventLoopStage))
						return False
					logging.debug("eventCB is an end marker event, updating libuv run stage")
					if currentStage in Schedule.LIBUV_RUN_INNER_STAGES:
						lastEndedInnerStage = currentStage					
				elif eventCBType == "EXIT":			
					exiting = True
				else:
					# By process of elimination, this is a "normal CB" of some kind.
					# It must be a valid event in the current stage.
					if not inAnyStage:
						logging.debug("eventCBType {} but we are not in any stage".format(eventCBType))
						return False		

					# If we're in a libuv stage, verify that we're looking at a valid eventCB type
					# cf. unified-callback-enums.c::is_X_cb
					currentStage = currentLibuvLoopStage[-1]
					assert(prevLooperEvent is not None)
					valid = self._isValidNormalEvent(currentStage, event, prevLooperEvent)
					if not valid:
						logging.debug("Not a valid normal event")
						return False

				prevLooperEvent = event

		logging.debug("All events looked valid")
		return True

	# input: (currentStage, event, prevEvent)
	#   currentStage    the current stage we're in in as we traverse the schedule. must not be "UV_RUN"
	#   event           the event we're currently assessing
	#   prevEvent       the looper event that preceded us
	# output: isValid   True or False
	#
	# This is a helper for self.isValid.
	# Apply it to "normal" events (not a marker, not during exit, etc.)
	def _isValidNormalEvent(self, currentStage, event, prevEvent):
		# Extract some details about these events for convenience
		eventCB, eventLoopStage = event.getCB(), event.getLibuvLoopStage()
		eventCBType = eventCB.getCBType()

		prevEventCB, prevEventLoopStage = prevEvent.getCB(), prevEvent.getLibuvLoopStage()
		prevEventCBType = prevEventCB.getCBType()
		assert(not prevEventCB.isThreadpoolCB())

		# Confirm that event is in the right stage
		assert(currentStage != "UV_RUN")
		logging.debug("We're in stage {}; verifying current eventCB type {} is appropriate to the stage".format(currentStage, eventCBType))
		if (currentStage == "RUN_TIMERS_1" and eventCB.isRunTimersCB()) or \
			 (currentStage == "RUN_PENDING" and eventCB.isRunPendingCB()) or \
			 (currentStage == "RUN_IDLE" and eventCB.isRunIdleCB()) or \
			 (currentStage == "RUN_PREPARE" and eventCB.isRunPrepareCB()) or \
			 (currentStage == "IO_POLL" and eventCB.isIOPollCB()) or \
			 (currentStage == "RUN_CHECK" and eventCB.isRunCheckCB()) or \
			 (currentStage == "RUN_CLOSING" and eventCB.isRunClosingCB()) or \
			 (currentStage == "RUN_TIMERS_2" and eventCB.isRunTimersCB()):
			# We checked each stage that can contain events, omitting UV_RUN which should never contain user events.
			logging.debug("We're in stage {}; eventCBType {} is appropriate".format(currentStage, eventCBType))
		else:
			logging.debug("We're in stage {}; eventCBType {} is not appropriate".format(currentStage, eventCBType))
			return False

		if eventCBType in CB.CallbackNode.TP_DONE_TYPES:
			# Threadpool 'done' events must obey extra rules about the types of nodes they can follow
			# See nodejsrr/jamie/libuv_src_notes for the rules on this
			if eventCBType in CB.CallbackNode.TP_DONE_INITIAL_TYPES:
				validPredecessorTypes = Schedule.LIBUV_THREADPOOL_DONE_BEGINNING_TYPE + CB.CallbackNode.TP_DONE_INITIAL_TYPES + CB.CallbackNode.TP_DONE_NESTED_TYPES
			elif eventCBType in CB.CallbackNode.TP_DONE_NESTED_TYPES:
				validPredecessorTypes = CB.CallbackNode.TP_DONE_INITIAL_TYPES
			else:
				raise ScheduleException("_isValidNormalEvent: Error, unexpected eventCBType {}".format(eventCBType))

			if prevEventCBType in validPredecessorTypes:
				logging.debug("event cbType {}; predecessor cbType {} is valid".format(eventCBType, prevEventCBType))
			else:
				logging.debug("event cbType {}; predecessor cbType {} is not valid (must be in {})".format(eventCBType, prevEventCBType, validPredecessorTypes))
				return False

		return True
	
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

	# input: (currStages, lastEndedInnerStage) 
	#    currStages: Stack of stage(s) we are in. Subset of LIBUV_RUN_ALL_STAGES. 
	#    lastEndedStage: The UV_RUN inner stage we last ended. Element in LIBUV_RUN_INNER_STAGES.
	# output: (possibleNextStages) The possible next stages in a valid schedule.  
	def _possibleNextStages(self, currStages, lastEndedInnerStage):
		logging.debug("currStages {} lastEndedInnerStage {}".format(currStages, lastEndedInnerStage))
		assert(lastEndedInnerStage in Schedule.LIBUV_RUN_INNER_STAGES)
				
		possibleNextStages = ["EXIT"] # Always an option
		
		# The legal next stage depends on both currStages and lastEndedInnerStage		
		if not len(currStages):
			# We are not currently in any stage. The next stage is the beginning of a loop: UV_RUN.
			possibleNextStages.append("UV_RUN")
		else:
			# Proceed to the next inner loop stage
			# lastEndedInnerStage is set to LIBUV_RUN_INNER_STAGES[-1] at the end of every loop and prior to the first loop. 
			nextStageIx = (Schedule.LIBUV_RUN_INNER_STAGES.index(lastEndedInnerStage) + 1) % len(Schedule.LIBUV_RUN_INNER_STAGES)
			possibleNextStages.append(Schedule.LIBUV_RUN_INNER_STAGES[nextStageIx])				
		
		return possibleNextStages

	# input: (racyNodeIDs)
	#	  racyNodeIDs: list of Callback registration IDs
	# output: (events, eventsToReschedule, pivot)
	#	  events: list, the events to which racyNodeIDs correspond, in original execution order
	#	  eventsToReschedule: list, the events that need to be rescheduled, in original execution order. subset of events.
	#   pivot: one of the events, the "pivot" around which to reschedule the eventsToReschedule
	#
	# Raises a ScheduleException on invalid or insupportable request
	def _validateRescheduleIDs(self, racyNodeIDs):
		events = [e for e in self.execSchedule if int(e.getCB().getID()) in racyNodeIDs]

		# Validate the events
		for event in events:
			assert (event is not None)
			cb = event.getCB()

			logging.info("racy node: {}".format(cb))

			# We cannot flip unexecuted events
			if not cb.executed():
				raise ScheduleException('Error, one of the nodes to flip was not executed')

			# We cannot flip events if there is a happens-before relationship between them
			otherEvents = [e for e in events if e is not event]
			for other in otherEvents:
				if cb.isAncestorOf(other.getCB()):
					raise ScheduleException(
						'Error, one of the nodes to flip is an ancestor of another of the nodes to flip:\n  {}\n  {}'.format(cb,
																																																								 other.getCB()))

		origRacyExecOrder = sorted(events, key=lambda e: int(e.getCB().getExecID()))

		# We can only reschedule async events at the moment
		# In the future we could climb until we find a node we cannot flip (network input?) or an async node (can flip, and then trickle down the effect)
		asyncEvents = [e for e in events if e.getCB().isAsync()]

		# "Fixed events"  are non-async
		nFixedEvents = len(events) - len(asyncEvents)

		# Identify the pivot: the event around which we "pivot" the other events as we reschedule
		pivot = None
		eventsToReschedule = None
		if nFixedEvents == 0:
			# All events are async.
			# Pivot defaults to the earliest-scheduled event
			# TODO This should be a policy decision
			#pivot = origRacyExecOrder[-1]
			pivot = origRacyExecOrder[0]
			eventsToReschedule = [e for e in origRacyExecOrder if e is not pivot]
			logging.debug("All events are async. Using the earliest-scheduled node ({}) as the pivot".format(pivot))
		elif nFixedEvents == 1:
			# One event is not async, so that's the pivot.
			eventsToReschedule = asyncEvents
			pivots = set(events) - set(asyncEvents)
			assert(len(pivots) == nFixedEvents == 1)
			pivot = pivots.pop()
			logging.debug("Event {} is fixed, so that is our pivot".format(pivot))
		else:
			# More than one async event. Cannot handle this.
			types = [e.getCB().getCBType() for e in events]
			raise ScheduleException('Error, multiple non-async nodes. Types of events: {}'.format(types))

		assert(pivot not in eventsToReschedule)
		return origRacyExecOrder, eventsToReschedule, pivot

	# input: (racyNodeExecIDs)
	#	  racyNodeIDs: list of Callback execIDs
	# output: (origToNewExecIDs)
	#   origToNewIDs: dict from racyNodeID to the new execID of the corresponding ScheduleEvent after changing execution order
	# Modifies the schedule so that the ScheduleEvents corresponding to racyNodeIDs are executed in "reverse" order compared to how they actually
	# executed in this Schedule.
	# Throws a ScheduleException if the requested exchange is not feasible.
	def reschedule(self, racyNodeIDs):
		racyNodeIDs_set = set(racyNodeIDs)
		# Need exactly 2 IDs
		if len(racyNodeIDs_set) != 2:
			raise ScheduleException("reschedule: Error, I can only reschedule 2 events; you requested {} unique events: <{}>".format(len(racyNodeIDs_set), racyNodeIDs_set))

		events, eventsToReschedule, pivot = self._validateRescheduleIDs(racyNodeIDs)
		eventIDs = [e.getCB().getID() for e in events]
		eventsToRescheduleIDs = [e.getCB().getID() for e in eventsToReschedule]
		logging.info("eventIDs {} eventsToRescheduleIDs {} pivotID {}".format(eventIDs, eventsToRescheduleIDs, pivot.getCB().getID()))

		# Save original IDs so that we can populate origToNewIDs later
		eventToOrigID = { event: event.getCB().getID() for event in events }

		# Recursively relocate each event and its affected relations.
		for eventToReschedule in eventsToReschedule:
			logging.debug("relocating {}".format(eventToReschedule))
			inserted, insertedIx = self._relocateEvent(eventToReschedule, pivotEvent=pivot)
			if inserted:
				logging.debug("Moved event {} from index {} to index {}".format(eventToReschedule, eventToOrigID[eventToReschedule], insertedIx))
			else:
				raise ScheduleException("Error, could not relocate eventToReschedule {}: {}".format(eventToReschedule, eventToReschedule.getCB()))

		# Update the execID of the events in the schedule
		self._updateExecIDs()

		# Public method, so we must be valid
		assert(self.isValid())

		# TODO Revisit this and make sure it's correct
		# Get the new execID of the rescheduled events
		# (other events have also been rescheduled, but the caller doesn't care about them)
		# At least one ID must have changed, otherwise we haven't done anything!
		origToNewIDs = { eventToOrigID[event]: event.getCB().getID() for event in events }
		changedIDs = [id for id in origToNewIDs.keys() if id != origToNewIDs[id]]
		assert(len(changedIDs))

		return origToNewIDs

	# input: (event, [pivotEvent], [beforeEvent], [afterEvent])
	#   event             insert this event. must be an async event.
	#	 Provide exactly one of the following three locations:
	#   pivotEvent        if defined, insert event on the opposite side of pivot from its current location
	#   afterEvent        if defined, insert event after this event
	#   beforeEvent       if defined, insert event before this event
	#
	#                     if afterEvent or beforeEvent is specified, event must occur (after,before) (beforeEvent,afterEvent).
	#
	# output: (successfullyInserted, newIx)
	#   successfullyInserted     True if we inserted, else False
	#   newIx                    if successfullyInserted: the new index of event
	#
	# Relocate event from its current location in self.execSchedule to a new one relative to the location of another event.
	# This is done by adding an empty UV_RUN loop before or after the one containing the pivot/before/afterEvent, then placing event in it.
	# Modifies self.execSchedule and self.cbTree.
	#
	# May raise a ScheduleException if you really invoke it wrong.
	def _relocateEvent(self, event, pivotEvent=None, beforeEvent=None, afterEvent=None):
		notNones = [e for e in [pivotEvent, beforeEvent, afterEvent] if e is not None]
		if len(notNones) != 1:
			raise ScheduleException("_relocateEvent: Error, you must provide exactly one of {pivotEvent, beforeEvent, afterEvent}")

		eventCB, eventType, eventIx = event.getCB(), event.getCB().getCBType(), self.execSchedule.index(event)

		if not eventCB.isAsync():
			raise ScheduleException("_relocateEvent: Error, event is not async (type {})".format(eventCB.getCBType()))

		if pivotEvent:
			# pivotEvent is convenient shorthand. Convert to beforeEvent or afterEvent.
			pivotIx = self.execSchedule.index(pivotEvent)
			if pivotIx < eventIx:
				logging.debug("pivot {} initially preceded event {}, using it as beforeEvent".format(pivotIx, eventIx))
				beforeEvent = pivotEvent
				pivotEvent = None
			elif pivotIx < eventExecID:
				logging.debug("pivot {} initially came after event {}, using it as afterEvent".format(pivotIx, eventIx))
				afterEvent = pivotEvent
				pivotEvent = None
			else:
				raise ScheduleException("_relocateEvent: Error, pivotEvent {} and event {} have the same exec ID ({})".format(pivotEvent, event, eventIx))
		assert(not pivotEvent)
		
		# Obtain the index of the 'relative event' relative to which we are relocating event.
		# Ensure the caller's request make sense -- event must be before afterEvent or after beforeEvent.
		if beforeEvent:
			relativeEventIx = self.execSchedule.index(beforeEvent)
			if eventIx < relativeEventIx:
				raise ScheduleException("_relocateEvent: Error, event already occurs prior to beforeEvent")
		else:
			relativeEventIx = self.execSchedule.index(afterEvent)
			if relativeEventIx < eventIx:
				raise ScheduleException("_relocateEvent: Error, event already occurs after afterEvent")

		# Identify the event's relocation class and family
		relocationClass = self._getRelocationClass(event)
		relocationFamily = self._findRelocationFamily(event, relocationClass)
		logging.debug("eventType {} relocationClass {} relocationFamily {}".format(eventType, relocationClass, relocationFamily))

		# Insert a new UV_RUN loop to hold the family, either before (for beforeEvent) or after (for afterEvent)
		if beforeEvent:
			newUV_RUNLoopIx = self._findMatchingScheduleEvent(lambda se: se.getCB().getCBType() == Schedule.runBegin, 'earlier', relativeEventIx)
		else:
			newUV_RUNLoopIx = self._findMatchingScheduleEvent(lambda se: se.getCB().getCBType() == Schedule.runBegin, 'later', relativeEventIx)
		if not newUV_RUNLoopIx:
			# This can occur when we have afterEvent and the schedule terminates prematurely, e.g. via an explicit exit() call or an exception
			# It cannot occur with beforeEvent because there is always an initial UV_RUN loop
			assert(afterEvent)
			logging.debug("Error, could not find a newUV_RUNLoopIx -- perhaps this schedule has an early exit?")
			return False, -1
		newUV_RUNLoopIx = self._insertUVRunLoop(newUV_RUNLoopIx, enterLoop=True)
		logging.debug("Inserted a new UV_RUN loop at index {}".format(newUV_RUNLoopIx))

		# Remove the family from execSchedule
		logging.debug("Removing the relocationFamily")
		for event in relocationFamily:
			self.execSchedule.remove(event)

		# Recalculate the ix of the new loop (easy because the relocationFamily was adjacent in self.execSchedule)
		if afterEvent:
			newUV_RUNLoopIx += len(relocationFamily)
		assert(self.execSchedule[newUV_RUNLoopIx].getCB().getCBType() == Schedule.runBegin)

		# Re-insert the family
		logging.debug("Replacing the relocationFamily")
		if relocationClass == RelocationClass.TP_WORK:
			# TP_WORK items can be scheduled safely anywhere that isn't between another nested family.
			# We have a whole UV_RUN loop to play with, so place the family immediately after the beginning of the new UV_RUN loop.
			insertIx = newUV_RUNLoopIx
			logging.debug("relocationClass {}: Replacing the relocationFamily at insertIx {}".format(relocationClass, insertIx))
			for event in relocationFamily:
				self.execSchedule.insert(insertIx, event)
				insertIx += 1
		elif relocationClass == RelocationClass.TP_DONE:
			# Threadpool done items must be executed in the IO_POLL stage, and we need to add a UV_ASYNC_CB to the TP ASYNC chain to hold them.
			# TODO
			# THIS IS NEXT
			raise ScheduleException("_relocateEvent: Error, unsupported relocationClass {}".format(relocationClass))
		elif relocationClass == RelocationClass.GENERAL_LOOPER:
			# Place GENERAL_LOOPER family members after the BEGIN marker of the appropriate type in our new loop and insert it
			searchFunc = lambda se: self._isBeginMarkerEvent(se.getCB()) and se.getLibuvLoopStage() == event.getLibuvLoopStage()
			markerIx = self._findMatchingScheduleEvent(searchFunc, "later", newUV_RUNLoopIx)
			assert(markerIx)
			insertIx = markerIx + 1
			logging.debug("relocationClass {}: Replacing the relocationFamily at insertIx {}".format(relocationClass, insertIx))
			for event in relocationFamily:
				self.execSchedule.insert(insertIx, event)
				insertIx += 1

		# TODO Recurse up or down event's tree to ensure it hasn't jumped over an ancestor (beforeEvent) or a descendant (afterEvent)

		newEventIx = self.execSchedule.index(event)

		return True, newEventIx

	# input: (event)
	#   event             must be an async ScheduleEvent
	# output: (relocationClass)
	#   relocationClass   The 'class' of event. One of RelocationClass.relocationClasses
	#
	# This is a helper for _relocateEvent. A different approach is required for each relocationClass.
	def _getRelocationClass(self, event):
		assert(event.getCB().isAsync())

		eventType = event.getCB().getCBType()
		if eventType in CB.CallbackNode.TP_WORK_TYPES:
			relocationClass = RelocationClass.TP_WORK
		elif eventType in CB.CallbackNode.TP_DONE_TYPES:
			relocationClass = RelocationClass.TP_DONE
		else:
			assert(eventType in CB.CallbackNode.MISC_ASYNC_TYPES)
			relocationClass = RelocationClass.GENERAL_LOOPER
		assert(relocationClass in RelocationClass.relocationClasses)

		logging.debug("eventType {} relocationClass {}".format(eventType, relocationClass))
		return relocationClass

	# input: (event)
	#   event             self.execSchedule index of a ScheduleEvent. Must be async.
	# output: (relocationFamily)
	#   relocationFamily  A 'family' of nodes that must be relocated together because they are really nested CBs.
	#                     All members of the 'family' are in the same libuv loop stage.
	#                     All members of the 'family' occur one after another (exec IDs go up by one).
	#                     relocationFamily is sorted by increasing self.execSchedule index order.
	#
	# This is a helper for _relocateEvent.
	def _findRelocationFamily(self, event, relocationClass):
		assert(event.getCB().isAsync())
		assert(relocationClass in RelocationClass.relocationClasses)
		
		eventIx, eventType = self.execSchedule.index(event), event.getCB().getCBType()

		relocationFamily = [event]
		if relocationClass in RelocationClass.tpClasses:
			# TP_WORK
			if eventType in CB.CallbackNode.TP_WORK_INITIAL_TYPES:
				assert(relocationClass == RelocationClass.TP_WORK)
				siblingEvent = self.execSchedule[eventIx + 1]
				siblingTypeList = CB.CallbackNode.TP_WORK_NESTED_TYPES
			elif eventType in CB.CallbackNode.TP_WORK_NESTED_TYPES:
				assert(relocationClass == RelocationClass.TP_WORK)
				siblingEvent = self.execSchedule[eventIx - 1]
				siblingTypeList = CB.CallbackNode.TP_WORK_INITIAL_TYPES
			# TP_DONE
			elif eventType in CB.CallbackNode.TP_DONE_INITIAL_TYPES:
				assert(relocationClass == RelocationClass.TP_DONE)
				siblingEvent = self.execSchedule[eventIx + 1]
				siblingTypeList = CB.CallbackNode.TP_DONE_NESTED_TYPES
			elif eventType in CB.CallbackNode.TP_DONE_NESTED_TYPES:
				assert(relocationClass == RelocationClass.TP_DONE)
				siblingEvent = self.execSchedule[eventIx - 1]
				siblingTypeList = CB.CallbackNode.TP_DONE_INITIAL_TYPES
			else:
				raise ScheduleException("_findRelocationFamily: Error, unexpected combination of relocationClass {} eventIx {} eventType {}".format(relocationClass, eventIx, eventType))

			siblingEvent = self.execSchedule[siblingEventIx]
			siblingEventType = siblingEvent.getCB().getCBType()
			if siblingEventType not in siblingTypeList:
				raise ScheduleException("_findRelocationFamily: Error, relocationClass {} eventIx {} eventType {}: unexpected siblingCB type {}".format(relocationClass, eventIx, eventType, siblingEventType))
			relocationFamily.append(siblingEvent)

		elif relocationClass == RelocationClass.GENERAL_LOOPER:
			pass # event is already in relocationFamily, and we don't have any family members to carry along
		else:
			raise ScheduleException("_findRelocationFamily: Error, unexpected relocationClass {}".format(relocationClass))

		# event must be in relocationFamily
		assert(event in relocationFamily)

		# All events in relocationFamily must share the same relocationClass
		uniqueClasses = set([self._getRelocationClass(se) for se in relocationFamily])
		assert(len(uniqueClasses) == 1)
		# All events in relocationFamily must share the same libuv loop stage
		uniqueLoopStages = set([se.getLibuvLoopStage() for se in relocationFamily])
		assert(len(uniqueLoopStages) == 1)

		# Sorted by increasing execSchedule index
		relocationFamily = sorted(relocationFamily, key=lambda se: self.execSchedule.index(se))

		logging.debug("_findRelocationFamily: Returning a family of size {} in relocationClass {} and loop stage {}: family {}".format(len(relocationFamily), relocationClass, list(uniqueLoopStages)[0], relocationFamily))
		return relocationFamily

	# input: (event, [includeDependents=True])
	#   event                  event to query
	#   includeDependents      just descendants, or also dependents?
	# output: (executedDescendants)
	#   executedDescendants    list of all executed descendants of event, NOT including event itself
	def _getExecutedDescendants(self, event, includeDependents=True):
		# Identify all events that need to be relocated: event and its descendants and dependents, now referred to as descendants
		executedEventDescendants_CBs = [cb for cb in event.getCB().getDescendants(includeDependents=True) if
																		cb.executed()]
		# Some CBs might be registered (i.e. in self.cbTree) but not executed (i.e. in self.execSchedule)
		matchingEvents = [e for e in self.execSchedule if e.getCB() in executedEventDescendants_CBs]
		assert(len(matchingEvents) <= len(executedEventDescendants_CBs))
		return matchingEvents

	# input: (events)
	#   events          list of events to remove from self.execSchedule
	# output: ()
	#
	#	The removed events remain in self.cbTree
	def _removeEvents(self, events):
		for e in events:
			self.execSchedule.remove(e)

	# input: ([startingIx])
	#   startingIx    Update exec IDs beginning with this ix
	#                 This is for efficiency, use carefully. Default is 0.
	# output: ()
	# Set the execID of each event in self.execSchedule to its index in self.execSchedule
	def _updateExecIDs(self, startingIx=0):
		for ix, event in enumerate(self.execSchedule[startingIx:]):
			newExecID = startingIx + ix
			logging.debug("event {} type {}: newExecID {}".format(event, event.getCB().getCBType(), newExecID))
			event.getCB().setExecID(newExecID)

	# input: (startIx)
	# output: (eventIx)
	# Finds the first "looper" ScheduleEvent in self.execSchedule after startIx
	# See _findMatchingScheduleEvent for details.
	def _findNextLooperScheduleEvent(self, startIx):
		return self._findMatchingScheduleEvent(lambda se: not se.getCB().isThreadpoolCB(), "later", startIx)

	# input: (startIx)
	# output: (eventIx)
	# Finds the first "looper" ScheduleEvent in self.execSchedule before startIx
	# See _findMatchingScheduleEvent for details.
	def _findPrevLooperScheduleEvent(self, startIx):
		return self._findMatchingScheduleEvent(lambda se: not se.getCB().isThreadpoolCB(), "earlier", startIx)

	# input: (startIx)
	# output: (eventIx)
	# Finds the first threadpool ScheduleEvent in self.execSchedule after startIx
	# See _findMatchingScheduleEvent for details.
	def _findNextTPScheduleEvent(self, startIx):
		return self._findMatchingScheduleEvent(lambda se: se.getCB().isThreadpoolCB(), "later", startIx)

	# input: (startIx)
	# output: (eventIx)
	# Finds the first threadpool ScheduleEvent in self.execSchedule before startIx
	# See _findMatchingScheduleEvent for details.
	def _findPrevTPScheduleEvent(self, startIx):
		return self._findMatchingScheduleEvent(lambda se: se.getCB().isThreadpoolCB(), "earlier", startIx)

	# input: (searchFunc, direction, startIx)
	#   searchFunc: when invoked on a ScheduleEvent, returns True if match, else False
	#   direction: 'earlier' or 'later'
	#   startIx: where to start looking?	
	# output: (eventIx)
	# Find the index of the first ScheduleEvent in self.execSchedule for which searchFunc(e) evaluates to True
	# The first considered event is at startIx and we continue in the direction specified
	# Returns (None) if not found
	def _findMatchingScheduleEvent(self, searchFunc, direction, startIx):
		assert(searchFunc is not None)
		assert(direction in ['earlier', 'later'])
		assert(startIx is not None)

		minIx = 0
		maxIx = len(self.execSchedule) - 1		
		if startIx < minIx:
			startIx = minIx			
		if maxIx < startIx:
			startIx = maxIx

		if direction == 'earlier':
			lastIx = minIx			
			sliceStride = -1			
		else:
			lastIx = maxIx
			sliceStride = 1

		for i, scheduleEvent in enumerate(self.execSchedule[startIx:lastIx:sliceStride]):
			if searchFunc(scheduleEvent):
				logging.debug("Match!")
				realIx = startIx + sliceStride*i
				assert(self.execSchedule[realIx] == scheduleEvent) # Math is hard, did I get it right?
				return realIx
		return None

	# input: (newLoopIx, [enterLoop])
	#		newLoopIx		 ix of a MARKER_UV_RUN_BEGIN event before which we will insert a new loop
	#   enterLoop    Insert just the UV_RUN stage or the inner stages (timers, etc.) too?
	#                Default is True
	# output: (ixOfBeginningOfNewLoop)
	#   ixOfBeginningOfNewLoop    The index of the MARKER_UV_RUN_BEGIN ScheduleEvent beginning the new loop
	#
	# Modifies self.execSchedule and self.cbTree.
	# The execIDs of all events after newLoopIx nodes are modified.
	def _insertUVRunLoop(self, newLoopIx, enterLoop=True):
		#newLoopIx must be to a MARKER_UV_RUN_BEGIN CB.
		assert(self.execSchedule[newLoopIx].getCB().getCBType() == Schedule.runBegin)

		logging.debug("Adding new UV_RUN loop at index {}".format(newLoopIx))

		# What stages will we be inserting?
		if enterLoop:			
			markersToInsert = Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER
		else:
			markersToInsert = Schedule.LIBUV_LOOP_OUTER_STAGE_MARKER_ORDER

		# Insert the new stages into self.execSchedule.
		dummyCallbackString = self.execSchedule[0].getCB().callbackString
		for i, markerType in enumerate(markersToInsert):
			insertIx = newLoopIx + i
			logging.debug("Inserting marker of type {} at index {}".format(markerType, insertIx))
			# Create a marker CallbackNode.
			markerCB = CB.CallbackNode(dummyCallbackString)
			markerCB.setCBType(markerType)
			markerCB.setChildren([])
			markerCB.setParent(None)	
			markerCB.setName("0x{}".format(self._getNewEventID())) # Unique name						
			# Create a marker ScheduleEvent.
			scheduleEvent = ScheduleEvent(markerCB)
			scheduleEvent.setLibuvLoopStage(Schedule.MARKER_EVENT_TO_STAGE[markerType])
			# Insert it.
			self.execSchedule.insert(insertIx, scheduleEvent)

		# We've tweaked the exec schedule and the tree structure.
		# Get the tree back to a consistent state.
		logging.debug("Returning self.execSchedule and self.cbTree to a consistent state")
		self._updateExecIDs() # Need to do this in order to repair the cbTree
		self._updateMarkerInheritance()
		self.cbTree.repairAfterUpdates()

		# We don't know if self.isValid() because we don't know the state of things when we were called.

		# Since we inserted this loop before the existing loop, newLoopIx is the beginning of the new loop.
		assert(self.execSchedule[newLoopIx].getCB().getCBType() == Schedule.runBegin)
		# and the original loop is still there, right?
		assert(self.execSchedule[newLoopIx + len(markersToInsert)].getCB().getCBType() == Schedule.runBegin)

		logging.debug("Returning newLoopIx {} (the new loop runs from indices {} to {})".format(newLoopIx, newLoopIx, newLoopIx + len(markersToInsert) - 1))
		return newLoopIx
	
	# input: ()
	# output: ()	
	#
	# Ensure the marker events are valid in self.execSchedule.
	# This may involve modifying self.cbTree to put it in a valid state.
	# Updates the parent, children, and tree_parent fields of the marker events.	
	#
	# Marker events should be in direct lineage INITIAL_STACK -> M1 -> M2 -> M3 -> ...
	# With the exception of the INITIAL_STACK, markers have no other children.		  
	def _updateMarkerInheritance(self):		
		initialStackFindFunc = lambda e: e.getCB().getCBType() == "INITIAL_STACK"
		initialStackIx = self._findMatchingScheduleEvent(searchFunc=initialStackFindFunc, direction="later", startIx=0)
		initialStackEvent = self.execSchedule[initialStackIx]
		assert(initialStackIx == 0 and initialStackEvent is not None)

		# Helper for _updateMarkerInheritance.
		# markerParent is the ScheduleEvent corresponding to the parent of the next marker.
		# At the beginning of each loop iteration, it has no markers in its list of children.
		def _prepMarkerParent (markerParent):
			children = markerParent.getCB().getChildren()
			markerParent.getCB().setChildren([c for c in children if c.getCBType() not in Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER])

		markerParent = initialStackEvent
		_prepMarkerParent(markerParent)

		markerEvents = [e for e in self.execSchedule if e.getCB().getCBType() in Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER]
		for markerEvent in markerEvents:
			# Update the markerParent <-> markerEvent relationship
			mpCB = markerParent.getCB()
			meCB = markerEvent.getCB()

			meCB.setTreeLevel(int(mpCB.getTreeLevel()) + 1)
			mpCB.addChild(meCB)
			meCB.setParent(mpCB) # Correct CBTree parent
			meCB.setTreeParent(mpCB.getName()) # Correct libuv parent

			markerParent = markerEvent
			_prepMarkerParent(markerParent)

	# input: (file)
	#	 place to write the schedule
	# output: ()
	# Emits this schedule to the specified file, sorted on registration order.
	# (sorting in registration order is required by the libuv scheduler API)
	#
	# May raise IOError
	def emit(self, file):
		assert (self.isValid())
		regOrder = self._regList()
		# This will fail if self.execSchedule and self.cbTree have gotten significantly out of sync.
		# There may be unexecuted registered events.
		assert(len(self.execSchedule) <= len(regOrder))
		
		logging.info("Emitting schedule ({} registered events) to file {} in registration order".format(len(regOrder), file))
		with open(file, 'w') as f:
			for cb in regOrder:
				f.write("%s\n" % (cb))

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
