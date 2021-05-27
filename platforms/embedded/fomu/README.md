## Build for Fomu

```sh
export PATH=/opt/fomu-toolchain-linux_x86_64-v1.5.6/bin:$PATH
make
```

### Upload:

```sh
dfu-util -D wasm3.dfu
```

## Hints

```sh
# To reboot fomu:
wishbone-tool 0xe0006000 0xac

# To run previously flashed program on Fomu:
dfu-util -e
```

## Debugging

```sh
wishbone-tool -s gdb
```
On second tab:
```sh
riscv64-unknown-elf-gdb wasm3.elf -ex 'target remote localhost:1234'

b m3_CallWithArgs
print *(uint64_t*)env->stack

```
