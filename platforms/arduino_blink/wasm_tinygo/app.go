package main


const LOW  = 0
const HIGH = 1

const INPUT           =  0
const OUTPUT          =  1
const INPUT_PULLUP    =  2

//go:wasm-module arduino
//go:export millis
func millis() uint

//go:wasm-module arduino
//go:export delay
func delay(ms uint)

//go:wasm-module arduino
//go:export pinMode
func pinMode(pin, mode uint)

//go:wasm-module arduino
//go:export digitalWrite
func digitalWrite(pin, value uint)

//go:wasm-module arduino
//go:export getPinLED
func getPinLED() uint



const LED uint = 19

func setup() {
  pinMode(LED, 1)
}

func loop() {
  digitalWrite(LED, HIGH)
  delay(100)
  digitalWrite(LED, LOW)
  delay(900)
}

/*
 * Entry point
 */

func main() {
  setup()
  for { loop() }
}
