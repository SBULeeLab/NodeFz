/*
  Simple program for setImmediate.
*/

mylog = function (str) {
  //console.log('APP: ' + str);
  console.log('***************************************\n************************\n\n\nAPP: ' + str + '\n\n\n************************\n********************');
}

mylog("Program begins\n");

/* This will go off in the subsequent loop iteration inside a CHECK_CB. */
setImmediate(function (){ 
  mylog("setImmediate 1\n");

  /* These will go off in the subsequent loop iteration inside a single CHECK_CB. */
  setImmediate(function (){ 
    mylog("setImmediate 1: Nested setImmediate 1\n");
  });
  setImmediate(function (){ 
    mylog("setImmediate 1: Nested setImmediate 2\n");
  });
});

/* This will go off in a later loop iteration alone inside a CHECK_CB. */
setTimeout(function(){
  mylog("setImmediate 2 post-timeout\n");
}, 250);
