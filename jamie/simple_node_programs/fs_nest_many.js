/*
  Let's watch the inheritance graph when an async callback
  launches an async callback.
*/

var fs = require("fs");

var nQueries = 5;
var nRun = 0;

for (var i = 0; i < nQueries; i++)
{
  fs.writeFile("/tmp/foo", "AAAAAA", function(){
    nRun += 1;
    console.log("APP: Level 1 (nRun " + nRun + ")");
    fs.readFile("/tmp/foo", function(){
      nRun += 1;
      console.log("APP: Level 2 (nRun " + nRun + ")");
      fs.writeFile("/tmp/foo", "AAAAAAA", function(){
        nRun += 1;
        console.log("APP: Level 3 (nRun " + nRun + ")");
        fs.readFile("/tmp/foo", function() { 
          nRun += 1;
          console.log("APP: Bottomed out (nRun " + nRun + ")");
        });
      });
    });
  });
}

//Start reading from stdin so we don't exit.
process.stdin.resume();
