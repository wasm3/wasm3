#!/bin/sh
set -eu

COSMOCC="${COSMOCC:-../../../toolchain/bin/cosmocc}"
SOURCE_DIR=../../source
APP_DIR=../app

EXTRA_FLAGS="-Dd_m3PreferStaticAlloc -Dd_m3HasWASI"

"$COSMOCC" -g -Os -o wasm3 $EXTRA_FLAGS \
    -I"$SOURCE_DIR" \
    "$SOURCE_DIR"/*.c "$APP_DIR"/main.c
