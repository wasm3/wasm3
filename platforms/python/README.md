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
import wasm3

# WebAssembly binary
WASM = bytes.fromhex("""
  00 61 73 6d 01 00 00 00 01 06 01 60 01 7e 01 7e
  03 02 01 00 07 07 01 03 66 69 62 00 00 0a 1f 01
  1d 00 20 00 42 02 54 04 40 20 00 0f 0b 20 00 42
  02 7d 10 00 20 00 42 01 7d 10 00 7c 0f 0b
""")

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
