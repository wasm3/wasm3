# Wasm3 hardware support

## Recommended devices (ideal for beginners)

- ESP32-based
  - **LilyGO TTGO T7 V1.4** │ [AliExpress](https://www.aliexpress.com/item/32977375539.html)
  - **M5Stack Fire** │ [AliExpress](https://www.aliexpress.com/item/32847906756.html)
- nRF52840-based
  - **Adafruit CLUE** │ [Adafruit](https://www.adafruit.com/clue)
  - **Arduino Nano 33 BLE (or BLE Sense)** │ [Arduino](https://store.arduino.cc/arduino-nano-33-ble)
  - **Particle Argon** │ [Particle](https://store.particle.io/collections/bluetooth/products/argon)
  - **Adafruit Feather nRF52840** | [Adafruit](https://www.adafruit.com/product/4062)
- Other
  - **Raspberry Pi Pico** | [Raspberry Pi](https://www.raspberrypi.org/products/raspberry-pi-pico)
  - **Adafruit PyGamer/PyBadge/PyBadge LC** │ [Adafruit](https://www.adafruit.com/product/4242)
  - **SparkFun Artemis** | [SparkFun](https://www.sparkfun.com/search/results?term=Artemis)
  - **Teensy 4.0** │ [PJRC](https://www.pjrc.com/store/teensy40.html)
  - **Wemos W600 PICO** │ [AliExpress](https://www.aliexpress.com/item/4000314757449.html)

## Compatibility table

Device                        | Chipset   | Architecture | Clock    | Flash | RAM
---                           |:---:      | ---        |      -----:| ---   | ---
Espressif ESP32               |           | Xtensa LX6 <sup>⚠️</sup> | 240MHz     |  4 MB | 520KB
Particle Argon, Boron, Xenon  | nRF52840  | Cortex-M4F | 64MHz      |  1 MB | 256KB
Particle Photon, Electron     | STM32F205 | Cortex-M3  | 120Mhz     |  1 MB | 128KB
Sparkfun Photon RedBoard      | STM32F205 | Cortex-M3  | 120Mhz     |  1 MB | 128KB
Air602                        | WM W600   | Cortex-M3  | 80MHz      |  1 MB | 160KB+128KB
Adafruit PyBadge            | ATSAMD51J19 | Cortex-M4F | 120MHz     | 512KB | 192KB
Realtek RTL8711               |           | Cortex-M3  | 166MHz     |  2 MB | 2 MB+512KB
Nordic nRF52840               |           | Cortex-M4F | 64MHz      |  1 MB | 256KB
Nordic nRF52833               |           | Cortex-M4F | 64MHz      | 512KB | 128KB
P-Nucleo WB55RG             | STM32WB55RG | Cortex-M4F | 64MHz      |  1 MB | 256KB
Teensy 4.0               |  NXP iMXRT1062 | Cortex-M7  | 600MHz     |  2 MB | 1 MB
Teensy 3.5                    | MK64FX512 | Cortex-M4F | 120MHz     | 512KB | 192KB
MXChip AZ3166            | EMW3166        | Cortex-M4  | 100MHz | 1 MB+2 MB | 256KB
Arduino Due                 | AT91SAM3X8E | Cortex-M3  | 84MHz      | 512KB | 96KB
Sipeed MAIX              |  Kendryte K210 | RV64IMAFDC | 400MHz     | 16 MB | 8 MB
Fomu (soft CPU)       | Lattice ICE40UP5K |      RV32I | 12MHz      |  2 MB | 128KB

## Limited support

The following devices can run Wasm3, however they cannot afford to allocate even a single Linear Memory page (64KB).
This means `memoryLimit` should be set to the actual amount of RAM available, and that in turn usually breaks the allocator of the hosted Wasm application (which still assumes the page is 64KB and performs OOB access).

Device                        | Chipset   | Architecture | Clock     | Flash | RAM
---                           |:---:      | ---         |     -----:| ---   | ---
Espressif ESP8266             |           | Xtensa L106 <sup>⚠️</sup> | 160MHz    |  4 MB | ~50KB (available)
Teensy 3.1/3.2            | NXP MK20DX256 |  Cortex-M4  | 72MHz     | 288KB | 64KB
Blue Pill                     | STM32F103 |  Cortex-M3  | 72MHz     |  64KB | 20KB
Arduino MKR*                  | SAMD21    |  Cortex-M0+ <sup>⚠️</sup> | 48MHz     | 256KB | 32KB
Arduino 101                   | Intel Curie |     ARC32 | 32MHz     | 196KB | 24KB
SiFive HiFive1           |   Freedom E310 |   RV32IMAC | 320MHz     | 16 MB | 16KB
Nordic nRF52832               |           |  Cortex-M4F | 64MHz | 256/512KB | 32/64KB
Nordic nRF51822               |           |  Cortex-M0 <sup>⚠️</sup>  | 16MHz | 128/256KB | 16/32KB
Wicked Device WildFire       | ATmega1284 |  8-bit AVR <sup>⚠️</sup>  | 20MHz     | 128KB | 16KB

### Legend:
 ⚠️ This architecture/compiler currently fails to perform TCO (Tail Call Optimization/Elimination), which leads to sub-optimal interpreter behaviour (intense native stack usage, lower performance).  
There are plans to improve this in future 🦄.
