/*
  Let's watch the inheritance graph when an async callback
  launches an async callback.
*/

var fs = require("fs");

var nQueries = 5;
var nRun = 0;
var runForever = (process.argv[2] && process.argv[2] == '--forever');

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

if (runForever)
{
  //Start reading from stdin so we don't exit.
  process.stdin.resume();
}
