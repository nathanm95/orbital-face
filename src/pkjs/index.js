var Clay = require('@rebble/clay');
var clayConfig = require('./config');

// Clay handles the whole settings round trip: it renders the page, stores the
// answers locally, and posts them to the watch as an AppMessage keyed by the
// same messageKey names declared in package.json.
var clay = new Clay(clayConfig, null, { autoHandleEvents: true });

Pebble.addEventListener('ready', function () {
  console.log('Orbital: PebbleKit JS ready');
});
