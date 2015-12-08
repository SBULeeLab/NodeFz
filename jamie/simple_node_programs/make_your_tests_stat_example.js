/* Jamie Davis <davisjam@vt.edu>

Modified based on the example in http://howtonode.org/make-your-tests-deterministic
Running this can demonstrate that my modified libuv provides a superset of the 
functionality of the node-mocks module presented by the author. */

if (process.argv.length != 3)
{
  console.log ("Description: Test to demonstrate that the naive asynchronous collection of mtime in an array\n  can result in reordering\nUsage: node " + process.argv[1] + " n_files_to_stat");
  process.exit (0);
}

var fs = require('fs');

var n_files = process.argv[2];

/* 
//Create some test files.
var files = [];
files.push("/home");
files.push("/var");
files.push("/srv");
//a -> b -> c
files.forEach(function(file) {
  try {
    fs.unlinkSync(file);
  } catch (err) {}

  var fd = fs.openSync(file, 'w');
  fs.closeSync(fd);
});
*/

var dir = "/";
var files = fs.readdirSync (dir);
files = files.map (function (curr, ix, arr) {
  return dir + curr;
});

/* Reduce to the requested number of files. */
files = files.slice(0, n_files);
console.log ("Files " + files);

/* Timestamps of each file, in order. */
var expected_timestamps = [];
files.forEach (function(file) {
  expected_timestamps.push (fs.statSync(file).mtime);
});

/* Append the mtime of each file in FILES to timestamps. */
var getLastModified = function(files, done) {
  var stat_files = [];
  var timestamps = [];
  console.log ("APP: HELLO with " + files.length + " files");

  files.forEach(function(file) {
    /* Bug: the asynchronous nature of fs.stat means that files can be handled and
       pushed into {stat_files, timestamps} in an arbitrary order. */
    fs.stat(file, function(err, stat) {
      console.log ("APP: Finished processing " + file);
      stat_files.push(file);
      timestamps.push(stat.mtime);

      if (stat_files.length === files.length) {
        done(stat_files, timestamps);
      }

    });
  });
};

/* Compare STAT_FILES and TIMESTAMPS obtained by getLastModified to 
FILES and EXPECTED_TIMESTAMPS calculated using statSync. */
var compare = function (stat_files, timestamps) {

  console.log ("APP: Comparing the " + stat_files.length + " files we stat'd synchronously and asynchronously");
  for (var i = 0; i < timestamps.length; i++)
  {
    if (stat_files[i] != files[i])
    {
      console.log ("Error, stat_files[" + i +"] != files[" + i + "]");
      for (var j = 0; j < timestamps.length; j++)
      {
        console.log (j + " " + "{" + stat_files[j] + ", " + timestamps[j]          + "}" 
                       + " " + "{" + files[j]      + ", " + expected_timestamps[j] + "}");
      }
      process.exit (1);
    }
  }
  console.log ("Synchronous and asynchronous orders matched");
  process.exit (0);
}

getLastModified (files, compare);
