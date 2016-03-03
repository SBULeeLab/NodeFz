/*
  Exercise dns.lookup, which ultimately calls uv_getaddrinfo.
  Three paths in the CB make the source of the signal handler for SIGWINCH obviuos.
*/

var dns = require('dns');
var fs = require('fs');

dns.lookup('www.google.com', { family: 6 }, function onLookup(err, addresses, family) {
  var out = "www.google.com -> " + addresses;
  if (1)
  {
    /* This registers a signal handler for SIGWINCH. */
    console.log(out);
  }
  else if (0)
  {
    /* This does not. */
    fs.writeFile('/tmp/foo', out);
  }
  else
  {
  }
});

//Start reading from stdin so we don't exit.
process.stdin.resume();
