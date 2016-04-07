/*
  Simple program for process.nextTick.
*/

console.log("APP: Program begins\n");
process.nextTick(function (){ 
  console.log("APP: nextTick 1\n");
  process.nextTick(function (){ 
    console.log("APP: nextTick 2\n");
  });
});
