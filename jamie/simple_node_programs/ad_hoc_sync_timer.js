/*
  Simple application demonstrating ad-hoc synchronization using timers.
*/

/* Modules. */

var fs = require("fs");

/* Globals. */

var fname = "/tmp/ad_hoc_sync_test";
var created = 0;

/* Helper functions. */

var mylog = function (str) {
  console.log('APP: ' + str);
};

/* Program code. */
try {
  fs.unlinkSync(fname);
} catch (err) {
}

/* While !created, call self with a small timeout. */
var safeStat = function () {
  if (created)
  {
    mylog("Stat'ing " + fname);
    fs.stat(fname, function () {
      mylog("Stat'd " + fname);
      fs.unlinkSync(fname);
    });
  }
  else
  {
    mylog(fname + " not created yet, spinning");
    setTimeout(safeStat, 10);
  }
};

/* Wait until created, then stat. */
safeStat(); 
/* After 1 second, write the file and then set created=1. */
setTimeout(function() { 
  mylog("Creating " + fname);
  fs.writeFile(fname, 'AAAAA', function() { 
    mylog("Finished creating " + fname); 
    created = 1; 
  }); 
}, 1000);

//Start reading from stdin so we don't exit.
process.stdin.resume();
