/*
  Bluebird-based promises.
*/

var Promise = require('bluebird');

var fs = Promise.promisifyAll(require("fs"));

var fs = Promise.promisifyAll(require("fs"));
fs.readFileAsync("promise_bb.js", "utf8").then(function(data) {
  console.log("I read my own source code:\n" + data);
});

setInterval(function (){ console.log("TIMER"); }, 5000);
