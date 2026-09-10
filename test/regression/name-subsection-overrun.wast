;; Name section subsection declared length must not extend past the custom section.
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"
    "\03\02\01\00"
    "\07\0b\01\07to_test\00\00"
    "\0a\04\01\02\00\0b"
    "\00\0d\04name\01\40\01\00\03fib"
  )
  "section overrun while parsing Wasm binary"
)
