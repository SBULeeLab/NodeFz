// readFileUsingPromises.js
var FS = require('fs'),
    Q = require('q');

Q.nfcall(FS.readFile, "file.txt", "utf-8")
.then(function(data) {      
    console.log('File has been read:', data);
})
.then(function(date) {
    console.log('It sure was nice data');
})
.then(function(date) {
    console.log('Yeah!');
})
.fail(function(err) {
    console.error('Error received:', err);
})
.done();

setInterval(function (){ console.log("TIMER"); }, 5000);
