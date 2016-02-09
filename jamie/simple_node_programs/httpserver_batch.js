/* 
   http server listening on port 8000
   Incoming clients are placed in an array to be handled later.
   Pending client responses are handled in batches every 3 seconds.

   Clients are sent the contents of /tmp.
*/

var http = require('http');
var fs = require('fs');

/* Array of objects with fields: client_id, request, response. */
var clients_to_handle = [];
var client_count = 0;

http.createServer(function (request, response) {
  var client_id = client_count;
  client_count++;

  var client_obj = {
    "client_id"  : client_id,
    "request"    : request,
    "response"   : response,
  };

  clients_to_handle.push(client_obj);
  console.log('APP: Server received a client request for client ' + client_id);
}).listen(8000);
console.log('APP: Server running at http://127.0.0.1:8000/');

/* Responding to a client. */
function RespondToClient (clientRequest) {
  fs.readdir("/tmp", function respond(err, files){
    clientRequest["response"].writeHead(200, {'Content-Type': 'text/plain'});
    clientRequest["response"].write('Hello client ' + clientRequest["client_id"] + '!\n');
    clientRequest["response"].write('In /tmp I found files: ' + files.toString() + '\n');
    clientRequest["response"].end('Goodbye client ' + clientRequest["client_id"] + '!\n');
    console.log('APP: Server finished client ' + clientRequest["client_id"]);
  });
};

/* Handle all clients in clients_to_handle. */
function HandleClients () {
  var item;
  while (clients_to_handle.length)
  {
    item = clients_to_handle.pop();
    RespondToClient(item);
  }
  clients_to_handle = [];
};

/* HandleClients every 3 seconds. */
setInterval(HandleClients, 3000);
