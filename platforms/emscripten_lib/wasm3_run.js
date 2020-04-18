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

const isNodeJS = (typeof(process) != 'undefined');

function encode(str) {
    var Len = str.length, resPos = -1;
    var resArr = new Uint8Array(Len * 3);
    for (var i = 0; i !== Len; i += 1) {
        var point = str.charCodeAt(i);
        if (point <= 0x007f) {
            resArr[resPos += 1] = point;
        }
    }
    return resArr.subarray(0, resPos + 1);
}

if (isNodeJS) {
  var scriptArgs = process.argv.slice(2);
  const fs = require('fs');
  var readFile = (fn) => new Uint8Array(fs.readFileSync(fn));
} else {
  var readFile = (fn) => read(fn, 'binary');
}

const env = {
  __memory_base: 0,
  __table_base: 0,
  "env": {
    "emscripten_notify_memory_growth": function() {},
    "emscripten_get_sbrk_ptr": function() {},
  },
  "wasi_snapshot_preview1": {
    "fd_close": function() {},
    "fd_seek": function() {},
    "fd_write": function() { return -1; },
    "proc_exit": function() {}
  }
}

var instance;
//let encoder = new TextEncoder('utf-8');

function strToMem(s) {
  const data = encode(s);
  const ptr = instance.exports.malloc(data.length+1);
  const buf = new Uint8Array(instance.exports.memory.buffer, ptr, data.length+1);
  buf.set(data)
  buf[data.length] = 0;
  return ptr;
}

function bufToMem(data) {
  const ptr = instance.exports.malloc(data.length);
  const buf = new Uint8Array(instance.exports.memory.buffer, ptr, data.length);
  buf.set(data)
  return ptr;
}


function new_runtime() {
  return instance.exports.new_runtime();
}

function free_runtime(runtime) {
  instance.exports.free_runtime(runtime);
}

function load(runtime, buff) {
    const ptr = bufToMem(buff);
    instance.exports.load(runtime, ptr, buff.length);
}

function call(runtime, fname, args) {
    const namePtr = strToMem(fname)

    const argsCnt = args.length;
    const argsPtr = instance.exports.malloc(argsCnt*4);
    const argsArray = new Uint32Array(
      instance.exports.memory.buffer,
      argsPtr, argsCnt
    );

    args.forEach((item, idx) => {
      argsArray[idx] = strToMem(item.toString());
    })

    return instance.exports.call(runtime, namePtr, argsCnt, argsPtr);
}

/* TODO:
 * - callback/hook exported function calls
 *
 */


async function main() {
  const FILE = scriptArgs[0];
  const FUNC = scriptArgs[1];
  const ARGS = scriptArgs.slice(2);

  for (let i=0; i<25000; i++) {
    let rt = new_runtime();

    load(rt, readFile(FILE));

    let result = call(rt, FUNC, [1]);
    //console.log(i, result);

    free_runtime(rt);
  }
}

try {
  WebAssembly.instantiate(readFile('wasm3.wasm'), env).then(result => {
    instance = result.instance;
    instance.exports.init();
    main();

    console.log(`Memory size: ${instance.exports.memory.buffer.byteLength}`);
  })
} catch (e) {
  console.log(JSON.stringify(e.message));
}
