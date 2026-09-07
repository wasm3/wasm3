;; Block types can be encoded as s33 LEB integers (either a non-negative type
;; index x >= 0, 0x40 for empty, or a negative 7-bit value type in [-64, -1]).
;; Negative values smaller than -64 are neither a valid value type nor a valid
;; type index. Truncating the s33 to i8 accepted -39845905 (\xef\xff\xff\x6c)
;; as externref (-17).
(assert_invalid
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"
    "\03\02\01\00"
    "\07\0b\01\07to_test\00\00"
    "\0a\0a\01\08\00"
    "\03\ef\ff\ff\6c"
    "\0b\0b"
  )
  "unknown value_type"
)
