/* 
   http server listening on port 8000
   Responds to clients asynchronously with the contents of /tmp. 
*/

var http = require('http');
var fs = require('fs');

var client_count = 0;

http.createServer(function (request, response) {
  var client_id = client_count;
  client_count++;

  fs.readdir("/tmp", function respond(err, files){
    response.writeHead(200, {'Content-Type': 'text/plain'});
    response.write('Hello client ' + client_id + '!\n');
    response.write('In /tmp I found files: ' + files.toString() + '\n');
    response.end('Goodbye client ' + client_id + '!\n');
    console.log('APP: Server finished client ' + client_id);
  });

  console.log('APP: Server received a client request for client ' + client_id);
}).listen(8000);

console.log('APP: Server running at http://127.0.0.1:8000/');
