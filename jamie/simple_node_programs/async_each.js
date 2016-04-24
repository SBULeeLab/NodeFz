/*
  Demonstrates the use of async each.
  Each element in a collection is invoked with an iterator function.
  When the iterator function on each element has completed, a done CB is invoked.

  This particular version delays the first iterator function to be invoked such that it invokes
    doneCB after the rest. That means that the DoneCallback is invoked by the first iterator function.
*/

var fs = require("fs");
var async = require("async");

var hereYet = 0;

mylog = function (str) {
  console.log('APP: ' + str);
}

function DoneCallback (err, results) {
  mylog("All callbacks have finished");
  fs.readdir('/tmp', function(err, files) {
    mylog("All callbacks have finished, and I ran fs.readdir, too");
  });
};

function Iter (item, doneCB) {
  var f = item;
  mylog("iter: item " + item);
  fs.readFile(f, function(err, data) { 
    mylog("iter: item " + item + ": Found data " + data + "; err " + err); 

    if (!hereYet){
      hereYet = 1;
      /* Delay so that DoneCallback is invoked by us. */
      setTimeout(function(){ doneCB(null); }, 500);
    }
    else {
      doneCB(null);
    }

  }); 
};

/* Create files synchronously. */
var f = '/tmp/xyz';
var files = [f + "1", f + "2", f + "3", f + "4", f + "5"];
mylog('Writing data into files ' + files);
files.forEach(function(item, ix, arr){ 
  fs.writeFileSync(item, 'aaaaa'); 
 });

/* Read each file in parallel; invoke DoneCallback when all are finished. */
mylog('Kicking off async.each on files ' + files);
async.each(files, Iter, DoneCallback);
