## Build for ESP-IDF

Download and install ESP-IDF v4.0

```sh
export IDF_PATH=/opt/esp32/esp-idf

# Set tools path if needed:
#export IDF_TOOLS_PATH=/opt/esp32

source $IDF_PATH/export.sh

idf.py menuconfig

# Select target:
idf.py set-target esp32s2beta # or esp32

idf.py build

idf.py -p /dev/ttyUSB0 flash monitor

```
