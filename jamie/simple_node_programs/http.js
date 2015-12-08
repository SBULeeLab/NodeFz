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
  console.log("Hello world from worker " + cluster.worker.id);

  var n_connections = 0;

  var serv = http.createServer(function(req, res) {
    res.writeHead(200);
    res.end("hello world from worker " + cluster.worker.id + "\n");
    n_connections++;
    //assert(0 == 1, 'Hmmm');
  });

  serv.listen(8000);
  console.log("worker " + cluster.worker.id + ' is listening');

  /* Not sure why this does not work... */
  serv.on('request', function(request, response) {
    console.log("Worker " + cluster.worker.id + " received a request");
    if(n_connections >= 3){
      // close sockets
      console.log("Closing worker " + cluster.worker.id + "'s server");
      this.close();
      console.log("Closing sockets");
      while(sockets.length)
      {
        var s = sockets.pop();
        console.log("Destroying socket " + s);
        s.destroy();
        delete s;
      }
      process.exit(0);
    }
  });

  var sockets = new Array();

  serv.on('connection', function (socket){
    sockets.push(socket);
  });

  serv.on('close', function() {
    console.log("Worker " + cluster.worker.id + "'s server is truly closing");
  });
  
}
