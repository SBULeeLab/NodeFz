/*
  Q-based promises.
  https://strongloop.com/strongblog/promises-in-node-js-with-q-an-alternative-to-callbacks/
*/

// statFileUsingPromises.js
var fs = require('fs'),
    Q = require('q');

mylog = function (str) {
  //console.log('APP: ' + str);
  console.log('***************************************\n************************\n\n\nAPP: ' + str + '\n\n\n************************\n********************');
};

/* Obtain a promise-based version of fs.stat. */
var fs_stat = Q.denodeify(fs.stat);

/* Stat 4 times, then finish. Same code in all Promise examples. */
var files = ['/tmp', '/tmp', '/tmp'];

statSuccess = function (data) {
  mylog('Stat complete: Data: ' + data);
  if (files.length == 0) {
    return;
  }
  else {
    return fs_stat(files.shift());
  }
};

statFailure = function (err) {
  mylog('Stat complete: Error: ' + err);
};

mylog('Before promise in code')
fs_stat('/tmp')
.then(statSuccess, statFailure)
.then(statSuccess, statFailure)
.then(statSuccess, statFailure)
.done()
mylog('After promise in code')
