/*
  Let's watch the inheritance graph when an async callback
  launches an async callback.
*/

var fs = require("fs");

fs.writeFileSync("/tmp/foo", "AAAAAA");
console.log('APP: Started file with AAAAAA');

/* Race! */
fs.writeFile("/tmp/foo", "BBBBBB", function() { console.log('APP: Wrote BBBBBB'); });
fs.readFile("/tmp/foo", function(err, data) { console.log('APP: Read ' + data); });

//Start reading from stdin so we don't exit.
process.stdin.resume();
