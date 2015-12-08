var assert = require('assert');
var http = require('http');

var client_num = 0;
var MAX_CLIENTS = 5;

http.createServer(function (request, response) {
  client_num++;
  response.writeHead(200, {'Content-Type': 'text/plain'});
  response.end('Hello World\n');
  console.log('Server handled a client!');
  //assert.fail(1, 0, 'Eh?', '!=');
  //assert(1 == 0, 'Eh?');
  assert.notEqual(client_num, MAX_CLIENTS, 'Error, max of ' + MAX_CLIENTS + ' clients handled'); 
}).listen(8124);

console.log('Server running at http://127.0.0.1:8124/');

var thrower = function(should_throw){
  if(should_throw)
    throw new Error("You told me to throw!");
}

var do_throw = function(){ thrower(1); };
var dont_throw = function(){ thrower(0); };

assert.throws( do_throw );
assert.throws( do_throw, /You told me to throw/ );
assert.throws( function () { throw new Error('a'); }, /a/ );

assert.doesNotThrow( dont_throw );
assert.doesNotThrow( function () { return 1; } );
