/*
   This program launches a series of timers. 
   Its behavior should be deterministic.
   I'd like to see what kinds of CBs are invoked in a timer-only world.
*/

var timer_invocations = 0;
var n_timers = 0;
var max_timers = 20;

var intervalObj;

var timerFunc = function(){
  timer_invocations++;
  console.log("APP: Timer number " + timer_invocations);
  if (10 <= timer_invocations)
  {
    console.log("APP: Last iter");
    clearInterval(intervalObj);
  }
};

n_timers++;
intervalObj = setInterval(timerFunc, 50);

//Start reading from stdin so we don't exit.
process.stdin.resume();
