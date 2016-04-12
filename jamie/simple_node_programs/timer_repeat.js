/*
   This program launches a timer sequence using setInterval.
   One timer every 50 ms; stops after 10 timers.
   Its behavior should be deterministic.
   I'd like to see what kinds of CBs are invoked in a timer-only world.
*/

var timer_invocations = 0;

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

intervalObj = setInterval(timerFunc, 50);

//Start reading from stdin so we don't exit.
process.stdin.resume();
