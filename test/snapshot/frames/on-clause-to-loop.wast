;; An on clause that branches back to a loop outside a try_table. Its pause
;; point is recorded at the resume, inside the try_table the branch leaves. The
;; loop's parameter is the suspended continuation the clause hands over.
;;
;; @pause interrupt
;; @frame root 1 back-edge loop@+0xe : i32 | f64 contref
;;
;; The bytes spell out:
;;
;; (module
;;   (type $ft (func))
;;   (type $ct (cont $ft))
;;   (tag $e)
;;   (elem declare func $child)
;;   (func $child
;;     suspend $e)
;;   (func (export "run") (local $i i32)
;;     f64.const 0.5
;;     ref.null $ct
;;     loop $l (param (ref null $ct))
;;       drop
;;       local.get $i i32.const 1 i32.add local.set $i
;;       local.get $i i32.const 3 i32.lt_u
;;       if
;;         block $x
;;           try_table (catch_all $x)
;;             ref.func $child cont.new $ct
;;             resume $ct (on $e $l)
;;           end
;;         end
;;       end
;;     end
;;     drop))

(assert_malformed (module binary
  "\00\61\73\6d\01\00\00\00\01\0b\03\60\00\00\5d\00\60\01\63\01\00\03\03\02"
  "\00\00\0d\03\01\00\00\07\07\01\03\72\75\6e\00\01\09\05\01\03\00\01\00\0a"
  "\3d\02\04\00\e2\00\0b\36\01\01\7f\44\00\00\00\00\00\00\e0\3f\d0\01\03\02"
  "\1a\20\00\41\01\6a\21\00\20\00\41\03\49\04\40\02\40\1f\40\01\02\00\d2\00"
  "\e0\01\e3\01\01\00\00\03\0b\0b\0b\0b\1a\0b"
) "stack switching is newer than this assembler")
