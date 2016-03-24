/*
  Let's watch the inheritance graph when an async callback
  launches an async callback.
*/

var fs = require("fs");

fs.readFile("/tmp/foo", function(err, data){
  console.log("APP: Bottomed out (read <" + data + ">");
});

//Start reading from stdin so we don't exit.
process.stdin.resume();
