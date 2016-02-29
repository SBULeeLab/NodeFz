/*
  Let's watch the inheritance graph when an async callback
  launches an async callback.
*/

var fs = require("fs");

fs.writeFile("/tmp/foo", "AAAAAA", function(){
  fs.writeFile("/tmp/foo", "AAAAAA", function(){
    fs.writeFile("/tmp/foo", "AAAAAA", function(){
      console.log("Bottomed out");
    });
  });
});

//Start reading from stdin so we don't exit.
process.stdin.resume();
