(module
  ;; The .wasm beside this file is hand-assembled and does NOT round-trip through
  ;; the text format: the encoding is the whole point of the case.
  ;;
  ;; The 0xFC prefix names its instruction with a LEB128 u32, not a byte, and the
  ;; spec permits non-minimal encodings up to the five bytes a u32 takes. So
  ;; i32.trunc_sat_f32_s below is encoded as
  ;;
  ;;     fc 80 80 80 80 00       ;; sub-opcode 0, padded to the legal maximum
  ;;
  ;; rather than the minimal "fc 00" any assembler would emit. Compile_ExtendedOpcode
  ;; used to read that sub-opcode with Read_u8, so it saw 0x80, found nothing in
  ;; c_operationsFC and rejected a valid module with "unknown opcode". The validator
  ;; read the same bytes as a LEB and accepted them, so the two disagreed.
  ;;
  ;; See fc-subopcode-toolong for the six-byte case, which really is malformed.
  ;;
  ;; trunc_sat(3.5) = 3
  (func (export "to_test") (result i32)
    f32.const 0x1.cp+1
    i32.trunc_sat_f32_s
  )
)
