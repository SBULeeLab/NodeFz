/*
   This program creates a tree of timers. Each timer creates two children
   calling its own function, until 20 times have been created.

   Its behavior is dependent on the order in which timers go off.
   I'd like to see what kinds of CBs are invoked in a timer-only world.
*/

/* Globals. */
var timer_invocations = 0;
var n_timers = 0;
var max_timers = 20;

var runForever = (process.argv[2] && process.argv[2] == '--forever');

/* Helper functions. */
var mylog = function (str) {
  console.log('APP: ' + str);
};

/* Program code. */
function childBearingAlarm(parentID, myID) {
  function handle() {
    timer_invocations++;
    mylog("Timer " + myID + " (child of " + parentID + "): " + timer_invocations + " timers have gone off");
    for (var i = 0; i < 2; i++)
    {
      if (n_timers < max_timers)
      {
        n_timers++;
        childBearingAlarm(myID, n_timers);
      }
    }
  };

  setTimeout(handle, 10);
}

childBearingAlarm(-1, n_timers);

if (runForever)
{
  //Start reading from stdin so we don't exit.
  process.stdin.resume();
}

