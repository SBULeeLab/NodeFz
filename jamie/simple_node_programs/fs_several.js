/*
  Make several concurrent fs requests.  
*/

var fs = require("fs");

fd = fs.openSync("/tmp/fs_stat_TMP_LOG_FILE.txt", 'w');

fs.writeFileSync("/tmp/foo", "aaaaa");

fs.writeFile("/tmp/foo", "AAAAAA", function(){
  out("APP: 1: Wrote file");
});

fs.readFile("/tmp/foo", function(){
  out("APP: 2: Read file");
});

fs.writeFile("/tmp/foo", "AAAAAA", function(){
  out("APP: 3: Wrote file");
});

fs.readFile("/tmp/foo", function(){
  out("APP: 4: Read file");
});

fs.writeFile("/tmp/foo", "AAAAAA", function(){
  out("APP: 5: Wrote file");
});

fs.readFile("/tmp/foo", function(){
  out("APP: 6: Read file");
});

//Start reading from stdin so we don't exit.
process.stdin.resume();

/* Seeing if UV_WRITE_CB comes from both process.stdout.write and console.log.
   Answer: yes. */
var out = function(str)
{
  outputMode = "fs.writeSync";
  if (outputMode == "console.log")
  {
    console.log(str);
  }
  else if (outputMode == "process.stdout.write")
  {
    process.stdout.write(str + "\n");
  }
  else if (outputMode == "fs.writeSync")
  {
    str += "\n";
    fs.writeSync(fd, str);
  }
};
