/*
  Let's watch the inheritance graph when an async callback
  launches an async callback.
*/

var fs = require("fs");

var runForever = (process.argv[2] && process.argv[2] == '--forever');

fs.writeFile("/tmp/foo", "AAAAAA", function(){
  console.log("APP: Level 1");
  fs.readFile("/tmp/foo", function(){
    console.log("APP: Level 2");
    fs.writeFile("/tmp/foo", "AAAAAAA", function(){
      console.log("APP: Level 3");
      fs.readFile("/tmp/foo", function() { 
        console.log("APP: Bottomed out");
      });
    });
  });
});

if (runForever)
{
  //Start reading from stdin so we don't exit.
  process.stdin.resume();
}
