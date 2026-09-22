;; A resume_throw_ref, waiting on the continuation it threw into. The exception
;; and the continuation reference are consumed; the f64 pushed before them is
;; all the stack holds. The continuation caught the exception and left its
;; try_table, so none stands around its call.
;;
;; @pause interrupt
;; @frame root 2 resume : exnref contref | f64
;; @frame target 1 call : |
;; @frame target 0 back-edge loop@+0x3 : i32 |
;;
;; The bytes spell out:
;;
;; (module
;;   (type $ft (func))
;;   (type $ct (cont $ft))
;;   (tag $e)
;;   (tag $x)
;;   (elem declare func $child)
;;   (func $spin (local $i i32)
;;     loop $l
;;       local.get $i i32.const 1 i32.add local.set $i
;;       local.get $i i32.const 3 i32.lt_u br_if $l
;;     end)
;;   (func $child
;;     block $c
;;       try_table (catch_all $c)
;;         suspend $e
;;       end
;;       return
;;     end
;;     call $spin)
;;   (func (export "run") (local $exn exnref) (local $k (ref null $ct))
;;     block $got (result exnref)
;;       try_table (catch_all_ref $got)
;;         throw $x
;;       end
;;       unreachable
;;     end
;;     local.set $exn
;;     block $h (result (ref $ct))
;;       ref.func $child cont.new $ct
;;       resume $ct (on $e $h)
;;       return
;;     end
;;     local.set $k
;;     f64.const 2.5
;;     local.get $exn
;;     local.get $k
;;     resume_throw_ref $ct
;;     drop))

(assert_malformed (module binary
  "\00\61\73\6d\01\00\00\00\01\06\02\60\00\00\5d\00\03\04\03\00\00\00\0d\05"
  "\02\00\00\00\00\07\07\01\03\72\75\6e\00\02\09\05\01\03\00\01\01\0a\60\03"
  "\15\01\01\7f\03\40\20\00\41\01\6a\21\00\20\00\41\03\49\0d\00\0b\0b\10\00"
  "\02\40\1f\40\01\02\00\e2\00\0b\0f\0b\10\00\0b\37\02\01\69\01\63\01\02\69"
  "\1f\40\01\03\00\08\01\0b\00\0b\21\00\02\64\01\d2\01\e0\01\e3\01\01\00\00"
  "\00\0f\0b\21\01\44\00\00\00\00\00\00\04\40\20\00\20\01\e5\01\00\1a\0b"
) "stack switching is newer than this assembler")
