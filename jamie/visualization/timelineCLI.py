#!/usr/bin/env python2

#Author: Jamie Davis (davisjam@vt.edu)
#Description: Provides a CLI to interact with timeline XML files 
#  for use with the TheTimelineProj timeline software.

import libtimeline as LT
import argparse

parser = argparse.ArgumentParser(description="Turn a file of events into a .timeline file compatible with TheTimelineProj. Could be extended with addEvent and addEra modes for an interactive mode; would need to get the fromXML methods of the libtimeline classes working")
parser.add_argument("eventFile", help="file describing events")
parser.add_argument("outputFile", help="file describing events")
parser.add_argument("-n", "--normalize", help="normalize events to the duration of the shortest event, and begin at time 0", action="store_true")
parser.add_argument("-c", "--collapseGaps", help="collapse event gaps exceeding gapThreshold", type=int, metavar='gapThreshold')
args = parser.parse_args()

with open(args.eventFile) as f:
	eventLines = f.readlines()
#eventLines = eventLines[0:24] #TODO TESTING	

timeline = LT.Timeline()

for eventLine in eventLines:
	timeline.addEvent(LT.Event(eventString=eventLine))
	
if (args.normalize):
	print "Normalizing events"
	timeline.normalizeEventTimes()

if (args.collapseGaps):	
	print "Collapsing gaps (gapThreshold %i)" % (args.collapseGaps)
	timeline.collapseGaps(args.collapseGaps)

timeline.write(fileName=args.outputFile)