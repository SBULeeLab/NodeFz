var http = require('http');

http.createServer(function (request, response) {
  response.writeHead(200, {'Content-Type': 'text/plain'});
  response.write('1\n');
  response.write('2\n');
  response.write('3\n');
  response.end('Hello World\n');
  console.log('APP: Server handled a client!');
}).listen(8000);

console.log('Server running at http://127.0.0.1:8000/');
