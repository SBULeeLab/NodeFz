/*
  Demonstrates the use of async parallel.
  Each function is invoked with a callback, and when it finishes 
    it must invoke the provided callback.
  When the provided callback has been called N times, the DoneCallback
    is invoked with the results set by each function in order.
*/

var fs = require("fs");
var async = require("async");

function DoneCallback (err, results) {
  console.log("All callbacks have finished. err " + err + ", results: " + results.toString());
};

async.parallel(
  [
    function (callback){
      fs.readdir("/tmp", function(err, files) {
        callback(null, files)
      });
    },

    function (callback){
      fs.readdir("/etc", function(err, files) {
        callback(null, files)
      });
    }
  ],

  DoneCallback
);

setInterval(function (){ console.log("TIMER"); }, 5000);
