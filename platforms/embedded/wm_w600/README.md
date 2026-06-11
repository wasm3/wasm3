## Build for Winner Micro W600

You will need:

- [W600 SDK from ThingsTurn](https://github.com/w600/sdk)
  `git clone --depth=1 --branch=sdk_v3.2.0 https://github.com/w600/sdk.git /opt/w600-sdk`
- [gcc-arm-none-eabi-4_9-2015q3](https://launchpad.net/gcc-arm-embedded/4.9/4.9-2015-q3-update)
- [w600tool](https://github.com/vshymanskyy/w600tool)

```sh
export PATH=/opt/gcc-arm-none-eabi-4_9-2015q3/bin:$PATH

export PATH=/opt/w600tool:$PATH

export WM_SDK=/opt/w600-sdk

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
