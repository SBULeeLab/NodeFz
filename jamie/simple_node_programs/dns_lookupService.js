/*
  Exercise dns.lookupService, which ultimately calls uv_getnameinfo
  Taken from https://nodejs.org/api/dns.html#dns_dns_lookup_hostname_options_callback
*/

const dns = require('dns');
dns.lookupService('127.0.0.1', 22, (err, hostname, service) => {
  console.log('APP: hostname ' + hostname + ', service (port 22) ' + service); // localhost, ssh
});

//Start reading from stdin so we don't exit.
process.stdin.resume();
