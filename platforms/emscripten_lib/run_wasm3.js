'use strict';

/*
  Node.js
  -------
    node --v8-options | grep -A1 wasm
    --print_wasm_code --code-comments
    --wasm_interpret_all --trace_wasm_interpreter

  SpiderMonkey
  ------------
    export PATH=/opt/jsshell/:$PATH
    js --help | grep wasm
    --wasm-compiler=baseline/ion/cranelift/baseline+ion/baseline+cranelift
    --wasm-verbose
    --ion-full-warmup-threshold=1
*/

if (typeof(process) != 'undefined') { // Node.js environment?
  var scriptArgs = process.argv.slice(2);
  const fs = require('fs');
  var readFile = (fn) => new Uint8Array(fs.readFileSync(fn));
} else {
  var readFile = (fn) => read(fn, 'binary');
}


// Encode string into Uint8Array (with '\0' terminator)
// Could use TextEncoder instead
function encode(str) {
  const len = str.length;
  const res = new Uint8Array(len + 1);
  let pos = 0;
  for (let i = 0; i < len; i++) {
    const point = str.charCodeAt(i);
    if (point <= 0x007f) {
      res[pos++] = point;
    }
  }
  return res.subarray(0, pos + 1);
}

let instance;
let runtimes = {};

const imports = {
  "env": {
    "emscripten_notify_memory_growth": function() {},
    "emscripten_get_sbrk_ptr": function() {},
  },
  "wasi_snapshot_preview1": {
    "fd_close":   function() { return -1; },
    "fd_seek":    function() { return -1; },
    "fd_write":   function() { return -1; },
    "proc_exit":  function() { }
  }
}

function load(buff) {
  const runtime = instance.exports.new_runtime();
  const ptr = instance.exports.malloc(buff.length);
  const mem  = new Uint8Array(instance.exports.memory.buffer);
  mem.set(buff, ptr);
  instance.exports.load(runtime, ptr, buff.length);
  runtimes[runtime] = { binary_ptr: ptr }
  return runtime;
}

function unload(runtime) {
  if (!runtimes[runtime]) return;
  instance.exports.free_runtime(runtime);
  instance.exports.free(runtimes[runtime].binary_ptr);
  runtimes[runtime] = undefined;
}

function call(runtime, fname, args) {
  // Convert names to buffers
  args = [fname].concat(args).map(arg => encode(arg.toString()));

  const arglen = args.length;
  let argbytes = arglen*4;
  for (let arg of args) {
    argbytes += arg.length;
  }

  // Allocate the required memory
  const buff = instance.exports.malloc(argbytes);
  const mem  = new Uint8Array(instance.exports.memory.buffer);
  const ptrs = new Uint32Array(mem.buffer, buff, arglen);

  // Fill-in memory
  let ptr = buff + ptrs.byteLength;
  for (let i=0; i<arglen; i++) {
    const arg = args[i];
    ptrs[i] = ptr;
    mem.set(arg, ptr);
    ptr += arg.length;
  }

  // Actual call
  const result = instance.exports.call(runtime, arglen, buff);

  // Cleanup
  instance.exports.free(buff);

  return result;
}

(async() => {
  instance = (await WebAssembly.instantiate(readFile('wasm3.wasm'), imports)).instance;
  instance.exports.init();

  const wasm = scriptArgs[0];
  const func = scriptArgs[1];
  const args = scriptArgs.slice(2);

  const binary = readFile(wasm);

  for (let i=0; i<100000; i++) {
    let module = load(binary);

    let result = call(module, func, args);
    //console.log(i, result);

    unload(module);
  }

  console.log(`Memory size: ${instance.exports.memory.buffer.byteLength/(1024*1024)} MB`);
})();
