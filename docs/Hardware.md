# Wasm3 hardware support

Device                        | Chipset   | Architecture | Clock    | Flash | RAM
---                           |:---:      | ---        |      -----:| ---   | ---
Espressif ESP32               |           | Xtensa LX6 <sup>⚠️</sup> | 240MHz     |  4 MB | 520KB
Particle Argon, Boron, Xenon  | nRF52840  | Cortex-M4F | 64MHz      |  1 MB | 256KB
Particle Photon, Electron     | STM32F205 | Cortex-M3  | 120Mhz     |  1 MB | 128KB
Air602                        | WM W600   | Cortex-M3  | 80MHz      |  1 MB | 160KB+128KB
Realtek RTL8711               |           | Cortex-M3  | 166MHz     |  2 MB | 2 MB+512KB
Nordic nRF52840               |           | Cortex-M4F | 64MHz      |  1 MB | 256KB
Nordic nRF52833               |           | Cortex-M4F | 64MHz      | 512KB | 128KB
P-Nucleo WB55RG             | STM32WB55RG | Cortex-M4F | 64MHz      |  1 MB | 256KB
Teensy 4.0               |  NXP iMXRT1062 | Cortex-M7  | 600MHz     |  2 MB | 1 MB
Teensy 3.5                    | MK64FX512 | Cortex-M4F | 120MHz     | 512KB | 192KB
MXChip AZ3166            | EMW3166        | Cortex-M4  | 100MHz | 1 MB+2 MB | 256KB
Arduino Due                 | AT91SAM3X8E | Cortex-M3  | 84MHz      | 512KB | 96KB
Sipeed MAIX              |  Kendryte K210 | RV64IMAFDC | 400MHz     | 16 MB | 8 MB
SiFive HiFive1           |   Freedom E310 |   RV32IMAC | 320MHz     | 16 MB | 16KB
Fomu (soft CPU)       | Lattice ICE40UP5K |      RV32I | 12MHz      |  2 MB <sup>⁑</sup> | 128KB

## Limited support

The following devices can run Wasm3, however they cannot afford to allocate even a single Linear Memory page (64KB).
This means `memoryLimit` should be set to the actual amount of RAM available, and that in turn usually breaks the allocator of the hosted Wasm application (which still asumes the page is 64KB and performs OOB access).

Device                        | Chipset   | Architecture | Clock     | Flash | RAM
---                           |:---:      | ---         |     -----:| ---   | ---
Espressif ESP8266             |           | Xtensa L106 <sup>⚠️</sup> | 160MHz    |  4 MB | ~50KB (available)
Teensy 3.1/3.2            | NXP MK20DX256 |  Cortex-M4  | 72MHz     | 288KB | 64KB
Blue Pill                     | STM32F103 |  Cortex-M3  | 72MHz     |  64KB | 20KB
Arduino MKR*                  | SAMD21    |  Cortex-M0+ <sup>⚠️</sup> | 48MHz     | 256KB | 32KB
Arduino 101                   | Intel Curie |     ARC32 | 32MHz     | 196KB | 24KB
Nordic nRF52832               |           |  Cortex-M4F | 64MHz | 256/512KB | 32/64KB
Nordic nRF51822               |           |  Cortex-M0 <sup>⚠️</sup>  | 16MHz | 128/256KB | 16/32KB
Wicked Device WildFire       | ATmega1284 |  8-bit AVR <sup>⚠️</sup>  | 20MHz     | 128KB | 16KB

### Legend:
 ⚠️ This architecture/compiler currently fails to perform TCO (Tail Call Optimization/Elimination), which leads to sub-optimal interpreter behaviour (intense native stack usage, lower performance). There are plans to improve this in future 🦄.
 
 ⁑ Some flash space is used by the bootloader meaning usable space is less.
