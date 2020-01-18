## Arduino blink example

This example was tested with ESP32.  
ESP8266 is almost working (just needs a workaround for 64KiB wasm pages).

`./wasm_cpp` directory contains an example Arduino app (sketch) that is compiled to WebAssembly.  
Compilation is performed using `wasicc` here, but `clang --target=wasm32` can be used as well.  
The resulting `wasm` binary is then converted to a C header using `xxd`.  
See `build.sh` for details.

`PlatformIO` is used to build the host interpreter.  
You can change the LED pin number in `platformio.ini` with `-DLED_BUILTIN` option.  

To run the example on ESP32:

```sh
pio run -e esp32 -t upload && pio device monitor
```

