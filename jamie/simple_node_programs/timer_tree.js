/*
   This program creates a tree of timers. Each timer creates two children
   calling its own function, until 20 times have been created.

   Its behavior should be deterministic.
   I'd like to see what kinds of CBs are invoked in a timer-only world.
*/

var timer_invocations = 0;
var n_timers = 0;
var max_timers = 20;

var runForever = (process.argv[2] && process.argv[2] == '--forever');

var timerFunc = function(){
  timer_invocations++;
  console.log("APP: Timer " + timer_invocations + " n_timers " + n_timers + " went off");
  for (var i = 0; i < 2; i++)
  {
    if (n_timers < max_timers)
    {
      n_timers++;
      setTimeout(timerFunc, 10);
    }
  }
};

n_timers++;
setTimeout(timerFunc, 50);

if (runForever)
{
  //Start reading from stdin so we don't exit.
  process.stdin.resume();
}
