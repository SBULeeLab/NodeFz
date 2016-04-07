/* 
   http server listening on port 8000.
   This server results in complex schedules involving several components:
    - timers
    - http responses
    - dns lookup
    - FS activity
   It serves as a challenge for my scheduler.
*/

var http = require('http');
var dns = require('dns');
var fs = require('fs');

var client_count = 0;

/* HTTPServer listens on port 8000.
   On request, asynchronously readdir's /tmp and replies with the contents. */
http.createServer(function (request, response) {
  var client_id = client_count;
  client_count++;

  console.log('APP: Server handling client ' + client_id);

  fs.readdir("/tmp", function respond(err, files){
    response.writeHead(200, {'Content-Type': 'text/plain'});
    response.write('Hello client ' + client_id + '!\n');
    response.write('In /tmp I found files: ' + files.toString() + '\n');
    response.end('Goodbye client ' + client_id + '!\n');
    console.log('APP: Server finished client ' + client_id);
  });

  console.log('APP: Server received a client request for client ' + client_id);
}).listen(8000);

/* Timers going off every second for 10 seconds. */
var nums = [1, 2, 3, 4, 5];
nums.forEach(function (num) {
  setTimeout(function(){ console.log("APP: Timer: " + num + " second(s) has/ve passed"); }, num*1000);
});

/* Async operations after a bit. 
   Do this funnily enough enough that the clients have a chance to interweave. */

/* Some FS operations. */
setTimeout(function(){
  var files = ["/tmp/a", "/tmp/b", "/tmp/c", "/tmp/d"];
  files.forEach(function (file) {
    fs.writeFile(file, "aaaaa", function() { 

       console.log("APP: Wrote to " + file); 
       var fileNum = file.charAt(file.length-1).charCodeAt() - "a".charCodeAt();
       var sleepLen = 100*fileNum;
       console.log("APP: sleepLen " + sleepLen);
       setTimeout(function(){
         fs.readFile(file, function(err, data) {
           console.log("APP: Read " + file + ": " + data); 
         });
       }, sleepLen);

    });
  });
}, 250);

/* DNS queries. */
setTimeout(function(){
  var sites = ['www.google.com', 'www.bbc.co.uk', 'www.gmail.com', 'www.cnn.com'];
  sites.forEach(function (site) {
    console.log("APP: Looking up DNS for " + site);
    dns.lookup(site, { family: 6 }, function onLookup(err, addresses, family) {
      if (err) {
        console.log("APP: DNS lookup failed on " + site + ": " + err);
      }
      else {
        console.log("APP: DNS: " + site + " -> " + addresses);
      }
    });
  });
}, 300);

console.log('APP: Server running at http://127.0.0.1:8000/');
