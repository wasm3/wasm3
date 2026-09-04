(module
  ;; The .wasm beside this file is hand-assembled and does NOT round-trip through
  ;; the text format: the encoding is the whole point of the case.
  ;;
  ;; The counterpart to fc-subopcode-overlong. Here the 0xFC sub-opcode is padded
  ;; to six bytes,
  ;;
  ;;     fc 80 80 80 80 80 00
  ;;
  ;; which is one past the five a LEB128 u32 allows, so the module is malformed and
  ;; has to be refused. The point is that it is refused *as a LEB overflow* by both
  ;; the validator and the compiler: reading the sub-opcode as a single byte instead
  ;; reports "unknown opcode", which is the wrong diagnosis and hid the fact that
  ;; the two paths decoded differently.
  ;;
  ;; The instruction the encoding is reaching for:
  (func (export "to_test") (result i32)
    f32.const 0x1.cp+1
    i32.trunc_sat_f32_s
  )
)
