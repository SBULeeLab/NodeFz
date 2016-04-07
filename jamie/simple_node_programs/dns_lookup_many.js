/*
  Exercise dns.lookup, which ultimately calls uv_getaddrinfo.
  Three paths in the CB make the source of the signal handler for SIGWINCH obviuos.
*/

var dns = require('dns');
var fs = require('fs');

/* 1: success
   2: success
   3: success
   4: unexpected output, sometimes. final line is not always "I'm outta here" on replay
   8: infinite loops and messiness */
var nQueries = 32;

var nRun = 0;

for (var i = 0; i < nQueries; i++) 
{
  dns.lookup('www.google.com', { family: 6 }, function onLookup(err, addresses, family) {
    nRun += 1;
    var out = "APP: lookup " + nRun + ": www.google.com -> " + addresses;
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
}

//Start reading from stdin so we don't exit.
process.stdin.resume();
