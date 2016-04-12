/*
  Exercise dns.lookup, which ultimately calls uv_getaddrinfo.
  Three paths in the CB make the source of the signal handler for SIGWINCH obviuos.
*/

var dns = require('dns');
var fs = require('fs');

//var host = 'woody.cs.vt.edu';
var host = 'google.com';

dns.lookup(host, { family: 6 }, function onLookup(err, address, family) {
  console.log('APP: dns.lookup(\'%s\'): address: %j; family: %d', host, address, family);
  var out = "APP: err " + err + ", " + host + " -> " + address + ", family " + family;
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
