;; (module
;;   (type $f (func)) (type $c (cont $f))
;;   (elem declare func $child)
;;   (func $child ref.func $child cont.new $c resume $c)
;;   (func (export "to_test") ref.func $child cont.new $c resume $c))
;; A fifth concurrently active stack exceeds --max-continuations 4.
(assert_malformed (module binary
  "\00\61\73\6d\01\00\00\00\01\06\02\60\00\00\5d\00"
  "\03\03\02\00\00\07\0b\01\07to_test\00\01\09\05\01\03\00\01\00"
  "\0a\15\02\09\00\d2\00\e0\01\e3\01\00\0b"
  "\09\00\d2\00\e0\01\e3\01\00\0b"
) "stack switching is newer than this assembler")
