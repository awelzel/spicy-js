'use strict';
// This is a bit of a mess. With the node-api we do not easily
// have access to the addon path anymore, so we use 'bindings'
// to give us a path t it.
//
// https://github.com/nodejs/node-addon-api/issues/449
//
const addon = require('bindings')('spicy')
addon.__initialize(addon, addon.path)
module.exports = addon


module.exports['lookupParser'] = function(name) {
  const p = addon.parsers().find((p) => p.name === name);
  if (p == null)
    throw 'no such parser';

  return p;
}
