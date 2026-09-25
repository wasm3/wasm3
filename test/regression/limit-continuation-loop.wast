;; The bundled WABT predates stack switching. These bytes spell out:
;; (module
;;   (type $f (func)) (type $c (cont $f)) (type $result (func (result i32)))
;;   (elem declare func $child)
;;   (func $child)
;;   (func (export "to_test") (result i32) (local $n i32)
;;     i32.const 1000 local.set $n
;;     loop $again
;;       ref.func $child cont.new $c resume $c
;;       local.get $n i32.const 1 i32.sub local.tee $n br_if $again
;;     end i32.const 1))
(assert_malformed (module binary
  "\00\61\73\6d\01\00\00\00\01\0a\03\60\00\00\5d\00\60\00\01\7f"
  "\03\03\02\00\02\07\0b\01\07to_test\00\01\09\05\01\03\00\01\00"
  "\0a\23\02\02\00\0b\1e\01\01\7f\41\e8\07\21\00\03\40"
  "\d2\00\e0\01\e3\01\00\20\00\41\01\6b\22\00\0d\00\0b\41\01\0b"
) "stack switching is newer than this assembler")
