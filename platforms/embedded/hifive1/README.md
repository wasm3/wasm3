## Build for HiFive1

### Debugging:

On first console, run
```sh
JLinkGDBServer -singlerun -if JTAG -select USB -speed 1000 -jtagconf -1,-1 -device FE310 --port 2331
```

On second console, run `riscv64-unknown-elf-gdb`:
```gdb
file ".pio/build/hifive1-revb/firmware.elf"
target extended-remote :2331
b main
monitor halt
monitor reset
load
c
```
