var http = require('http');
var fs = require('fs');
var li_file = "lorem-ipsum.txt";
var port = 8000

var client_num = 0;
http.createServer(function (request, response) {
  client_num++;
  response.writeHead(200, {'Content-Type': 'text/plain'});
  response.write('Let me fetch you some lorem ipsum!\n\n');

  var did_handle = 0;
  console.log('Server is getting lorem ipsum for client ' + client_num); 
  fs.readFile(li_file, function (err, data) {
    if(err){
      throw err;
    }

    console.log('Server got data of length ' + data.length + ' for client ' + client_num);
    response.write(data);
    response.end('Enjoy your lorem ipsum\n');
    console.log('Server finished client ' + client_num);
    did_handle = 1;
  });
  console.log('Server requested read for client ' + client_num + '. did_handle ' + did_handle);

}).listen(port);

console.log('Server running at http://127.0.0.1:' + port + '/');
