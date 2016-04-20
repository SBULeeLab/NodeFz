import requests
import logging

# HTTP GET on URL
def httpGet (url):
	logging.info("httpGet: url %s" % (url))
	try:
		r = requests.get(url, timeout=1)
		return r
	except requests.exceptions.Timeout:
		logging.debug("httpGet: Timed out")
		return None

def httpPostJSON (url, jsonData):
	logging.info("httpPostJSON: url %s, data %s" % (url, jsonData))
	headers = {'content-type': 'application/json'}
	r = requests.post(url, data=jsonData, headers=headers)
	return r.content
