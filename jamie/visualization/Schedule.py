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
# External functions always return leaving the Schedule with isValid True.
# Internal functions (self._*) might not. 
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
		
		self.nextNewEventID = 999999
		
		if not self.isValid():
			raise ScheduleException("The input schedule was not valid")		

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
				pass
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

	# input: ()
	# output: (isValid) True if this schedule "looks valid" (i.e. like a legal libuv schedule)
	# Goes through each event and verifies that it occurs in a legal place in the schedule
	# Checks: 
	#	  - execID
	#   - libuv stages in the correct order
	#   - non-marker events occur within the correct stage
	# The validity of a schedule is an invariant to be maintained by each Schedule method. 
	def isValid(self):
		# Stack of the current libuv run stage.
		# Each element is in LIBUV_RUN_ALL_STAGES.
		# A stage is append()'d when its BEGIN is encountered, and pop()'d when its END is encountered
		currentLibuvLoopStage = []
		# The UV_RUN loop inner stage we most recently ended
		# Starts set to the final inner stage so that we don't need special cases for "first time through the loop"
		lastEndedInnerStage = Schedule.LIBUV_RUN_INNER_STAGES[-1]
		# If we've encountered the EXIT event
		exiting = False
		
		for execID, event in enumerate(self.execSchedule):
			# Extract some details about this event for convenience			
			eventCB = event.getCB()			
			eventCBType = eventCB.getCBType()
			eventLoopStage = event.getLibuvLoopStage() # This is used to classify BEGIN and END markers.
			
			inAnyStage = (0 < len(currentLibuvLoopStage))
			
			logging.debug("execID {} eventCBType {} eventLoopStage {} inAnyStage {} lastEndedInnerStage {} exiting {}".format(execID, eventCBType, eventLoopStage, inAnyStage, lastEndedInnerStage, exiting))			

			# Event execID must be correct (execIDs must go 0, 1, 2, ...)			
			if int(eventCB.getExecID()) != execID:
				logging.debug("node {}, execID {}, expectedExecID {}".format(eventCB, int(eventCB.getExecID()), execID))
				return False
			
			# Event must be in the correct point of the schedule										
			if eventCB.isThreadpoolCB():
				# Threadpool CBs: nothing to check.
				# These are legal even if we're exiting.
				logging.debug("threadpool eventCB (type {}) is always legal".format(eventCB.getCBType()))								
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
					if execID != 0:
						logging.debug("INITIAL_STACK must be the first event in the schedule; its execID is actually {}".format(execID))
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
					# By process of elimination, this must be a valid event in the current stage.
					if not inAnyStage:
						logging.debug("eventCBType {} but we are not in any stage".format(eventCBType))
						return False		
			
					# If we're in a libuv stage, verify that we're looking at a valid eventCB type
					# cf. unified-callback-enums.c::is_X_cb
					currentStage = currentLibuvLoopStage[-1]
					logging.debug("We're in stage {}; verifying current eventCB type {} is appropriate to the stage".format(currentStage, eventCBType))
					if (currentStage == "RUN_TIMERS_1" and eventCB.isRunTimersCB()) or \
						 (currentStage == "RUN_PENDING" and eventCB.isRunPendingCB()) or \
						 (currentStage == "RUN_IDLE" and eventCB.isRunIdleCB()) or \
						 (currentStage == "RUN_PREPARE" and eventCB.isRunPrepareCB()) or \
						 (currentStage == "IO_POLL" and eventCB.isIOPollCB()) or \
						 (currentStage == "RUN_CHECK" and eventCB.isRunCheckCB()) or \
						 (currentStage == "RUN_CLOSING" and eventCB.isRunClosingCB()) or \
						 (currentStage == "RUN_TIMERS_2" and eventCB.isRunTimersCB()):
						#We checked each stage that can contain events, omitting UV_RUN which should never contain user events.						
						logging.debug("We're in stage {}; eventCBType {} is appropriate".format(currentStage, eventCBType))
					else:						
						logging.debug("We're in stage {}; eventCBType {} is not appropriate".format(currentStage, eventCBType))
						return False
					
		logging.debug("All events looked valid")
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
		# In the future we could climb until we find network input (cannot flip) or an async node (can flip)
		asyncEvents = [e for e in events if e.getCB().isAsync()]

		# "Fixed events"  are non-async
		nFixedEvents = len(events) - len(asyncEvents)

		# Identify the pivot: the event around which we "pivot" the other events as we reschedule
		pivot = None
		eventsToReschedule = None
		if nFixedEvents == 0:
			# All events are async.
			# Pivot defaults to the latest-scheduled event
			# TODO This should be a policy decision
			#pivot = origRacyExecOrder[-1]
			pivot = origRacyExecOrder[0]
			eventsToReschedule = [e for e in origRacyExecOrder if e is not pivot]
			logging.debug("All events are async. Using the latest-scheduled ({}) as the pivot".format(pivot))
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

		return origRacyExecOrder, eventsToReschedule, pivot

	# input: (racyNodeIDs)
	#	  racyNodeIDs: list of Callback execution IDs
	# output: (origToNewIDs)
	#   origToNewIDs: dict from racyNodeID entries to newNodeID entries after changing execution order
	# Modifies the schedule so that the specified Callbacks are executed in reverse order compared to how they actually
	# executed in this Schedule.
	# Throws a ScheduleException if the requested exchange is not feasible
	def reschedule(self, racyNodeIDs):
		assert(0 < len(racyNodeIDs))
		if len(racyNodeIDs) != 2:
			raise ScheduleException('Error, cannot reschedule more than 2 events')
		origToNewIDs = {}

		events, eventsToReschedule, pivot = self._validateRescheduleIDs(racyNodeIDs)
		eventIDs = [e.getCB().getID() for e in events]
		eventsToRescheduleIDs = [e.getCB().getID() for e in eventsToReschedule]
		logging.info("eventIDs {} eventsToRescheduleIDs {} pivotID {}".format(eventIDs, eventsToRescheduleIDs, pivot.getCB().getID()))

		# Save original IDs so that we can populate origToNewIDs later
		eventToOrigID = {}
		for event in events:
			eventToOrigID[event] = event.getCB().getID()

		# Map from an event to all events that descend from or depend on it (not including the event itself)
		eventToEventsToMove = {}

		# Remove all eventsToReschedule (including their descendants) from the schedule
		for event in eventsToReschedule:
			eventToEventsToMove[event] = self._removeEvent(event, recursive=True)

		for eventToReschedule in eventsToReschedule:
			# Insert eventToReschedule and its descendants
			eventsToMove = eventToEventsToMove[eventToReschedule]
			if not self._insertEvent(eventToReschedule, pivotEvent=pivot):
				raise ScheduleException("Error, cannot insert eventToReschedule {}: {}".format(eventToReschedule, eventToReschedule.getCB()))
			
			# Maintain the execution order within the descendant tree
			descendantsToMove = sorted([e for e in eventsToMove if e is not eventToReschedule], key=lambda e: int(e.getCB().getExecID()))			
			mustComeAfterEvent = eventToReschedule
			for descendantToMove in descendantsToMove:
				if not self._insertEvent(descendantToMove, afterEvent=mustComeAfterEvent):
					raise ScheduleException("Error, cannot insert descendant {}: {}".format(descendantToMove, descendantToMove.getCB()))
				mustComeAfterEvent = descendantToMove

		# Update the execID of the events in the schedule
		# Get the new execID of the rescheduled events
		# (other events have also been rescheduled, but the caller doesn't care about them)
		self._updateExecIDs()
		for event in eventToOrigID:
			origToNewIDs[eventToOrigID[event]] = event.getCB().getID()

		# At least one ID must have changed
		changedIDs = [id for id in origToNewIDs.keys() if id != origToNewIDs[id]]
		assert(len(changedIDs))

		assert (self.isValid())
		return origToNewIDs

	# input: (event, [nInsertAttempts], [pivotEvent], [beforeEvent], [afterEvent])
	#   event: insert this event
	#		nInsertAttempts: how many times have we tried to insert this event? 
	#										 use to control recursion
	#	  Provide exactly one of the following three locations:
	#   pivotEvent: if defined, insert event on the opposite side of pivot
	#   afterEvent: if defined, insert event after this event
	#   beforeEvent: if defined, insert event before this event
	# output: (successfullyInserted) True if we inserted, else False
	#
	# Insert an event into self.execSchedule relative to some other event.
	# It must already be in self.cbTree.
	# It will be inserted at the nearest point that fits the description.
	# On successful insertion, self.execSchedule will be modified. self.cbTree may be modified.
	#
	# May raise a ScheduleException if you really invoke it wrong.
	def _insertEvent(self, event, nInsertAttempts=1, pivotEvent=None, beforeEvent=None, afterEvent=None):
		notNones = [e for e in [pivotEvent, beforeEvent, afterEvent] if e is not None]
		if len(notNones) != 1:
			raise ScheduleException("_insertEvent: Error, you must provide exactly one of {pivotEvent, beforeEvent, afterEvent}")

		assert(not event.getCB().isThreadpoolCB())

		evExecID = int(event.getCB().getExecID())

		# Find an endMarkerEvent of the same stage as event
		findMarkerFunc = None
		searchStartIx = None
		moveIxFunc = None
		calcInsertIxFunc = None

		if pivotEvent is not None:
			# pivotEvent is convenient shorthand. Convert to beforeEvent or afterEvent.
			pivExecID = int(pivotEvent.getCB().getExecID())
			if pivExecID < evExecID:
				logging.debug("pivot {} initially preceded event {}, recursing with beforeEvent=pivot".format(pivExecID, evExecID))
				return self._insertEvent(event, beforeEvent=pivotEvent)
			elif evExecID < pivExecID:
				logging.debug("pivot {} initially came after event {}, recursing with afterEvent=pivot".format(pivExecID, evExecID))
				return self._insertEvent(event, afterEvent=pivotEvent)
			else:
				raise ScheduleException("_insertEvent: Error, pivotEvent {} and event {} have the same exec ID ({})".format(pivotEvent, event, evExecID))
		elif beforeEvent is not None:
			# Prepare to insert event before beforeEvent
			befEventExecID = int(beforeEvent.getCB().getExecID())
			if evExecID == befEventExecID:
				raise ScheduleException("_insertEvent: Error, you are requesting that I insert event with execID {} before beforeEvent with the same execID".format(evExecID))
			# Insert after the next beginMarkerEvent of the appropriate type
			findMarkerFunc = lambda e: self._isBeginMarkerEvent(e.getCB()) and e.getLibuvLoopStage() == event.getLibuvLoopStage()
			searchStartIx = self.execSchedule.index(beforeEvent)
			searchDirection = 'earlier'
			calcInsertIxFunc = lambda ix: ix + 1 # Insert after the beginMarkerEvent we found
		elif afterEvent is not None:
			# Prepare to insert event after afterEvent
			aftEventExecID = int(afterEvent.getCB().getExecID())
			if evExecID == aftEventExecID:
				raise ScheduleException("_insertEvent: Error, you are requesting that I insert event with execID {} after afterEvent with the same execID".format(evExecID))
			# Insert before the next endMarkerEvent of the appropriate type
			findMarkerFunc = lambda e: self._isEndMarkerEvent(e.getCB()) and e.getLibuvLoopStage() == event.getLibuvLoopStage()
			searchStartIx = self.execSchedule.index(afterEvent)
			searchDirection = 'later'
			calcInsertIxFunc = lambda ix: ix  # Insert before the endMarkerEvent we found
		else:
			assert(not "_insertEvent: How did we get here?")

		# Search for the insertion point
		# If we're inserting before, we look "left" (smaller ix) for the nearest BEGIN marker event of the correct type
		# If we're inserting after, we look "right" (larger ix) for the nearest END marker event of the correct type
		assert(findMarkerFunc is not None and searchStartIx is not None and searchDirection is not None and calcInsertIxFunc is not None)
		
		inserted = False
		matchIx, matchEvent = self._findMatchingScheduleEvent(searchFunc=findMarkerFunc, direction=searchDirection, startIx=searchStartIx)		
		if matchIx is not None:			
			insertIx = calcInsertIxFunc(matchIx)
			logging.debug("Found an insertion point at index {}".format(insertIx))
			self.execSchedule.insert(insertIx, event)
			inserted = True
		elif nInsertAttempts == 1:			
			logging.debug("Couldn't find an insertion point on the first attempt.")
						
			if event.getCB().isThreadpoolCB():
				raise ScheduleException("_insertEvent: Sorry, cannot handle threadpool CBs yet")
			else:				
				# Looper event.
				# Add another uv_run loop and try again.
				recursionArgs = { "event": event,
												"nInsertAttempts" : nInsertAttempts + 1,
												"pivotEvent": None,
												"beforeEvent": None,
												"afterEvent": None,
												 }			
				if beforeEvent is not None:
					newLoopLocation = "beginning"
					recursionArgs["beforeEvent"] = beforeEvent
				elif afterEvent is not None:
					newLoopLocation = "end"
					recursionArgs["afterEvent"] = afterEvent
				else:
					assert(not "_insertEvent: How did we get here?")
				logging.debug("Inserting a new UV_RUN loop and trying again")
				self._addUVRunLoop(newLoopLocation)
				inserted = self._insertEvent(**recursionArgs) # Unpack -- https://docs.python.org/2/tutorial/controlflow.html#unpacking-argument-lists
				# inserted can still be false here.
				# Example: perhaps we're trying to insert with afterEvent the final event before an inconvenient EXIT.
		
		logging.debug("Returning inserted {}".format(inserted))
		return inserted

	# input: (event, recursive)
	#   event: event to remove from self.execSchedule
	#   recursive: remove children, grandchildren, etc.?
	# output: (removedEvents)
	#  	all events removed from self.execSchedule
	#   these are the executed events out of {event (and descendants, if recursive)}
	#	The removed events remain in self.cbTree
	# removedEvents is a subset of the descendants of event, since it excludes unexecuted descendants
	def _removeEvent(self, event, recursive=False):
		# Identify all events that need to be relocated: event and its descendants and dependents, now referred to as descendants
		allRemovedEvents = [event]
		if recursive:
			executedEventDescendants_CBs = [cb for cb in event.getCB().getDescendants(includeDependents=True) if
																			cb.executed()]
			for cb in executedEventDescendants_CBs:
				matchingEvents = [e for e in self.execSchedule if e.getCB() is cb]
				# This descendant might have been registered (in self.cbTree) but not executed (in self.execSchedule)
				assert(len(matchingEvents) <= 1)
				if len(matchingEvents) == 1:
					allRemovedEvents += matchingEvents

		# Actually remove the allRemovedEvents
		for e in allRemovedEvents:
			self.execSchedule.remove(e)
		return allRemovedEvents

	# input: ()
	# output: ()
	# Update the execID of each event in self.execSchedule based on its list index
	def _updateExecIDs(self):
		for ix, event in enumerate(self.execSchedule):
			event.getCB().setExecID(ix)

	# input: (searchFunc, direction, startIx)
	#   searchFunc: when invoked on a ScheduleEvent, returns True if match, else False
	#   direction: 'earlier' or 'later'
	#   startIx: where to start looking?	
	# output: (eventIx, event)
	# Find the first ScheduleEvent in self.execSchedule for which searchFunc(e) evaluates to True
	# The first considered event is at startIx and we continue in the direction specified
	# Returns (None, None) if not found
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
				assert(self.execSchedule[realIx] == scheduleEvent) # Math is hard. 
				return (realIx, scheduleEvent)
		return (None, None)

	# input: (newLoopLocation, [enterLoop])
	#		newLoopLocation		'beginning' or 'end'; add a new loop at the beginning of self.execSchedule or at the end?
	#   enterLoop: Insert just the UV_RUN stage or the inner stages (timers, etc.) too?
	#							 Default is True
	# output: (ixOfBeginningOfNewLoop)
	#   ixOfBeginningOfNewLoop		The index of the MARKER_UV_RUN_BEGIN beginning the new loop.
	# Add a new UV_RUN loop, including both the UV_RUN markers and the internal event markers.
	# The new loop is added before the first loop or after the last complete loop.  	
	# Adds MARKER_* ScheduleEvents to self.execSchedule and self.cbTree.
	#
	# Returns with the Schedule in a valid state
	# Returns None if it cannot add a new UV_RUN loop.
	# This only occurs if there is no complete UV_RUN loop in self.execSchedule.
	def _addUVRunLoop(self, newLoopLocation, enterLoop=True):
		assert(newLoopLocation == "beginning" or newLoopLocation == "end")

		# What stages will we be inserting?
		if enterLoop:			
			markersToInsert = Schedule.LIBUV_LOOP_STAGE_MARKER_ORDER
		else:
			markersToInsert = Schedule.LIBUV_LOOP_OUTER_STAGE_MARKER_ORDER
				
		# Locate the appropriate insertion point for the new loop. 
		if (newLoopLocation == "beginning"):
			searchFunc = lambda e: e.getCB().getCBType() == "MARKER_UV_RUN_BEGIN"
			searchDirection = 'later'
			searchStartIx = 0
			calcInsertIxFunc = lambda ix: ix # Insert prior to this UV_RUN BEGIN 
		else:
			searchFunc = lambda e: e.getCB().getCBType() == "MARKER_UV_RUN_END"
			searchDirection = 'earlier'
			searchStartIx = len(self.execSchedule) - 1
			calcInsertIxFunc = lambda ix: ix + 1 # Insert after this UV_RUN END
		(ixOfLoop, scheduleEvent) = self._findMatchingScheduleEvent(searchFunc=searchFunc, direction=searchDirection, startIx=searchStartIx)
		if ixOfLoop is None:
			logging.debug("Found no place to insert a new loop, returning None")
			return None
		ixOfNewLoop = calcInsertIxFunc(ixOfLoop)

		# Insert the new stages into self.execSchedule.
		insertIx = ixOfNewLoop
		dummyCallbackString = self.execSchedule[0].getCB().callbackString
		logging.debug("Adding new UV_RUN loop at index {}".format(insertIx))
		for markerType in markersToInsert:
			logging.debug("Adding marker of type: {}".format(markerType))
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

			insertIx += 1

		# We've tweaked the exec schedule and the tree structure.
		# Get the tree back to a consistent state.
		logging.debug("Returning tree to a consistent state")
		self._updateExecIDs()
		self._updateMarkerInheritance()
		self.cbTree.repairAfterUpdates()

		# This schedule must be valid now.
		assert(self.isValid())
		logging.debug("Tree is valid. Returning {}".format(ixOfNewLoop))
		return ixOfNewLoop
	
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
		(initialStackIx, initialStackEvent) = self._findMatchingScheduleEvent(searchFunc=initialStackFindFunc, direction="later", startIx=0)		
		assert(initialStackEvent is not None and initialStackIx == 0)

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
