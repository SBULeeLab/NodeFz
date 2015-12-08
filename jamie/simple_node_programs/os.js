var os = require('os');

for (var func in os)
{
  if (typeof os[func] == "function")
    console.log(func + " " + os[func]());
}
