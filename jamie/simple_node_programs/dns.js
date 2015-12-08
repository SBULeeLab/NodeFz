var dns = require('dns');

dns.lookup('www.google.com', { family: 6 }, function onLookup(err, addresses, family) {
    console.log('addresses:', addresses);
});
