;; Function body declared size must match the actual body length.
;; Trailing bytes after the outermost 'end' opcode must be rejected.
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"
    "\03\02\01\00"
    "\07\0b\01\07to_test\00\00"
    "\0a\06\01\04\00\0b\00\00"
  )
  "section underrun while parsing Wasm binary"
)
