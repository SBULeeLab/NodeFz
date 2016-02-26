/* 
   Invokes leakMem every loop iteration forever. 
   leakMem allocates a very large array.
   You can then watch the garbage collector get triggered in node/deps/v8/src/heap/heap.cc

   TODO See if functions are ever discarded -- libuv games.
*/

function leakMem() {
  console.log("Hello world, leaking some mem!")
  var arr = new Array(1000000)
  setImmediate(leakMem)
}

setImmediate(leakMem)
