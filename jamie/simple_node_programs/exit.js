/* 
  This program demonstrates an exit due to process.exit.
  Invoke as 'node exit.js X' to exit with code X, default 0.
*/

var mylog = function (str) {
  console.log('APP: ' + str);
};

var code = process.argv[2] || 0;
process.exit(code);
