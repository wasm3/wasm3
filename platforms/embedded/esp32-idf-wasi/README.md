## Build for ESP-IDF, with minimal WASI support

**Note:** Currently, to run this example you need a PSRAM-enabled ESP32 module (this might be improved in future).

Download and install ESP-IDF v4.0

```sh
export IDF_PATH=/opt/esp32/esp-idf

# Set tools path if needed:
#export IDF_TOOLS_PATH=/opt/esp32

source $IDF_PATH/export.sh

idf.py menuconfig

# Select target:
idf.py set-target esp32     # or: esp32s2, esp32c3, esp32s3, linux

idf.py build

idf.py -p /dev/ttyUSB0 flash monitor

```
