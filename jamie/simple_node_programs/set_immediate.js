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

setTimeout(function(){
  mylog("timeout\n");
  setImmediate(function (){
    mylog("setImmediate 2: 1\n");
  });
  var imm = setImmediate(function (){
    mylog("setImmediate 2: 2: SHOULD NOT GO OFF\n");
    xyz = abc
  });
  clearImmediate(imm);
}, 100);
