#![allow(dead_code)]

#[link(wasm_import_module = "arduino")]
extern {
  #[link_name = "millis"]         fn unsafe_millis() -> u32;
  #[link_name = "delay"]          fn unsafe_delay(ms: u32);
  #[link_name = "pinMode"]        fn unsafe_pinMode(pin:u32, mode:u32);
  #[link_name = "digitalWrite"]   fn unsafe_digitalWrite(pin:u32, value:u32);

  #[link_name = "getPinLED"]      fn unsafe_getPinLED() -> u32;
}

pub static LOW:u32  = 0;
pub static HIGH:u32 = 1;

pub static INPUT:u32          = 0x0;
pub static OUTPUT:u32         = 0x1;
pub static INPUT_PULLUP:u32   = 0x2;

pub fn millis         () -> u32              { unsafe { unsafe_millis() } }
pub fn delay          (ms: u32)              { unsafe { unsafe_delay(ms); } }
pub fn pin_mode       (pin:u32, mode:u32)    { unsafe { unsafe_pinMode(pin, mode) } }
pub fn digital_write  (pin:u32, value:u32)   { unsafe { unsafe_digitalWrite(pin, value) } }
pub fn get_pin_led    () -> u32              { unsafe { unsafe_getPinLED() } }

