#include "m3/m3_api_defs.h"
#include "m3/m3_env.h"

#include <Arduino.h>

/*
 * Note: each RawFunction should complete with one of these calls:
 *   m3ApiReturn(val)   - Returns a value
 *   m3ApiSuccess()     - Returns void (and no traps)
 *   m3ApiTrap(trap)    - Returns a trap
 */

m3ApiRawFunction(m3_arduino_millis)
{
    m3ApiReturnType (uint32_t)

    m3ApiReturn(millis());
}

m3ApiRawFunction(m3_arduino_delay)
{
    m3ApiGetArg     (uint32_t, ms)

    //printf("api: delay %d\n", ms); // you can also trace API calls

    delay(ms);

    m3ApiSuccess();
}

// This maps pin modes from arduino_wasm_api.h
// to actual platform-specific values
uint8_t mapPinMode(uint8_t mode)
{
    switch(mode) {
    case (0): return INPUT;
    case (1): return OUTPUT;
    case (2): return INPUT_PULLUP;
    }
    return INPUT;
}

m3ApiRawFunction(m3_arduino_pinMode)
{
    m3ApiGetArg     (uint32_t, pin)
    m3ApiGetArg     (uint32_t, mode)

    pinMode(pin, mapPinMode(mode));

    m3ApiSuccess();
}

m3ApiRawFunction(m3_arduino_digitalWrite)
{
    m3ApiGetArg     (uint32_t, pin)
    m3ApiGetArg     (uint32_t, value)

    digitalWrite(pin, value);

    m3ApiSuccess();
}

// This is a convenience function
m3ApiRawFunction(m3_arduino_getPinLED)
{
    m3ApiReturnType (uint32_t)

    m3ApiReturn(LED_BUILTIN);
}


M3Result  m3_LinkArduino  (IM3Runtime runtime)
{
    IM3Module module = runtime->modules;
    const char* arduino = "arduino";

    m3_LinkRawFunction (module, arduino, "millis",           "i()",    &m3_arduino_millis);
    m3_LinkRawFunction (module, arduino, "delay",            "v(i)",   &m3_arduino_delay);
    m3_LinkRawFunction (module, arduino, "pinMode",          "v(ii)",  &m3_arduino_pinMode);
    m3_LinkRawFunction (module, arduino, "digitalWrite",     "v(ii)",  &m3_arduino_digitalWrite);

    m3_LinkRawFunction (module, arduino, "getPinLED",        "i()",    &m3_arduino_getPinLED);

    return m3Err_none;
}

