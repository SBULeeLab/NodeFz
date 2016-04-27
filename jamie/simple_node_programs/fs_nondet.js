/*
  Simple non-deterministic application due to threadpool.

  There's both an FS race (read/write to the same file)
  and a data race (read/write to the same variable).
*/

/* Modules. */
var fs = require("fs");
var dns = require('dns');

/* Globals. */
var i = 0;
var host = 'www.google.com';
var runForever = (process.argv[2] && process.argv[2] == '--forever');

/* Helper functions. */

var mylog = function (str) {
  console.log('APP: ' + str);
};

/* Program code. */
fs.writeFileSync("/tmp/foo", "AAAAAA");
console.log('APP: Started file with AAAAAA');

/* FS race on the contents of /tmp/foo. */
fs.writeFile("/tmp/foo", "BBBBBB", function() { 
  mylog('Wrote BBBBBB');
});

fs.readFile("/tmp/foo", function(err, data) { 
  mylog('Read ' + data);
});

/* Variable race on i. */
/*
dns.lookup(host, { family: 6 }, function onLookup(err, address, family) {
  mylog('looked up ' + host + '; i: ' + i);
  i++;
});

dns.lookup(host, { family: 6 }, function onLookup(err, address, family) {
  mylog('looked up ' + host + '; i: ' + i);
  i++;
});
*/

if (runForever)
{
  //Start reading from stdin so we don't exit.
  process.stdin.resume();
}
