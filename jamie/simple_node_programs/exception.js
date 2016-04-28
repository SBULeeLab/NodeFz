/* 
  This program demonstrates an exit due to an uncaught exception.
*/

var fs = require("fs");

var mylog = function (str) {
  console.log('APP: ' + str);
};

fs.statSync("/tmp/no/such/file");
mylog("Error, why am I still alive?");
