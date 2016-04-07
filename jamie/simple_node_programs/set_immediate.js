/*
  Simple program for setImmediate.
*/

console.log("APP: Program begins\n");

immediates = [];

immediates.push(setImmediate(function (){ 
  console.log("APP: setImmediate 1\n");
  clearImmediate(immediates[0]);
}));

immediates.push(setImmediate(function (){ 
  console.log("APP: setImmediate 2\n");
  clearImmediate(immediates[1]);
}));
