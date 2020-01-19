@external("arduino", "millis")
declare function millis(): u32;

@external("arduino", "delay")
declare function delay(ms: u32): void;

@external("arduino", "pinMode")
declare function pinMode(pin: u32, mode: u32): void;

@external("arduino", "digitalWrite")
declare function digitalWrite(pin: u32, value: u32): void;

@external("arduino", "getPinLED")
declare function getPinLED(): u32;

const LOW: u32  = 0;
const HIGH: u32 = 1;

const INPUT: u32        = 0x0;
const OUTPUT: u32       = 0x1;
const INPUT_PULLUP: u32 = 0x2;

const LED: u32 = 19;

function setup(): void {
  pinMode(LED, OUTPUT);
}

function run(): void {
  digitalWrite(LED, HIGH);
  delay(100);
  digitalWrite(LED, LOW);
  delay(900);
}

/*
 * Entry point
 */
export function _start(): void {
  setup();
  while (1) run();
}