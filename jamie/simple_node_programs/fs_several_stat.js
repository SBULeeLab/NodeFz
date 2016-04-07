/*
  Make several concurrent fs stat requests.  
*/

var fs = require("fs");

fd = fs.openSync("/tmp/fs_several_stat_TMP_LOG_FILE.txt", 'w');

fs.writeFileSync("/tmp/foo", "aaaaa");

fs.stat("/tmp/foo", function(){
  out("APP: 1: Stat'd file");
});

fs.stat("/tmp/foo", function(){
  out("APP: 2: Stat'd file");
});

fs.stat("/tmp/foo", function(){
  out("APP: 3: Stat'd file");
});

fs.stat("/tmp/foo", function(){
  out("APP: 4: Stat'd file");
});

fs.stat("/tmp/foo", function(){
  out("APP: 5: Stat'd file");
});

fs.stat("/tmp/foo", function(){
  out("APP: 6: Stat'd file");
});

//Start reading from stdin so we don't exit.
process.stdin.resume();

/* Seeing if UV_WRITE_CB comes from both process.stdout.write and console.log.
   Answer: yes. */
var out = function(str)
{
  outputMode = "console.log";
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
  else
  {
    throw error;
  }
};
