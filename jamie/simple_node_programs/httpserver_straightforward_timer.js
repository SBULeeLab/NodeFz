var http = require('http');

n_clients = 0;

http.createServer(function (request, response) {
  response.writeHead(200, {'Content-Type': 'text/plain'});
  response.write('1\n');
  response.write('2\n');
  response.write('3\n');
  /* response.write('3\n', function(){ console.log('APP: Just wrote 3\n'); }); */
  response.end('Hello World\n');
  /* console.log('APP: Server handled a client!'); */
  n_clients += 1;
}).listen(8000, function (){ 
  console.log('APP: Server bound to a client\n'); 
});

/* 3 second timer. */
setTimeout(function(){ 
  console.log('APP: Timer went off'); 
  if (n_clients != 2) 
  {
    console.log('APP: Error, n_clients != 2');
    throw 'error';
  }
}, 3000);

/* console.log('Server running at http://127.0.0.1:8000/'); */
