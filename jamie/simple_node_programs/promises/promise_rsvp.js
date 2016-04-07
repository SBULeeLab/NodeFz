var RSVP = require('rsvp');
var FS = require('fs');

var toss = 0;

function dieToss() {
  //return Math.floor(Math.random() * 6) + 1;  
  var ret = toss;
  toss++;
  toss %= 7;
  return ret;
}

function tossASix() {
  return new RSVP.Promise(function(fulfill, reject) {
    setTimeout(function(){
      var n = Math.floor(Math.random() * 6) + 1;
      if (n === 7) {
        fulfill(n);
      } else {
        reject(n);
      }
    }, 
    50); });
}

function logAndTossAgain(toss) {
  console.log("Tossed a " + toss + ", need to try again.");
  FS.readdir("/tmp", function(data, err) { 
    var res = data; 
    console.log("Hello world from toss " + toss);
  });
  return tossASix();
}

function logSuccess(toss) {
  console.log("Yay, managed to toss a " + toss + ".");
}

function logFailure(toss) {
  console.log("Tossed a " + toss + ". Too bad, couldn't roll a six");
}

/* Synchronous?? */
console.log("Before promise 1");
tossASix()
  .then(null, logAndTossAgain)   
  .then(null, logAndTossAgain)  
  .then(null, logAndTossAgain)  
  .then(null, logAndTossAgain)  
  .then(logSuccess, logFailure); //Roll last time
console.log("After promise 1");

/* Certainly not part of the initial loop. */
/*
setTimeout(function(){
  console.log("Before promise 2");
  tossASix()
    .then(null, logAndTossAgain)   
    .then(null, logAndTossAgain)  
    .then(null, logAndTossAgain) 
    .then(null, logAndTossAgain)
    .then(null, logAndTossAgain) 
    .then(null, logAndTossAgain)
    .then(null, logAndTossAgain)
    .then(null, logAndTossAgain)
    .then(null, logAndTossAgain)
    .then(null, logAndTossAgain)
    .then(null, logAndTossAgain)
    .then(null, logAndTossAgain)
    .then(null, logAndTossAgain)
    .then(null, logAndTossAgain)
    .then(logSuccess, logFailure); //Roll last time
  console.log("After promise 2");
});
*/

setInterval(function (){ console.log("TIMER"); }, 5000);
