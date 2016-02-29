/*
  Exercise dns.lookup, which ultimately calls uv_getaddrinfo
*/

var dns = require('dns');

dns.lookup('www.google.com', { family: 6 }, function onLookup(err, addresses, family) {
  console.log("www.google.com -> ", addresses);
});

//Start reading from stdin so we don't exit.
process.stdin.resume();
