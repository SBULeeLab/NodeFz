console.time('foo');
console.time('bar');
for(var i = 0; i < 100; i++)
  ;
console.timeEnd('bar');
for(var i = 0; i < 100; i++)
  ;
console.timeEnd('foo');
