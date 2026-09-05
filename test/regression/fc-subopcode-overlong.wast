;; A script rather than a plain module, because this one has to name its own
;; bytes: `(module binary ...)` reaches wast2json's output untouched, and the
;; encoding is the whole point of the case.
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
;; The module the bytes spell out:
;;
;;     (func (export "to_test") (result i32)
;;       f32.const 0x1.cp+1                   ;; 3.5
;;       i32.trunc_sat_f32_s)                 ;; trunc_sat(3.5) = 3

(module binary
  "\00asm" "\01\00\00\00"                      ;; magic, version

  "\01\05\01" "\60\00\01\7f"                   ;; type:   () -> i32
  "\03\02\01" "\00"                            ;; func:   one function, of type 0
  "\07\0b\01" "\07to_test\00\00"               ;; export: "to_test" is function 0

  "\0a\0f\01"                                  ;; code:   one body,
  "\0d\00"                                     ;;         13 bytes, no locals
  "\43\00\00\60\40"                            ;;           f32.const 3.5
  "\fc\80\80\80\80\00"                         ;;           i32.trunc_sat_f32_s
  "\0b"                                        ;;           end
)
