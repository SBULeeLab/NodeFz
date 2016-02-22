#!/usr/bin/nodejs

/* 
   http client. Makes one HTTP GET request to localhost:8000.
*/

var http = require('http');

var options = {
protocol : 'http:', 
host : 'localhost', 
port : 8000,
method : 'GET'
};

http.request( options, function log_response (response) {
  console.log('I got response ' + response);
  response.on('data', function(chunk) { 
    console.log('got %d bytes of data: %s', chunk.length, chunk);
  });
  response.on('end', function() { 
    console.log('No more data is coming');
  });
}).end();
