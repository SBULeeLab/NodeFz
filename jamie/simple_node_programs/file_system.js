fs = require('fs');

var before = "/tmp/hello";
var after = "/tmp/world";
var newFile = "/tmp/newFile";

/* Clean up from previous runs. */
try{
  fs.unlinkSync(before);
  fs.unlinkSync(after);
  fs.unlinkSync(newFile);
} catch(err) { console.log('APP: ' + "unlinkSync: caught exception " + err); } 

/* Create 'before'. */
fs.openSync(before, 'w');

/* Stat before -- races with rename. */
fs.stat(before, function (err, stats) {
  var report = "stat complete on " + before + ": " + (err ? "error" : "no error");
  console.log('APP: ' + report);
  if (err) throw err;

  //console.log('APP: ' + 'stats: ' + JSON.stringify(stats));
});

/* Rename before -> after.  
   Then create file newFile. */
fs.rename(before, after, function (err) {
  if (err) throw err;

  console.log('APP: ' + 'rename complete: ' + before + ' -> ' + after);
  fs.openSync(newFile, 'w')
});

/* Stat after -- races with rename. Note that this callback can execute
   before rename's callback and still be successful, provided the thread
   pool has finished handling rename already. */
fs.stat(after, function (err, stats) {
  var report = "stat complete on " + after + ": " + (err ? "error" : "no error");
  console.log('APP: ' + report);
  if (err) throw err;

  //console.log('APP: ' + 'stats: ' + JSON.stringify(stats));
});

var delayStat = function () {
  /* Stat newFile -- races with rename's callback. 
     If the 'stat' operation executes before rename's callback, 
     it will fail. */
  fs.stat(newFile, function (err, stats) {
    var report = "stat complete on " + newFile + ": " + (err ? "error" : "no error");
    console.log('APP: ' + report);
    if (err) throw err;

    //console.log('APP: ' + 'stats: ' + JSON.stringify(stats));
});
}

/* Wait a second, then call delayStat. */
setTimeout(delayStat, 1000);

/* Uncaught/thrown exception handling. */
process.on('uncaughtException', function(err) {
  console.log('APP: ' + "Caught exception " + err);
  try{
    fs.unlinkSync(before);
    fs.unlinkSync(after);
  } catch(err) {} 
});
