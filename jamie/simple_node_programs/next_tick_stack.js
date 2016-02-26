var fs = require('fs');

process.nextTick(function FOO(){ 
  console.log("stat'ing /tmp");
  try{
    fs.stat("/tmp", function (){ console.log('Done with stat'); });
  }
  catch(e) { console.log('Caught exception'); }
  console.log("Hello, world"); 
});

setTimeout(function FOO(){ }, 1000000);
