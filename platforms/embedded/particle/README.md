## Build for Particle

You need to install Particle CLI.

Build for `photon`, `electron`, `pi`, `duo`, `p1`, `argon`, `boron`, `xenon`:
```sh
particle compile --followSymlinks photon
```

Upload to device:
```
particle list
particle flash MyDevice1 ./firmware_*.bin

# Or using USB:
particle flash --usb ./firmware_*.bin
```

