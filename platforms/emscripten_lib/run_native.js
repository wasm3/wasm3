'use strict';

if (typeof(process) != 'undefined') { // Node.js environment?
  var scriptArgs = process.argv.slice(2);
  const fs = require('fs');
  var readFile = (fn) => new Uint8Array(fs.readFileSync(fn));
} else {
  var readFile = (fn) => read(fn, 'binary');
}

let instances = [];

(async() => {
  const wasm = scriptArgs[0];
  const func = scriptArgs[1];
  const args = scriptArgs.slice(2);

  const binary = readFile(wasm);

  for (let i=0; i<1000; i++) { // V8: 1028 max, SpiderMonkey: 32650 max
    let instance = (await WebAssembly.instantiate(binary)).instance;

    instances[i] = instance;

    let result = instance.exports[func](...args);
    //console.log(i, result);
  }
})();
