
COSMOPOLITAN_VERSION=1.0
COSMOPOLITAN_URL=https://github.com/jart/cosmopolitan/releases/download/$COSMOPOLITAN_VERSION/cosmopolitan-amalgamation-$COSMOPOLITAN_VERSION.zip

SOURCE_DIR=../../source

EXTRA_FLAGS="-Dd_m3PreferStaticAlloc -Dd_m3HasWASI"

STD=./cosmopolitan/std

if [ ! -d "./cosmopolitan" ]; then
    echo "Downloading Cosmopolitan..."
    curl -L -o cosmopolitan.zip $COSMOPOLITAN_URL
    unzip cosmopolitan.zip -d cosmopolitan
    rm cosmopolitan.zip
fi

if [ ! -d "$STD/sys" ]; then
    # Generate header stubs
    mkdir -p $STD/sys
    touch $STD/assert.h $STD/limits.h $STD/ctype.h $STD/time.h $STD/errno.h         \
          $STD/inttypes.h $STD/fcntl.h $STD/math.h $STD/stdarg.h $STD/stdbool.h     \
          $STD/stdint.h $STD/stdio.h $STD/stdlib.h $STD/string.h $STD/stddef.h      \
          $STD/sys/types.h $STD/sys/stat.h $STD/unistd.h $STD/sys/uio.h             \
          $STD/sys/random.h
fi

echo "Building Wasm3..."

# TODO: remove -fno-strict-aliasing

gcc -g -Os -static -nostdlib -nostdinc -fno-pie -no-pie -mno-red-zone               \
  -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 -fuse-ld=bfd                        \
  -Wl,-T,cosmopolitan/ape.lds -include cosmopolitan/cosmopolitan.h                  \
  -Wno-format-security -Wfatal-errors -fno-strict-aliasing $EXTRA_FLAGS             \
  -fomit-frame-pointer -fno-stack-check -fno-stack-protector                        \
  -o wasm3.com.dbg -DAPE -I$STD -I$SOURCE_DIR $SOURCE_DIR/*.c ../app/main.c         \
  cosmopolitan/crt.o cosmopolitan/ape.o cosmopolitan/cosmopolitan.a

objcopy -SO binary wasm3.com.dbg wasm3.com

