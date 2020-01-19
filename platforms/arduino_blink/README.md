## Arduino blink example

This example was tested with ESP32.
ESP8266 is almost working (just needs a workaround for 64KiB wasm pages).

`PlatformIO` is used to build the host interpreter.
You can change the LED pin number in `platformio.ini` with `-DLED_BUILTIN` option.

To run the example on ESP32:

```sh
pio run -e esp32 -t upload && pio device monitor
```

## C++ app

`./wasm_cpp` directory contains an example Arduino app (sketch) that is compiled to WebAssembly.
Compilation is performed using `wasicc` here, but `clang --target=wasm32` can be used as well.
The resulting `wasm` binary is then converted to a C header using `xxd`.
See `wasm_cpp/build.sh` for details.

## Rust app

For Rust, the LED pin is currently hardcoded in the app.
Before building the app, please install the toolchain:
```sh
rustup target add wasm32-unknown-unknown
```
To rebuild, use `wasm_rust/build.sh`.

## AssemblyScript app

For AssemblyScript the LED pin is currently hardcoded in the app.
Before building the app, please install npm dependencies:
```sh
yarn install
```
or
```sh
npm install
```
To rebuild, use `wasm_assemblyscript/build.sh` or `npm run build`.

## TinyGO app

For TinyGO, the LED pin is currently hardcoded in the app.
To rebuild, use `wasm_tinygo/build.sh`.