/*
  Simple non-deterministic application due to timers.

  There's a data race (read/write to the same variable).
*/

/* Globals. */
var x = 0;
var runForever = (process.argv[2] && process.argv[2] == '--forever');

/* Helper functions. */

var mylog = function (str) {
  console.log('APP: ' + str);
};

/* Program code. */
setTimeout(function() {
  mylog('Timer 1: x was ' + x);
  x = 1;
}, 10);
setTimeout(function() {
  mylog('Timer 2: x was ' + x);
  x = 2;
}, 10);

if (runForever)
{
  //Start reading from stdin so we don't exit.
  process.stdin.resume();
}
