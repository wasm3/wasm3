## Arduino blink example

`PlatformIO` is used to build the host interpreter.  
You can change the LED pin number in `platformio.ini` with `-DLED_PIN` option.

To run the example:
```sh
pio run -e <device> -t upload && pio device monitor
```
Where `<device>` is one of: `ESP32`, `ESP8266`, `Arduino101`, `MKR1000`, `BluePill`, `TinyBLE`, `Teensy31`, `WildFireV3`

## Building the WebAssembly app

You can build the wasm binary using `C/C++`, `Rust`, `AssemblyScript`, `TinyGo`, ...  
See https://github.com/wasm3/wasm3-arduino/tree/master/wasm_apps for details.
