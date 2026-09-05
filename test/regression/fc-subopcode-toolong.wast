;; A script rather than a plain module, because this one has to name its own
;; bytes, and because those bytes do not decode: only inside `assert_malformed`
;; does wast2json write out a module it could not read back.
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
;; The instruction the encoding is reaching for, and the module around it:
;;
;;     (func (export "to_test") (result i32)
;;       f32.const 0x1.cp+1                   ;; 3.5
;;       i32.trunc_sat_f32_s)

(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"                    ;; magic, version

    "\01\05\01" "\60\00\01\7f"                 ;; type:   () -> i32
    "\03\02\01" "\00"                          ;; func:   one function, of type 0
    "\07\0b\01" "\07to_test\00\00"             ;; export: "to_test" is function 0

    "\0a\10\01"                                ;; code:   one body,
    "\0e\00"                                   ;;         14 bytes, no locals
    "\43\00\00\60\40"                          ;;           f32.const 3.5
    "\fc\80\80\80\80\80\00"                    ;;           one byte too many
    "\0b"                                      ;;           end
  )
  "unable to read u32 leb128: opcode"
)
