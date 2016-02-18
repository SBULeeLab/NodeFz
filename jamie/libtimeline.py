#Author: Jamie Davis (davisjam@vt.edu)
#Description: Package for interacting with libtimeline XML trees
# Defines the following public classes:
#	 Timeline
#	 Event
# Defines the following private classes:
#	 Category
#  Container
# Python version: 2.7.6

import xml.etree.ElementTree as ET
import xml.dom.minidom as DOM
import re
import logging
import colorsys
import random

logging.basicConfig(level=logging.INFO)

#input: (numColors)
#output: (colors)
#Returns a list of NUMCOLORS tuples of form (R,G,B), with 0 <= R,G,B <= 256 
#Credit: Uri Cohen, http://stackoverflow.com/questions/470690/how-to-automatically-generate-n-distinct-colors
def _genColors (numColors):
	randState = random.getstate()
	
	random.seed(0) #ensure colors are the same every time this module is used 
	colors = []
	stepSize = 360/numColors
	for i in range(0, numColors):
		hue = i*stepSize/360.0
		lightness = (50 + random.random() * 10)/100.
		saturation = (90 + random.random() * 10)/100.
		rgb = colorsys.hls_to_rgb(hue, lightness, saturation)
		rgb = [int(256*x) for x in rgb] # [0,1] to [0, 256]		
		colors.append(rgb)
	
	#sort randomly so that we don't have like colors adjacent to each other	
	colors.sort(key=lambda x: random.random())
	
	random.setstate(randState)
	return colors

#############################
# Event
#############################

#representation of an Event
#Members: start, end, text, categoryName, containerName [, containerID]
#	In TheTimelineProj, category represents the type of an event, and containers group "like" events
class Event:

	#give one of:
	# xml (an Element)
	# eventString (a string of fields formatted as: 'Callback X: | <name> <value> | <name> <value> | .... |'
	# {start, end, text, categoryName, containerName} (strings)
	def __init__ (self, xml=None, eventString="", start=-1, end=-1, text="", categoryName="", containerName="", description=""):
		if (xml):
			self.fromXML(xml)
		elif (eventString):
			#Callback 34:  | <cbn> <0x2fb7930> | <id> <35> | <info> <0x2f9e9f0> | <type> <UV_SHUTDOWN_CB> | <logical level> <0> | <physical level> <0> | <logical parent> <(nil)> | <logical parent_id> <-1> | <logical parent> <(nil)> | <logical parent_id> <-1> |<active> <0> | <n_physical_children> <0> | <n_logical_children> <0> | <client_id> <-3> | <relative start> <12s 948599137ns> | <duration> <0s 4729ns> |
			kvs = eventString.split("|")
			for kv in kvs:
				match = re.search('<(?P<key>.*?)>\s+<(?P<value>.*)>', kv)
				if (match):
					setattr(self, match.group('key'), match.group('value'))
					
			#compute the standard event fields: start, end, text, categoryName, containerName
			
			#convert 'relative start, duration' to 'start, end'
			relative_start = getattr(self, 'relative start', None)
			duration = getattr(self, 'duration', None)
			assert(relative_start and duration)
			
			match = re.search('(?P<sec>\d+)s\s+(?P<nsec>\d+)ns', relative_start)
			assert(match)
			self.start = int(match.group('sec'))*1e9 + int(match.group('nsec'))
			
			match = re.search('(?P<sec>\d+)s\s+(?P<nsec>\d+)ns', duration)
			assert(match)
			duration_ns = int(match.group('sec'))*1e9 + int(match.group('nsec'))
			self.end = self.start + duration_ns
			assert(getattr(self, 'id', None) and getattr(self, 'type', None))
			self.text = "%s: %s" % (getattr(self, 'id', None), getattr(self, 'type', None))			
			
			self.categoryName = getattr(self, 'executing_thread', None)
			assert(self.categoryName)
			if (int(self.categoryName) != -1):
				self.containerName = self.categoryName			

			#compose an alt-text description based on other available data
			assert(getattr(self, 'client_id', None) and getattr(self, 'logical parent_id', None))
			self.description = "clientID %s hasLogicalParent %s" %(getattr(self, 'client_id', None), '0' if getattr(self, 'logical parent_id', None) == str(-1) else '1')
		else:
			self.start = start
			self.end = end
			self.text = text
			self.categoryName = categoryName
			self.containerName = containerName
			self.description = description
			
		assert(self.start <= self.end)
		logging.debug("Event::__init__: start %d end %d" % (self.start, self.end))	
		logging.debug("Event::__init__: Initialized event: %s" % self)
			
	def __str__ (self):
		return "Event '%s': start '%d' end '%d' category '%s' container '%s'" % (self.text, self.start, self.end, self.categoryName, self.containerName)
	
	def __cmp__ (self, other):
		if (self.text < other.text):
			return -1
		elif (self.text == other.text):
			return 0
		else:
			return 1
		
	#input: (otherEvent)
	#output: True if self and otherEvent overlap, else False
	#This function is symmetric: A.overlaps(B) == B.overlaps(A)
	#Pictorial representation of the possible forms of overlap:
	# AAAA    <-- right overlap (right end of AAAA overlaps with self [BBB])
	#   BBB   <-- self
	#     CCC <-- left overlap
	#  DDDDD  <-- envelope
	#Two events do overlap if one starts at the exact same time the other ends, or vice versa 
	def overlaps (self, other):
		isEnvelope     = (other.start <= self.start and self.end <= other.end)
		isRightOverlap = (self.start <= other.start and other.start <= self.end)
		isLeftOverlap  = (other.start <= self.start and self.start <= other.end) 
		return (isEnvelope or isRightOverlap or isLeftOverlap)
	
	#input: (otherEvent)
	#output: True if self ends before OTHER starts, else False
	#For True, OTHER's start must come after self's end
	def endsBeforeStarts (self, other):
		return (self.end < other.start)

	#return an Element representing this Event
	#If element has a containerName, it must also have a containerID
	def toXML (self):
		elt = ET.Element('event')
		ET.SubElement(elt, 'start').text = str(self.start) 
		ET.SubElement(elt, 'end').text = str(self.end)
		#in TheTimelineProj, being in container X is indicated by embedding (X) at the beginning of the text string 
		if (self.containerName):
			assert(self.containerID)
			ET.SubElement(elt, 'text').text = "(%d)%s" % (self.containerID, self.text)
		else:
			ET.SubElement(elt, 'text').text = self.text
		ET.SubElement(elt, 'progress').text = '0'
		ET.SubElement(elt, 'fuzzy').text = 'False'
		ET.SubElement(elt, 'locked').text = 'True'
		ET.SubElement(elt, 'ends_today').text = 'False'
		if (self.categoryName):
		  ET.SubElement(elt, 'category').text = self.categoryName
		if (self.description):
			ET.SubElement(elt, 'description').text = self.description
		ET.SubElement(elt, 'default_color').text = '200,200,200'
		return elt
	
	#Set this Event's members based on elt
	def fromXML (self, elt):
		for attr in (['start', 'end', 'text', 'progress', 'fuzzy', 'locked', 'ends_today', 'category', 'default_color']):
			childElt = elt.find(attr)
			if (childElt):
				setattr(self, attr, childElt.text)
			else:
				setattr(self, attr, '')

		#extract containerName, if any
		match = re.search('^\((?P<container>\d+)\)(?P<text>.*)', self.text)
		if (match):
			self.containerName = match.group('container')
			self.text = match.group('text')
		else:
			self.containerName = ''

#############################
# Era
#############################

#representation of an Era
#Members: name, start, end, color
class Era:
	__defaultColor = "0,0,0"
	
	#give either xml (an Element) or {name, start, end, maskedDuration}
	def __init__ (self, xml=None, name="", start=-1, end=-1, maskedDuration=-1):
		if (xml):
			self.fromXML(xml)
		else:
			self.name = str(name)
			self.start = start
			self.end = end
			self.maskedDuration = maskedDuration
			self.color = self.__defaultColor
			
	def __str__ (self):
		return "Era '%s' start %d end %d color '%s'" % (self.name, self.start, self.end, self.color)
	
	#Set this Era's members based on elt
	def fromXML (self, elt):
		name = elt.find("name")
		if (name):
			self.name = name.text
		else:
			self.name = ""
			
		#extract maskedDuration from name, if present		
		match = re.search('(?P<name>.*?)\n\((?P<maskedDuration\d+)\)>', self.name)		
		if (match):
			self.name = match.group('name')
			self.maskedDuration = match.group('maskedDuration')
		
		for field in ['start', 'end']:
			fieldElt = elt.find(field)
			if (fieldElt):
				setattr(self, field, fieldElt.text)
			else:
				setattr(self, field, -1)

		color = elt.find("color")
		if (color):
			self.color = color.text
		else:			
			self.color = self.__defaultColor		

	#return an Element representing this Era
	def toXML (self):
		elt = ET.Element('era')
		
		#embed maskedDuration in 'name' if present
		nameElt = ET.SubElement(elt, 'name')
		nameElt.text = self.name
		if (0 < self.maskedDuration):
			nameElt.text += "\n(%i)" % (self.maskedDuration)
			
		for field in ['start', 'end', 'color']:
			ET.SubElement(elt, field).text = str(getattr(self, field, ""))
		return elt
	

#############################
# Category
#############################

#representation of a Category
#Members: name, color
class Category:

	#give either xml (an Element) or {name, color}
	def __init__ (self, xml=None, name="", color=""):
		if (xml):
			self.fromXML(xml)
		else:
			self.name = name
			self.color = color
			self.font_color = "0,0,0"
			
	def __str__ (self):
		return "Category '%s' color '%s'" % (self.name, self.color)
	
	#comparison depends on members: name
	def __cmp__ (self, other):
		if (self.name < other.name):
			return -1
		elif (self.name == other.name):
			return 0
		else:
			return 1 

	#Set this Category's members based on elt
	def fromXML (self, elt):
		name = elt.find("name")
		if (name):
			self.name = name.text

		color = elt.find("color")
		if (color):
			self.color = color.text
		else:
			self.color = "0,0,0"

		font_color = elt.find("font_color")
		if (font_color):
			self.font_color = font_color.text
		else:
			self.font_color = "255,255,255"

	#return an Element representing this Category
	def toXML (self):
		elt = ET.Element('category')
		ET.SubElement(elt, 'name').text = self.name
		for field in ['color', 'progress_color', 'done_color']:
			ET.SubElement(elt, field).text = self.color
		ET.SubElement(elt, 'font_color').text = self.font_color
		return elt

#############################
# Container
#############################

#representation of a Container
#Members: name, id
class Container:
	def __init__ (self, name="", id=""):
		self.name = name
		self.id = id
		
	def __str__ (self):
		return "Container '%s' id '%d'" % (self.name, self.id)
	
	#comparison depends on members: name
	def __cmp__ (self, other):
		if (self.name < other.name):
			return -1
		elif (self.name == other.name):
			return 0
		else:
			return 1 
				
#############################
# Timeline
#############################

#representation of a Timeline 
#Members: 
#	version: string
#	timetype: string
#	categories: list of Category objects
#	events: list of Event objects
#	containers: list of Container objects
# eras: list of Era objects
# _remainingColors: list of all colors not yet assigned to timeline objects
class Timeline:
	MAX_NUM_COLORS = 256
	
	#evenly-spaced era colors

	#initialize Timeline
	#If xmlFile is specified, Timeline is initialized based on its contents
	#Else Timeline is empty
	def __init__ (self, xmlFile=None):
		if (xmlFile):
			tree = ET.ElementTree(file=fileName)
			self.fromXML(tree)
		else:
			self.version = "1.9.0 (d89b8d1eceb5 2016-01-31)"
			self.timetype = "numtime"
			self.categories = []
			self.events = []
			self.containers = []
			self.eras = []
		
		self._remainingColors = _genColors(self.MAX_NUM_COLORS)
		self._eraColor = self._nextColor()

	#input: (eltTree)
	#output: ()
	#Populate the members of self based on eltTree
	def _fromXML (self, eltTree):
		assert(0 == 1) #TODO untested
		version = eltTree.find('version')
		if (version):
			self.version = version.text
		timetype = eltTree.find('timetype')
		if (timetype):
			self.timetype = timetype.text

		self.categories = []
		categories = eltTree.find('categories')
		for categoryXML in list(categories):
			category = Category(xml=categoryXML)
			self.categories.append(category)

		self.events = []
		events = eltTree.find('events')
		for eventXML in list(events):
			event = Event(xml=eventXML)
			#TODO Extract 'container' events and add them to self.containers
			self.events.append(event)

	#input: ()
	#output: (eltTree)
	#Describe the members of self as an eltTree
	def _toXML (self):
		root = ET.Element('timeline')
		ET.SubElement(root, 'version').text = self.version
		ET.SubElement(root, 'timetype').text = self.timetype

		eras = ET.SubElement(root, 'eras')
		for era in self.eras:
			eras.append(era.toXML())

		categories = ET.SubElement(root, 'categories')
		for category in self.categories:
			categories.append(category.toXML())
		
		#containers: create "container" events that span all member events
		containerEvents = [] 
		for container in self.containers:			
			correspondingEvents = [e for e in self.events if e.containerName == container.name]
			assert(0 < len(correspondingEvents))
			min_start = min([e.start for e in correspondingEvents])
			max_end   = max([e.end   for e in correspondingEvents])
						
			#container events don't belong to a category, that way they end up gray
			containerEvent = Event(start=min_start, end=max_end, text="[%d]%s" % (container.id, container.name))
			containerEvents.append(containerEvent)
			#events that belong to a container must know the ID prior to Event.toXML()			
			for e in correspondingEvents:
				e.containerID = container.id

		events = ET.SubElement(root, 'events')
		for event in containerEvents:
			events.append(event.toXML())
		for event in self.events:
			events.append(event.toXML())
			
		#Now that XML has been generated, wipe containerID from the corresponding events
		for container in self.containers:
			correspondingEvents = [e for e in self.events if e.containerName == container.name]
			for e in correspondingEvents:
				del e.containerID

		#default view: show all events
		min_start = min([e.start for e in self.events])
		max_end = max([e.start for e in self.events])
		view = ET.SubElement(root, 'view')
		displayedPeriod = ET.SubElement(view, 'displayed_period')
		ET.SubElement(displayedPeriod, 'start').text = str(min_start*0.95)
		ET.SubElement(displayedPeriod, 'end').text = str(max_end*1.05)
		
		#idk what this is, but TTP generates it
		ET.SubElement(view, 'hidden_categories')

		return ET.ElementTree(root)
	
	#input: (gapThreshold)
	#output: ()
	#Locate gaps of length at least GAPTHRESHOLD between successive events
	#Collapse the gaps to width GAPTHRESHOLD by left-shifting all subsequent events an appropriate amount
	#These gaps are added to the Timeline's list of Eras, and the amount of time removed is indicated in the name of each era
	#NOTE: This function could modify the start and end time of all events in the timeline
	#Any existing Eras will be damaged, so no eras can be defined when it is invoked 
	def collapseGaps (self, gapThreshold):
		#This function will render any existing eras meaningless
		assert(not self.eras)
		
		logging.debug("Timeline::collapseGaps: Events before: " + ','.join(map(lambda event: "(%d,%d)" % (event.start, event.end), self.events)))		
		origEventDurations = [(e.end - e.start) for e in self.events]
		distances = []
		newEras = []
		eventsBeforeEras = []
			
		for event in self.events:
			#If E has an overlapping event that ends after it, then E cannot be an era lower boundary
			max_end = max([e.end for e in self._overlappingEvents(event)])
			if (event.end < max_end):
			  continue
			#If no events follow E, then E cannot be an era lower boundary
			nextEvent = self._nextEvent(event)
			if (not nextEvent):
				continue
			#If the distance from E to its nextEvent is less than gapThreshold, then E cannot be an era lower boundary 
			distance = (nextEvent.start - event.end)
			distances.append(distance)
			if (distance <= gapThreshold):
				continue
			
			#If E's end is the same as any event preceding an era that we've found, then E cannot be an era lower boundary
			# (well, it could be, but we can only have one event preceding each era)
			if ([prev for prev in eventsBeforeEras if prev.end == event.end]):
				continue
			
			logging.debug("Timeline::collapseGaps: Found an event preceding an era (distance %d): %s" % (distance, event))
			eventsBeforeEras.append(event)
			logging.debug("Timeline::collapseGaps: Current eventsBeforeEras:\n  %s" %(',\n  '.join(map(str, eventsBeforeEras))))
		
		logging.debug("Timeline::collapseGaps: Final eventsBeforeEras:\n  %s" %(',\n  '.join(map(str, eventsBeforeEras))))
		
		#Handle eras starting with the earliest so that subsequent left-shifts don't harm earlier eras 
		eventsBeforeEras.sort(key=lambda event: event.start)
		for event in eventsBeforeEras:			
			nextEvent = self._nextEvent(event)
			assert(nextEvent)
			distance = (nextEvent.start - event.end)
			assert(gapThreshold < distance)
			max_end = max([x.end for x in self._overlappingEvents(event)])
			if (event.end != max_end):
			  assert(event.end == max_end)			
														
			era = Era(name='Gap %d' % (len(self.eras) + len(newEras)), start=event.end, end=(event.end + gapThreshold), maskedDuration=distance)
			logging.debug("Timeline::collapseGaps: Found an era (distance %d). era (%d,%d), based on event (%d,%d) with successor (%d,%d)" % (distance, era.start, era.end, event.start, event.end, nextEvent.start, nextEvent.end))
			
			#Left-shift all subsequent events
			logging.debug("Timeline::collapseGaps: Events before shift:\n  " + '\n  '.join(map(lambda event: "(%d,%d)" % (event.start, event.end), self.events)))					
			shiftAmount = (distance - gapThreshold)
			logging.debug("Timeline::collapseGaps: distance %d gapThreshold %d (event %s) (nextEvent %s)" %(distance, gapThreshold, event, nextEvent))
			assert(0 < shiftAmount)					
			for affectedEvent in self._subsequentEvents(event):				
				assert(era.end + shiftAmount <= affectedEvent.start)
				affectedEvent.start -= shiftAmount
				affectedEvent.end -= shiftAmount
			
			newEras.append(era)
			logging.debug("Timeline::collapseGaps: Events after shift:\n  " + '\n  '.join(map(lambda event: "(%d,%d)" % (event.start, event.end), self.events)))
														
		for era in newEras:
			#Sanity check: no events should be active during the new eras
			for event in self.events:
				logging.debug("Timeline::computeEvents: era (%f, %f) event (%f, %f)" % (era.start, era.end, event.start, event.end))
				assert(event.end <= era.start or era.end <= event.start)
			self.addEra(era)
		logging.info("Timeline::collapseGaps: Max observed distance was %d, gapThreshold was %d. Added %d new eras." % (max(distances), gapThreshold, len(newEras)))		
		logging.debug("Timeline::collapseGaps: Events after:\n  " + '\n  '.join(map(lambda event: "(%d,%d)" % (event.start, event.end), self.events)))
		
		#Verify that the duration of each event remained fixed
		finalEventDurations = [(e.end - e.start) for e in self.events]
		for i in range(len(origEventDurations)):
			assert(origEventDurations[i] == finalEventDurations[i])		
		
	#input: (event)
	#output: (overlappingEvents)
	#Returns list of all events that overlap with EVENT
	def _overlappingEvents (self, event):
		return [e for e in self.events if event.overlaps(e)]	
		
	#input: (event)
	#output: (subsequentEvents)
	#returns list of all events that start after EVENT ends
	def _subsequentEvents (self, event):
		return [e for e in self.events if event.endsBeforeStarts(e)]
		
	#input: (event)
	#output: (nextEvent)
	#returns the first event in this timeline that starts after EVENT ends
	#returns None if there are no such events 
	def _nextEvent (self, event):
		subseqEvents = self._subsequentEvents(event)
		if (subseqEvents):
			#return one of the events with the min start time. TODO This is ugly!
			min_start = min([e.start for e in subseqEvents])
			next = [e for e in self.events if e.start == min_start][0]
			assert(event.start < next.start)
			return next
		else:
			return None
	
	#input: ()
	#output: ()
	#Normalizes the start and end times of this timeline's events
	#Normalization steps:
	# 1. The earliest event begins at time 0
	# 2. The position and duration of each event is calculated as a proportion of the duration of the shortest event, 
	#		 whose duration is set to 1
	#WARNING: This will destroy any intended correspondence between events and existing eras.	
	def normalizeEventTimes (self):
		min_start = min([e.start for e in self.events])
		orig_min_duration = min([(e.end - e.start) for e in self.events])
		orig_max_duration = max([(e.end - e.start) for e in self.events])
		orig_max_end = max([e.end for e in self.events])
		#Normalize
		for e in self.events:			
			#shift left and decrease proportionally
			e.start = int((e.start - min_start) / orig_min_duration)
			e.end   = int((e.end   - min_start) / orig_min_duration)
		normalized_min_duration = min([(e.end - e.start) for e in self.events])
		assert(normalized_min_duration == 1)
		normalized_max_duration = max([(e.end - e.start) for e in self.events])
		normalized_max_end = max([e.end for e in self.events])
		logging.info("Timeline::normalizeEventTimes: orig min duration %d orig_max_duration %d orig_max_end %d, normalized_min_duration %d normalized_max_duration %d normalized_max_end %d" % 
								(orig_min_duration, orig_max_duration, orig_max_end, normalized_min_duration, normalized_max_duration, normalized_max_end))

	#input: (fileName)
	#output: ()
	#Write ElementTree eltTree out to fileName as pretty XML
	def write (self, fileName):
		xml_string = ET.tostring(self._toXML().getroot())
		xml = DOM.parseString(xml_string)
		pretty_xml_as_string = xml.toprettyxml()
	
		with open(fileName, 'w') as f:
			f.write(pretty_xml_as_string)
			logging.info("Timeline::write: Wrote timeline (%d events, %d eras, %d categories) to %s" % (len(self.events), len(self.eras), len(self.categories), fileName))
		return
	
	#input: (era)
	#output: ()
	#Add ERA to this Timeline
	#The color for ERA is set automatically
	def addEra (self, era):
		era.color = self._getEraColor()
		self.eras.append(era)
	
	#input: ()
	#output: (eraColor) "R,G,B"
	#Returns the color for an era
	def _getEraColor (self):
		return self._eraColor

	#input: (event)
	#output: ()
	#add EVENT to this Timeline
	#also updates {categories, containers} based on fields of event
	def addEvent (self, event):
		self.events.append(event)
		#event.text += '%d' % (len(self.events)) #DEBUGGING -- unique IDs for events		
				
		#new category?
		eventCategory = Category(name=event.categoryName)
		if (eventCategory not in self.categories):			
			self.categories.append(Category(name=event.categoryName, color=self._nextColor()))
			logging.debug("Timeline::addEvent: Added category '%s'" % (event.categoryName))
		else:
			logging.debug("Timeline::addEvent: Category '%s' is already present" % (event.categoryName))
				
		#new container?
		if (event.containerName):
			eventContainer = Container(name=event.containerName)
			if (eventContainer not in self.containers):
				self.containers.append(Container(name=event.containerName, id=len(self.containers) + 1))
				logging.debug("Timeline::addEvent: Added container '%s'" % (event.containerName))
			else:
				logging.debug("Timeline::addEvent: Container '%s' is already present" % (event.containerName))
		logging.debug("Timeline::addEvent: Added event '%s' to timeline" % (event))
		
	#input: ()
	#output: (rgbColorStr)
	#Return the next color in the form "R,G,B"
	def _nextColor (self):
		assert(len(self._remainingColors))
		color = self._remainingColors.pop()
		rgbColorStr = ','.join(map(str, color))
		return rgbColorStr
