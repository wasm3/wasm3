## Build for Winner Micro W600

You will need:

- [W600 SDK from ThingsTurn](https://github.com/w600/sdk)
- [gcc-arm-none-eabi-4_9-2015q3](https://launchpad.net/gcc-arm-embedded/4.9/4.9-2015-q3-update)
- [w600tool](https://github.com/vshymanskyy/w600tool)

```sh
export PATH=/opt/gcc-arm-none-eabi-4_9-2015q3/bin:$PATH

export WM_SDK=/opt/WM_W600

./build.sh
```

To upload:

```sh
w600tool.py --upload .output/wasm3_gz.img
```
or
```sh
w600tool.py --upload .output/wasm3.fls
```
