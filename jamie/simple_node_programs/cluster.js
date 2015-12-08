var cluster = require('cluster');
var http = require('http');
var numCPUs = require('os').cpus().length;
var assert = require('assert');

cluster.on('fork', function(worker) {
  console.log('Started worker ' + worker.process.pid);
});

cluster.on('online', function(worker) {
  console.log('Worker ' + worker.process.pid + ' is online');
});

if (cluster.isMaster) {
  // Fork workers.
  for (var i = 0; i < numCPUs; i++) {
    cluster.fork();
  }

  cluster.on('exit', function(worker, code, signal) {
    console.log('worker ' + worker.process.pid + ' died');
  });
} else {
  // Workers can share any TCP connection
  // In this case it is an HTTP server
  console.log("APP: Hello world from worker " + cluster.worker.id);

  var n_connections = 0;

  var serv = http.createServer(function(req, res) {
    res.writeHead(200);
    res.end("hello world from worker " + cluster.worker.id + "\n");
    n_connections++;
    console.log("APP: worker " + cluster.worker.id + " asserting\n");
    assert(0 == 1, 'Hmmm');
    console.log("APP: worker should be dead????\n");
  });

  serv.listen(8000);
  console.log("APP: worker " + cluster.worker.id + ' is listening');

  /* Not sure why this does not work... */
  serv.on('connect', function(request, socket, head) {
    console.log("APP: Worker " + cluster.worker.id + " made a connection\n");
    if(n_connections >= 3){
      console.log("APP: Worker " + cluster.worker.id + "'s server is closing");
      serv.close(); // ??
    }
  });
  
}
