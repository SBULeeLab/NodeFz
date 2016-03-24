/*
  Let's watch the inheritance graph when an async callback
  launches an async callback.
*/

var fs = require("fs");

fs.writeFileSync("/tmp/foo", "AAAAAA");
console.log('APP: Started file with AAAAAA');

/* Race! */
fs.stat("/tmp/foo", function (err, stats) {
  if (err) {
    console.log("APP: stat failed -- no such file");
  }
  else {
    console.log("APP: isFile ? " + stats.isFile());
  }
});

fs.unlink("/tmp/foo", function (err) {
  console.log("APP: unlink finished");
});

//Start reading from stdin so we don't exit.
process.stdin.resume();
