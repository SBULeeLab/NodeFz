from functools import partial
import logging
import random
from HTTPRequests import httpGet

class MudClient:
	DIRECTIONS = ["up", "down", "left", "right"]

	def __init__ (self, mudURL, id):
		self.url = mudURL
		self.userID = id

		self.visitedSite = False
		self.registered = False
		self.disconnected = False

	# Client operations

	# Do this first
	def VisitSite (self):
		assert(not self.visitedSite)
		for page in ['', 'style.css', 'favicon.ico']:
			fullURL = "%s/%s" % (self.url, page)
			logging.info("MudClient::VisitSite: %s" % (fullURL))
			httpGet(fullURL)
		self.visitedSite = True

	# Do this second
	def Register (self):
		assert (self.visitedSite)
		fullURL = "%s/register?id=%d" % (self.url, self.userID)
		logging.info("MudClient::Register: %s" % (fullURL))
		httpGet(fullURL)
		self.registered = True

	# After registration and before disconnect
	def Move (self):
		assert (self.visitedSite and self.registered and not self.disconnected)
		direction = random.choice(self.DIRECTIONS)
		fullURL = "%s/move?id=%d&direction=%s" % (self.url, self.userID, direction)
		logging.info("MudClient::Move: %s" % (fullURL))
		httpGet(fullURL)

	# After registration and before disconnect
	def GetBoard (self):
		assert (self.visitedSite and self.registered and not self.disconnected)
		fullURL = "%s/recv" % (self.url)
		logging.info("MudClient::GetBoard: %s" % (fullURL))
		httpGet(fullURL)

	# Final step
	def Disconnect(self):
		assert (self.visitedSite and self.registered and not self.disconnected)
		fullURL = "%s/part?id=%d" % (self.url, self.userID)
		logging.info("MudClient::Disconnect: %s" % (fullURL))
		httpGet(fullURL)
		self.disconnected = True

class MudClientDriver:
	N_META_REQUESTS = 3

	#input: keywords: url, nClients, nRequests
	# mudURL -- where clients go to interact with the app
	#	nClients -- number of clients
	# nRequests -- requests per client
	def __init__ (self, url=None, nClients=-1, nRequests=-1):
		assert(url and 0 <= nClients and 0 <= nRequests)
		self.mudURL = url
		self.nClients = nClients
		self.nRequests = nRequests

		assert(MudClientDriver.N_META_REQUESTS <= self.nRequests)

		self.mudClients = [MudClient(self.mudURL, i) for i in range(0, self.nClients)]

	#output: requests --  dictionary, client ID -> list of function partials to invoke to run that client
	def GenRequests (self):
		perClientReqs = {}
		for i in range(0, self.nClients):
			clientReqs = []
			clientReqs.append(partial(self.mudClients[i].VisitSite))
			clientReqs.append(partial(self.mudClients[i].Register))
			for j in range(self.nRequests - MudClientDriver.N_META_REQUESTS):
				if (j % 2 == 0):
					clientReqs.append(partial(self.mudClients[i].Move))
				else:
					clientReqs.append(partial(self.mudClients[i].GetBoard))
			clientReqs.append(partial(self.mudClients[i].Disconnect))
			perClientReqs[i] = clientReqs
		return perClientReqs

	@staticmethod
	def NumMetaRequests ():
		return MudClientDriver.N_META_REQUESTS