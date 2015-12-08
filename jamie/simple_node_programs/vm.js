global.globalVar = 0;

var vm = require('vm');
var script = new vm.Script('setTimeout(function(){ console.log("Hello world from a callback in a Script"); global.globalVar++; console.log("In script, globalVar is " + global.globalVar);  }, 3000)');

script.runInThisContext();
setTimeout(function(){ global.globalVar++; console.log("Hello world from a callback in main: globalVar is " + globalVar); }, 1000);
