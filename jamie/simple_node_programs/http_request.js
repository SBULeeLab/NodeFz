/*
  http request sample from https://www.npmjs.com/package/request
  Attempts to reach www.google.com.

  I'm using it to track requests in libuv.
*/

var request = require('request');

request('http://www.google.com', function (error, response, body) {
  if (!error && response.statusCode == 200) {
    console.log("\n\n\n\nI reached google.com. It gave me back " + body.length + " bytes\n\n\n\n\n");
  }
})
