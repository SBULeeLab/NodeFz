/*
   Example from https://nodejs.org/api/addons.html
   Shows the invocation of a callback defined in C++ code from JS.
*/

const addon = require('./build/Release/addon');

addon((msg) => {
  console.log(msg); // 'hello world'
});
