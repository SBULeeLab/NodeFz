/*
  An HTTP 'closed system'.
  It defines a server and a client; the client opens an HTTP connection to the server.

  This simulates a simple HTTP server-client transaction.
  The server listens and writes.
  The client connects, reads, and closes. 
*/

/* Modules. */
var http = require('http');

/* Globals. */
var port = 8000;
var n_clients = 10;

/* Helper functions. */

var mylog = function (str) {
  console.log('APP: ' + str);
};

/* Program code. */

/* Server listens. */
http.createServer(function (request, response) {
  response.writeHead(200, {'Content-Type': 'text/plain'});
  response.write('1\n');
  response.write('2\n');
  response.write('3\n');
  /* response.write('3\n', function(){ console.log('APP: Just wrote 3\n'); }); */
  response.end('Hello World\n');
  /* console.log('APP: Server handled a client!'); */
}).listen(port, function (){ 
  mylog('Server listening'); 
});

/* Client speaks. */
var options = {
  protocol : 'http:', 
  host : 'localhost', 
  port : port,
  /* method : 'GET' */
  /* method : 'PUT' */
  method : 'POST'
};

var clientNums = new Array();
for (var i = 0; i < n_clients; i++) {
  clientNums.push(i);
}
clientNums.forEach(function (clientNum) {
  http.request(options, function log_response (response) {
    mylog('Client ' + clientNum + ': I got response ' + response);
    response.on('data', function(chunk) { 
      mylog('Client ' + clientNum + ': Got ' + chunk.length + ' bytes of data: ' + chunk);
    });
    response.on('end', function() { 
      mylog('Client ' + clientNum + ': No more data is coming');
    });
  }).end();
});
