import json
from functools import partial
import logging
import random
from HTTPRequests import httpGet, httpPostJSON


class WordFinderClient:
	PATTERNS = ['he__o', 'aa', 'st???', '/boo/']

	def __init__ (self, wfURL, id):
		self.url = wfURL
		self.clientID = id
		logging.debug("WordFinderClient::__init__: wfURL %s id %i" % (wfURL, id))

		self.visitedSite = False

	# Client operations

	# Do this first
	def VisitSite (self):
		assert(not self.visitedSite)
		for page in ['']:
			fullURL = "%s/%s" % (self.url, page)
			logging.info("WordFinderClient::VisitSite: client %i: %s" % (self.clientID, fullURL))
			httpGet(fullURL)
		self.visitedSite = True

	# Look up a word
	def Lookup (self):
		assert (self.visitedSite)
		logging.info("WordFinderClient::Lookup: client %i" % (self.clientID))
		fullURL = "%s/%s" % (self.url, 'search')
		jsonData = json.dumps({ 'pattern' : random.choice(WordFinderClient.PATTERNS) })
		httpPostJSON(fullURL, jsonData)

class WordFinderClientDriver:
	N_META_REQUESTS = 1

	#input: keywords: url, nClients, nRequests
	# mudURL -- where clients go to interact with the app
	#	nClients -- number of clients
	# nRequests -- requests per client
	def __init__ (self, url=None, nClients=-1, nRequests=-1):
		assert(url and 0 <= nClients and 0 <= nRequests)
		self.wfURL = url
		self.nClients = nClients
		self.nRequests = nRequests

		assert(WordFinderClientDriver.N_META_REQUESTS <= self.nRequests)

		self.wfClients = [WordFinderClient(self.wfURL, i) for i in range(0, self.nClients)]

	#output: requests --  dictionary, client ID -> list of function partials to invoke to run that client
	def GenRequests (self):
		perClientReqs = {}
		for i in range(0, self.nClients):
			clientReqs = []
			clientReqs.append(partial(self.wfClients[i].VisitSite))
			for j in range(self.nRequests - WordFinderClientDriver.N_META_REQUESTS):
				clientReqs.append(partial(self.wfClients[i].Lookup))
			perClientReqs[i] = clientReqs
		return perClientReqs

	@staticmethod
	def NumMetaRequests ():
		return WordFinderClientDriver.N_META_REQUESTS