/*
  Set a timer to send UDP data to localhost on port 41234
  after 50 ms. Wondering whether this causes one LCBN
  to be active when another LCBN goes off. We'll see...
  Example taken from https://nodejs.org/api/dgram.html
*/

const dgram = require('dgram');
const message = new Buffer('Some bytes');
const client = dgram.createSocket('udp4');

setTimeout(function(){ 
  client.send(message, 0, 5, 41234, 'localhost', function (err) { 
    client.close();
  });
}, 50);

//Start reading from stdin so we don't exit.
process.stdin.resume();
