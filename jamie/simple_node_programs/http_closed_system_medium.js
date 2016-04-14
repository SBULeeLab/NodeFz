/*
  An HTTP 'closed system'.
  It defines a server and a client; the client opens an HTTP connection to the server.

  This simulates a more complex server side, in which the server has to do some 
  extra work to honor the client's request.
*/

/* Modules. */
var http = require('http');
var dns = require('dns');
var fs = require('fs');

/* Globals. */
var port = 8000;
var n_clients = 10;

var client_count = 0;

/* Helper functions. */

var mylog = function (str) {
  console.log('APP: ' + str);
};

/* Program code. */

/* Server listens. */
http.createServer(function (request, response) {
  var client_id = client_count;
  client_count++;

  mylog('Server: handling client ' + client_id);

  fs.readdir("/lib", function respond(err, files){
    response.writeHead(200, {'Content-Type': 'text/plain'});
    response.write('Hello client ' + client_id + '!\n');
    response.write('In /tmp I found files: ' + files.toString() + '\n');
    response.end('Goodbye client ' + client_id + '!\n');
    mylog('Server: finished client ' + client_id);
  });
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
  mylog('Client ' + clientNum + ': submitting request');
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
