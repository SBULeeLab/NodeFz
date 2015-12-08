var fs = require('fs');

fs.readFile('/some/file/that/does-not-exist', function nodeStyleCallback(err, data) {
    console.log(err)  // Error: ENOENT
    console.log(data) // undefined / null
});

fs.readFile('/etc/passwd', function(err, data) {
    console.log(err)  // null
    console.log(data) // <Buffer: ba dd ca fe>
})

/* Invalid, should crash program. */
try{
  fs.readFile('/no/such/file', function(err, data) {
    if(err){ throw err; }
    else console.log("No error encountered, data: " + data);
  })
} catch(err){
  console.log("Error: " + err);
}

/* ...nevermind, now we're catching it at the last possible moment. */
process.on('uncaughtException', function(err, data) {
  console.log("listener 1: I caught uncaughtException " + err);
});
process.on('uncaughtException', function(err, data) {
  console.log("listener 2: I caught uncaughtException " + err);
});
process.on('uncaughtException', function(err, data) {
  console.log("listener 3: I caught uncaughtException " + err);
});
process.on('uncaughtException', function(err, data) {
  console.log("listener 4: I caught uncaughtException " + err);
});
process.on('uncaughtException', function(err, data) {
  console.log("Listener 5: Setting a timer");
  setTimeout( function() { console.log("listener 5: I caught uncaughtException " + err); }, 1000);
});
process.on('uncaughtException', function(err, data) {
  console.log("listener 6: I caught uncaughtException " + err);
});
process.on('uncaughtException', function(err, data) {
  console.log("listener 7: I caught uncaughtException " + err);
});
process.on('uncaughtException', function(err, data) {
  console.log("listener 8: I caught uncaughtException " + err);
});
process.on('uncaughtException', function(err, data) {
  console.log("listener 9: I caught uncaughtException " + err);
});
process.on('uncaughtException', function(err, data) {
  console.log("listener 10: I caught uncaughtException " + err);
});
process.on('uncaughtException', function(err, data) {
  console.log("listener 11: I caught uncaughtException " + err);
});
process.on('uncaughtException', function(err, data) {
  console.log("listener 12: I caught uncaughtException " + err);
});
