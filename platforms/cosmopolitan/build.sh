#!/bin/sh
set -e

COSMOCC_VERSION=4.0.2
COSMOCC_URL=https://cosmo.zip/pub/cosmocc/cosmocc-$COSMOCC_VERSION.zip

SOURCE_DIR=../../source

EXTRA_FLAGS="-Dd_m3PreferStaticAlloc -Dd_m3HasTypedRefs=1 -Dd_m3HasWASI"

if [ ! -d "./cosmocc" ]; then
    echo "Downloading Cosmopolitan toolchain..."
    curl -L -o cosmocc.zip $COSMOCC_URL
    unzip -q cosmocc.zip -d cosmocc
    rm cosmocc.zip
    chmod +x cosmocc/bin/*
fi

echo "Building Wasm3..."

# TODO: remove -fno-strict-aliasing

./cosmocc/bin/cosmocc -g -Os -Wfatal-errors -fno-strict-aliasing $EXTRA_FLAGS      \
  -fomit-frame-pointer -fno-stack-check -fno-stack-protector                       \
  -o wasm3.com -I$SOURCE_DIR $SOURCE_DIR/*.c ../app/main.c
