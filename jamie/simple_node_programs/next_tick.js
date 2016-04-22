/*
  Simple program for process.nextTick.
  process.nextTick(CB): CB is executed after the current call stack finishes.
*/

mylog = function (str) {
  console.log('APP: ' + str);
}

mylog("Program begins");
setTimeout(function(){
  mylog("Loading up some nextTicks");

  /* Goes off after 'Done loading up some nextTicks'. */
  process.nextTick(function (){
    mylog("nextTick 1 begin");

    /* Goes off after 'nextTick 1 done'. */
    process.nextTick(function (){
      mylog("nextTick 2 begin");

      /* Goes off after 'nextTick 2 done'. */
      process.nextTick(function (){
        mylog("nextTick 3 begin");
        mylog("nextTick 3 done");
      });

      mylog("nextTick 2 done");
    });

    mylog("nextTick 1 done");
  });

  mylog("Done loading up some nextTicks");
}, 1);

/* Goes off after the first setTimeout, therefore after all of the process.nextTick's registered by that timeout. */
setTimeout(function(){
  mylog("Other timeout function");
}, 1);
