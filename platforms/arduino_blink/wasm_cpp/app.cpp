
#include "arduino_api.h"

int LED_BUILTIN;

void setup() {
  LED_BUILTIN = getPinLED();

  // initialize digital pin LED_BUILTIN as an output.
  pinMode(LED_BUILTIN, OUTPUT);
}

// the loop function runs over and over again forever
void loop() {
  digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
  delay(100);                        // wait 100ms
  digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
  delay(900);                        // wait 900ms
}

/*
 * Entry point
 */

WASM_EXPORT
void _start() {
  setup();
  while (1) { loop(); }
}
