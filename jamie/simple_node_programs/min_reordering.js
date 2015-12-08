/* Author: Jamie Davis <davisjam@vt.edu>
   Description: 
     - This test will pass until the reordering distance is at least X. 
     - It launches one "reorder_test" function that errors out if at least X "incrementer functions" 
       have gone before it.
     - It launches X "incrementer" functions 2 seconds after launching the "reorder_test" function. 
     
     All functions are registered as callbacks to fs.stat'ing the same file, so under "normal" execution
       the "reorder_test" function would never be expected to fail.
     This is a read-only test. No rollback required between interleavings. 
       Each exploration of this program could be run in parallel in libux, though this is not true for generic Node.js apps.
*/

if (process.argv.length != 4)
{
  console.log ("Description: This program will exit successfully if the reordering distance is < min_reorder_distance_to_fail.\n  Otherwise it will fail.\nUsage: node " + process.argv[1] + " min_reorder_distance_to_fail n_functions\nExamples:\n  node " + process.argv[1] + " 4 2\n    => Run with a min distance of 4 and only 2 incrementer functions, so will not fail under any reordering\n  node " + process.argv[1] + " 4 3\n    => Run with a min distance of 4 and 3 functions, so will fail if reordering distance is 4 or higher");
  process.exit (0);
}

fs = require('fs');

var global_counter = 0;

var min_distance_required = process.argv[2];
var n_functions = process.argv[3];

/* reorder_test prints one thing if enough reordering has occurred (based on global_counter), else prints another thing. */
var reorder_test = function () {
  if (global_counter + 1 < min_distance_required) 
  {
    console.log ("APP: reorder_test: global_counter " + global_counter + " + 1 < min_distance_required " + min_distance_required + ". min_distance_required not met."); 
  }
  else
  {
    //console.log ("APP: reorder_test: min_distance_required " + min_distance_required + " <= global_counter " + global_counter + ". min_distance_required met.");
    console.log ("APP: reorder_test: min_distance_required " + min_distance_required + " <= global_counter " + global_counter + ". min_distance_required met. Exiting 1.");
    process.exit (1);
  }
};

/* Returns an incrementer named i. Makes it easier to eyeball execution order -- hex addresses are tough. */
var incrementer_factory = function (i) {
  return function () {
    var before = global_counter;
    global_counter++;
    console.log ("APP: incrementer_" + i + ": global_counter BEFORE " + before + " AFTER " + global_counter);
  }
};

/* Kick off the reorder_test. */
fs.stat (process.argv[1], reorder_test);

/* After some time, kick off n_functions incrementers */
var rough_time_distance = 2;
setTimeout (function () {
  console.log ("APP: Kicking off " + n_functions + " incrementers");
  for (var i = 0; i < n_functions; i++)
  {
    var incrementer = incrementer_factory (i);
    var counter = 0;
    /* Spin to encourage incrementers to be handled in increasing numerical order. */
    for (var j = 0; j < 20000; j++) 
      counter++;
    console.log ("counter " + counter);
    fs.stat (process.argv[1], incrementer);
  }
}, 1000*rough_time_distance);
