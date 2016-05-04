/*
   This program creates a tree of timers.
   Each timer creates two children, up to 4 layers deep.
   This makes a total of 31 timers.

   Its behavior is deterministic.
   I'd like to see what kinds of CBs are invoked in a timer-only world.
*/

var crypto = require('crypto');

/* Globals. */
var timer_invocations = 0;
var n_timers = 0;
var max_depth = 4;

var runForever = (process.argv[2] && process.argv[2] == '--forever');

/* Helper functions. */
var mylog = function (str) {
  console.log('APP: ' + str);
};

var randrange = function (min, max) {
  var bs = crypto.randomBytes(256);
  var range = max - min;

  /* Always an even number? */
  var randNum = parseInt(crypto.randomBytes(16).toString('hex'), 16);
              
  var ret = min + (randNum % range)
  return ret;
}

/* Program code. */
function childBearingAlarm(parentID, myID, depth) {
  function handle() {
    timer_invocations++;
    mylog("Timer " + myID + " (child of " + parentID + "): " + timer_invocations + " timers have gone off");
    if (depth < max_depth)
    {
      for (var i = 0; i < 2; i++)
      {
        n_timers++;
        childBearingAlarm(myID, "" + myID + "_" + i, depth+1);
      }
    }
  };

  //setTimeout(handle, 10);
  setTimeout(handle, randrange(10, 100));
}

childBearingAlarm("-1", "0", 0);

if (runForever)
{
  //Start reading from stdin so we don't exit.
  process.stdin.resume();
}

