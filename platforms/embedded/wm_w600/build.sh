#!/bin/sh
rm -rf .output

mkdir -p .output/wasm3/obj/m3/

make COMPILE=gcc TARGET=wasm3 FLASH_SIZE=1M VERBOSE=YES

cp $WM_SDK/bin/wasm3/* ./.output/

rm test.bin
