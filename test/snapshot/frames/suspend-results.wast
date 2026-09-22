;; A suspend with results. The suspended continuation's frame is the stack just
;; after the suspend: the f64 below it, then the i64 and f32 it waits to be
;; resumed with. Its i32 payload has gone to the handler.
;;
;; @pause interrupt
;; @frame root 2 call : |
;; @frame root 0 back-edge loop@+0x3 : i32 |
;; @frame global:0 1 suspend : | f64 i64 f32
;;
;; The bytes spell out:
;;
;; (module
;;   (type $ft (func))
;;   (type $ct (cont $ft))
;;   (type $rest (func (param i64 f32)))
;;   (type $crest (cont $rest))
;;   (tag $e (param i32) (result i64 f32))
;;   (global $k (mut (ref null $crest)) (ref.null $crest))
;;   (elem declare func $child)
;;   (func $spin (local $i i32)
;;     loop $l
;;       local.get $i i32.const 1 i32.add local.set $i
;;       local.get $i i32.const 3 i32.lt_u br_if $l
;;     end)
;;   (func $child
;;     f64.const 0.5
;;     i32.const 7
;;     suspend $e
;;     drop drop drop)
;;   (func (export "run")
;;     block $h (result i32 (ref $crest))
;;       ref.func $child cont.new $ct
;;       resume $ct (on $e $h)
;;       return
;;     end
;;     global.set $k drop
;;     call $spin))

(assert_malformed (module binary
  "\00\61\73\6d\01\00\00\00\01\19\06\60\00\00\5d\00\60\02\7e\7d\00\5d\02\60"
  "\01\7f\02\7e\7d\60\00\02\7f\64\03\03\04\03\00\00\00\0d\03\01\00\04\06\07"
  "\01\63\03\01\d0\03\0b\07\07\01\03\72\75\6e\00\02\09\05\01\03\00\01\01\0a"
  "\40\03\15\01\01\7f\03\40\20\00\41\01\6a\21\00\20\00\41\03\49\0d\00\0b\0b"
  "\12\00\44\00\00\00\00\00\00\e0\3f\41\07\e2\00\1a\1a\1a\0b\15\00\02\05\d2"
  "\01\e0\01\e3\01\01\00\00\00\0f\0b\24\00\1a\10\00\0b"
) "stack switching is newer than this assembler")
