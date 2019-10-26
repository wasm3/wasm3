# Performance

```log
Function:                                fib(24)   fib64(24)      comments
----------------------------------------------------------------------------------------------------
ESP8266             LX106 @ 160MHz         288ms       299ms      no TCO
ESP32                 LX6 @ 240MHz         410ms       430ms      no TCO
MXChip AZ3166      Arm M4 @ 100Mhz         651ms       713ms
WM W600            Arm M3 @ 80MHz          716ms       782ms      stack used: 1952
WM W600, TCO off   Arm M3 @ 80MHz          846ms       914ms      stack used: 8168
BleNano2 (nRF52)   Arm M4 @ 64MHz          1.19s       1.29s
Arduino MKR1000   Arm M0+ @ 48MHz          1.93s       2.06s      no TCO
TinyBLE (nRF51)    Arm M0 @ 16MHz          5.58s       5.93s      no TCO, 16 Kb total RAM
BluePill           Arm M3 @ 72MHz          7.62s       8.20s
HiFive1 (FE310)  rv32imac @ 320MHz         9.10s       9.82s   <- something wrong here?
Fomu                rv32i @ 12MHz          25.2s       26.1s
```
