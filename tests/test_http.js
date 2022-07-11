// Once we call init(), we can not add anymore parsers. Need to run
// each test file individually with jasmine, or load all parsers in
// a support script.
const spicy = require('../spicy.js');
spicy.load('./parsers/http.hlto');
spicy.init();

describe('Smoke test spicy using http.hlto', function() {
  it('lists all loaded parsers', function() {
    const parsers = spicy.parsers();
    expect(parsers).toHaveSize(5);
    expect(parsers).toContain({name: 'HTTP::Requests'});
    expect(parsers).toContain({name: 'HTTP::Request'});
  });

  it('allows looking up parsers', function() {
    const parser = spicy.lookupParser('HTTP::Request');
    expect(parser).toEqual({name: 'HTTP::Request'});
  });

  it('throws for invalid parsers', function() {
    expect(() => spicy.lookupParser('Unknown::Parser')).toThrow('no such parser')
  });
});

describe('Smoke test spicy.processInput using HTTP::Request', function() {
  beforeEach(function() {
    parser = spicy.lookupParser('HTTP::Request');
    decoder = new TextDecoder();
  });

  it('works', function() {
    const r = spicy.processInput(parser, 'GET /test.html HTTP/1.1\r\nHost: test.com\r\n\r\n');
    // console.log(JSON.stringify(r));
    const method = decoder.decode(r.value.request.value.method.value);
    expect(method).toBe('GET');
    const uri = decoder.decode(r.value.request.value.uri.value);
    expect(uri).toBe('/test.html');
  });

  it('throws on non-parser objects', function() {
    expect(() => {
      spicy.processInput({}, 'GET / HTTP/1.1\r\nHost: test.com\r\n\r\n')
    }).toThrowError(TypeError, 'not a spicy parser');
  });

  it('throws on invalid input', function() {
    expect(() => {
      spicy.processInput(parser, 'GET\r\n\r\n')
    // This is a spicy produced message:
    }).toThrowError(/failed to match regular expression.*http\.spicy/);
  });
});
