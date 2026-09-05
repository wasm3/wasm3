# WABT 1.0.41

Part of the `WebAssembly Binary Toolkit`, built for `wasm32-wasip1`.

Source: https://github.com/webassembly/wabt

`wast2json.wasm` is what `run-regression-test.py` and `run-strace-test.py` assemble their
cases with, so that no toolchain has to be installed -
WABT runs on the interpreter under test.

Because the offsets in `test/strace/*.txt` are offsets into the modules it writes,
re-record them with `run-strace-test.py --update` when this directory is updated.
