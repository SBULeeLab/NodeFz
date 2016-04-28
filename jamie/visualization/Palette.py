#Author: Jamie Davis (davisjam@vt.edu)
#Description: RGB color palette
# Defines the following public classes:
#  Palette

import random
import logging
import colorsys

#############################
# Palette
#############################

# nColors distinct RGB colors
class Palette:
	def __init__ (self, nColors):
		logging.debug("Palette::__init__: nColors %d" % (nColors))
		assert(0 < nColors)

		self.colors = self._genColors(nColors)
		self.nColors = nColors

		self.colorIx = 0 # for nextColor

	# input: ()
	# output: (color) see getColor
	# Loops over the nColors of this Palette
	def nextColor (self):
		rgb = self.getColor(self.colorIx)
		self.colorIx += 1
		self.colorIx %= self.nColors
		return rgb

	# input: (colorNum) 0 <= colorNum < nColors
	# output: (color): tuple (R,G,B)
	def getColor (self, colorNum):
		assert(colorNum < self.nColors)
		color = self.colors[colorNum]
		logging.debug("Palette::__init__: colorNum %d (%s)" % (colorNum, color))
		return color

	# input: (numColors)
	# output: (colors)
	# Returns a list of NUMCOLORS tuples of form (R,G,B), with 0 <= R,G,B <= 256
	# Credit: Uri Cohen, http://stackoverflow.com/questions/470690/how-to-automatically-generate-n-distinct-colors
	def _genColors (self, numColors):
		randState = random.getstate() # save RNG state to restore afterwards

		random.seed(0) # Ensure colors are the same every time this module is used
		colors = []
		stepSize = 360/numColors
		for i in range(0, numColors):
			hue = i*stepSize/360.0
			lightness = (50 + random.random() * 10)/100.
			saturation = (90 + random.random() * 10)/100.
			rgb = colorsys.hls_to_rgb(hue, lightness, saturation)
			rgb = [int(256*x) for x in rgb] # [0,1] to [0, 256]
			colors.append(rgb)

		# Sort randomly so that we don't have like colors adjacent to each other
		colors.sort(key=lambda x: random.random())

		random.setstate(randState)
		return colors