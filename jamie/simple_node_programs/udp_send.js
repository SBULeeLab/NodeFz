/*
  Send UDP data to localhost on port 41234.
  Example taken from https://nodejs.org/api/dgram.html
*/

const dgram = require('dgram');
const message = new Buffer('Some bytes');
const client = dgram.createSocket('udp4');

client.send(message, 0, 5, 41234, 'localhost', (err) => {
    client.close();
});

//Start reading from stdin so we don't exit.
process.stdin.resume();
