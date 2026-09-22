;; A resume, waiting on the continuation it runs. Its argument and the
;; continuation reference are consumed; the f32 pushed before them is all the
;; stack holds, and the i64 the continuation returns is not there yet.
;;
;; @pause interrupt
;; @frame root 2 resume : | f32
;; @frame target 1 call : i32 |
;; @frame target 0 back-edge loop@+0x3 : i32 |
;;
;; The bytes spell out:
;;
;; (module
;;   (type $ft (func (param i32) (result i64)))
;;   (type $ct (cont $ft))
;;   (tag $e)
;;   (elem declare func $child)
;;   (func $spin (local $i i32)
;;     loop $l
;;       local.get $i i32.const 1 i32.add local.set $i
;;       local.get $i i32.const 3 i32.lt_u br_if $l
;;     end)
;;   (func $child (type $ft)
;;     call $spin
;;     i64.const 1)
;;   (func (export "run")
;;     f32.const 1.5
;;     i32.const 3
;;     ref.func $child cont.new $ct
;;     resume $ct
;;     drop drop))

(assert_malformed (module binary
  "\00\61\73\6d\01\00\00\00\01\0b\03\60\00\00\60\01\7f\01\7e\5d\01\03\04\03"
  "\00\01\00\0d\03\01\00\00\07\07\01\03\72\75\6e\00\02\09\05\01\03\00\01\01"
  "\0a\31\03\15\01\01\7f\03\40\20\00\41\01\6a\21\00\20\00\41\03\49\0d\00\0b"
  "\0b\06\00\10\00\42\01\0b\12\00\43\00\00\c0\3f\41\03\d2\01\e0\02\e3\02\00"
  "\1a\1a\0b"
) "stack switching is newer than this assembler")
