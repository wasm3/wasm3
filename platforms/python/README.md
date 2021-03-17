# pywasm3

Python binding for Wasm3, the fastest WebAssembly interpreter.

## Install

```sh
pip3 install pywasm3

# Or, if you have a local copy:
python3 setup.py sdist
pip3 install dist/pywasm3-*.tar.gz
```

## Usage example

```py
import wasm3, base64

# WebAssembly binary
WASM = base64.b64decode("AGFzbQEAAAABBgFgAX4"
    "BfgMCAQAHBwEDZmliAAAKHwEdACAAQgJUBEAgAA"
    "8LIABCAn0QACAAQgF9EAB8Dws=")

env = wasm3.Environment()
rt  = env.new_runtime(1024)
mod = env.parse_module(WASM)
rt.load(mod)
func = rt.find_function("fib")
result = func.call_argv("24")
print(result)                       # 46368
```

### License
This project is released under The MIT License (MIT)
