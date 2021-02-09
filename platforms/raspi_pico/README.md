## Build for Raspberry Pi Pico

- Download [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) and install its dependencies
- `export PICO_SDK_PATH=/path/to/sdk`
- `mkdir build && cd build && cmake .. && make`
- Copy `build/main.uf2` to your Pico USB Mass Storage Device
- Output is sent to USB serial, connect a terminal to observe
