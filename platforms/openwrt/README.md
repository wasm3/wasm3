# wasm3 for OpenWRT

This is **wasm3** package for **OpenWRT**.

 wasm3 is an extremely fast WebAssembly interpreter.

## Build from source

```bash
echo "src-link wasm3 $(M3_ROOT)/platforms/openwrt/" >> ./feeds.conf
./scripts/feeds update -a
./scripts/feeds install -p wasm3 -a
make menuconfig
```
Select ```Languages -> WebAssembly -> wasm3```
```
make -j9
```

## Build just wasm3
```
make package/wasm3/compile V=s
```

## Rebuild wasm3
```
make package/wasm3/{clean,compile,install} V=s
```

## Install on device

```
scp ./bin/ramips/packages/wasm3/wasm3_*.ipk root@DEVICE:/tmp
# Then on device:
opkg install /tmp/wasm3_*.ipk
```
