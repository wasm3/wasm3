# Misc. helper commands:
#llc-9 --version
#llc-9 -march=wasm32 -mattr=help
#llc-9 -march=riscv32 -mattr=help
#echo | clang-9 -E - -march=native -###

# MSVC
#cl /TP /c /Ox /Oy /FA /Fax64.msvc.S ops.c

FLAGS="-Os -fverbose-asm"

OPT="/media/vshymanskyy/Data"

# x86 - clang and gcc

clang-9 $FLAGS -m64 -S ops.c -o x64.clang.S
gcc $FLAGS -m64 -S ops.c -o x64.S

clang-9 $FLAGS -m32 -S ops.c -o x86.clang.S
gcc $FLAGS -m32 -S ops.c -o x86.S

# clang - risc-v

clang-9 $FLAGS --target=riscv32 -march=rv32i -mabi=ilp32 -S ops.c -o riscv32i.clang.S
clang-9 $FLAGS --target=riscv64 -S ops.c -o riscv64.clang.S

# clang - arm

clang-9 $FLAGS --target=arm -mcpu=cortex-m4 -mthumb -S ops.c -o arm-m4.clang.S
clang-9 $FLAGS --target=arm -mcpu=cortex-m0 -mthumb -S ops.c -o arm-m0.clang.S
clang-9 $FLAGS --target=aarch64 -S ops.c -o arm.aarch64.clang.S

# clang - other

clang-9 $FLAGS --target=mipsel -S ops.c -o mipsel.clang.S
clang-9 $FLAGS --target=ppc32 -S ops.c -o ppc32.clang.S

# enable tail-call for wasm
clang-9 $FLAGS --target=wasm32 -Xclang -target-feature -Xclang +tail-call -S ops.c -o wasm32.clang.S


clang-9 $FLAGS --target=wasm32 -Xclang -target-feature -Xclang +tail-call -nostdlib -Wl,--no-entry -Wl,--export-all -o wasm32.clang.wasm ops.c

# clang - xtensa

export PATH=$OPT/llvm_build_xtensa/bin:$PATH
clang -target xtensa -mcpu=esp32 -O3 -S ops.c -o esp32.clang.S
clang -target xtensa -mcpu=esp8266 -O3 -S ops.c -o esp8266.clang.S

# gcc - xtensa

export PATH=~/.platformio/packages/toolchain-xtensa/bin/:$PATH
xtensa-lx106-elf-gcc $FLAGS -S ops.c -o esp8266.S

export PATH=~/.platformio/packages/toolchain-xtensa32/bin/:$PATH
xtensa-esp32-elf-gcc $FLAGS -S ops.c -o esp32.S

# gcc - arm

export PATH=/opt/gcc-arm-none-eabi-8-2018-q4/bin:$PATH
arm-none-eabi-gcc $FLAGS -mcpu=cortex-m4 -mthumb -mabi=aapcs -S ops.c -o arm-m4.S
arm-none-eabi-gcc $FLAGS -mcpu=cortex-m0 -mthumb -mabi=aapcs -S ops.c -o arm-m0.S

# gcc - risc-v

export PATH=~/.platformio/packages/toolchain-riscv/bin:$PATH
riscv64-unknown-elf-gcc $FLAGS -march=rv32i -mabi=ilp32 -S ops.c -o riscv32i.S
riscv64-unknown-elf-gcc $FLAGS -S ops.c -o riscv64.S

# gcc - mips

export STAGING_DIR=$OPT/openwrt-chaoscalmer/staging_dir
export PATH=$OPT/openwrt-chaoscalmer/staging_dir/toolchain-mipsel_24kec+dsp_gcc-4.8-linaro_uClibc-0.9.33.2/bin:$PATH
mipsel-openwrt-linux-gcc $FLAGS -mno-branch-likely -mips32r2 -mtune=24kc -fno-caller-saves -fno-plt -fhonour-copts -S ops.c -o mips24kc.S

# gcc - arc

export PATH=~/.platformio/packages/toolchain-intelarc32/bin:$PATH
arc-elf32-gcc $FLAGS -S ops.c -o arc32.S

# ----------------------
# Just for fun ;)

export PATH=~/.platformio/packages/toolchain-atmelavr/bin:$PATH
avr-gcc $FLAGS -S ops.c -o avr.S

export PATH=~/.platformio/packages/toolchain-timsp430/bin:$PATH
msp430-gcc $FLAGS -S ops.c -o msp430.S

#clang-9 --target=avr -mmcu=atmega328p $FLAGS -S ops.c -o avr.clang.S
clang-9 --target=msp430 $FLAGS -S ops.c -o msp430.clang.S
