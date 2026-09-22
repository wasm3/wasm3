;; The bundled WABT predates stack switching. This literal module is
;; equivalent to the source below. The malformed wrapper lets that older
;; assembler emit the bytes without interpreting stack-switching opcodes;
;; the snapshot runner executes the extracted module as a valid program.
;;
;; (module
;;   (type $value (func (result i32)))
;;   (type $full (func (param i32 f64 (ref $value)) (result i32)))
;;   (type $cfull (cont $full))
;;   (type $tail (func (param f64 (ref $value)) (result i32)))
;;   (type $ctail (cont $tail))
;;   (type $last (func (param (ref $value)) (result i32)))
;;   (type $clast (cont $last))
;;   (type $done (cont $value))
;;   (tag $need (result i32 f64 (ref $value)))
;;   (global $stage (mut i32) (i32.const 0))
;;   (global $tail (mut (ref null $ctail)) (ref.null $ctail))
;;   (global $last (mut (ref null $clast)) (ref.null $clast))
;;   (global $done (mut (ref null $done)) (ref.null $done))
;;   (elem declare func $value $combine $worker $wrapper)
;;   (func $value (type $value) i32.const 12)
;;   (func $pause (local $n i32)
;;     i32.const 200 local.set $n
;;     loop $next
;;       local.get $n i32.const 1 i32.sub local.tee $n br_if $next
;;     end)
;;   (func $combine (type $full)
;;     local.get 0
;;     local.get 1 i32.trunc_f64_s i32.add
;;     local.get 2 call_ref $value i32.add)
;;   (func $worker (type $value)
;;     suspend $need
;;     call $combine)
;;   (func $wrapper (type $value)
;;     ref.func $worker cont.new $done resume $done)
;;   (func $check (param i32)
;;     local.get 0 i32.const 42 i32.ne if unreachable end)
;;   (func (export "_start") (local $captured (ref null $cfull))
;;     i32.const 10 ref.func $combine cont.new $cfull
;;     cont.bind $cfull $ctail global.set $tail
;;     i32.const 1 global.set $stage call $pause
;;     f64.const 20 global.get $tail cont.bind $ctail $clast global.set $last
;;     i32.const 2 global.set $stage call $pause
;;     ref.func $value global.get $last cont.bind $clast $done global.set $done
;;     i32.const 3 global.set $stage call $pause
;;     global.get $done resume $done call $check
;;     block $capture (result (ref $cfull))
;;       ref.func $wrapper cont.new $done resume $done (on $need $capture)
;;       unreachable
;;     end
;;     local.set $captured
;;     i32.const 7 global.set $stage call $pause
;;     i32.const 10 local.get $captured cont.bind $cfull $ctail global.set $tail
;;     i32.const 4 global.set $stage call $pause
;;     f64.const 20 global.get $tail cont.bind $ctail $clast global.set $last
;;     i32.const 5 global.set $stage call $pause
;;     ref.func $value global.get $last cont.bind $clast $done global.set $done
;;     i32.const 6 global.set $stage call $pause
;;     global.get $done resume $done call $check))

(assert_malformed (module binary
  "\00\61\73\6d\01\00\00\00\01\35\0c\60\00\01\7f\60\03\7f\7c\64\00\01\7f\5d"
  "\01\60\02\7c\64\00\01\7f\5d\03\60\01\64\00\01\7f\5d\05\5d\00\60\00\03\7f"
  "\7c\64\00\60\00\00\60\01\7f\00\60\00\01\64\02\03\08\07\00\09\01\00\00\0a"
  "\09\0d\03\01\00\08\06\18\04\7f\01\41\00\0b\63\04\01\d0\04\0b\63\06\01\d0"
  "\06\0b\63\07\01\d0\07\0b\07\0a\01\06\5f\73\74\61\72\74\00\06\09\08\01\03"
  "\00\04\00\02\03\04\0a\dc\01\07\04\00\41\0c\0b\15\01\01\7f\41\c8\01\21\00"
  "\03\40\20\00\41\01\6b\22\00\0d\00\0b\0b\0d\00\20\00\20\01\aa\6a\20\02\14"
  "\00\6a\0b\06\00\e2\00\10\02\0b\09\00\d2\03\e0\07\e3\07\00\0b\0b\00\20\00"
  "\41\2a\47\04\40\00\0b\0b\93\01\01\01\63\02\41\0a\d2\02\e0\02\e1\02\04\24"
  "\01\41\01\24\00\10\01\44\00\00\00\00\00\00\34\40\23\01\e1\04\06\24\02\41"
  "\02\24\00\10\01\d2\00\23\02\e1\06\07\24\03\41\03\24\00\10\01\23\03\e3\07"
  "\00\10\05\02\0b\d2\04\e0\07\e3\07\01\00\00\00\00\0b\21\00\41\07\24\00\10"
  "\01\41\0a\20\00\e1\02\04\24\01\41\04\24\00\10\01\44\00\00\00\00\00\00\34"
  "\40\23\01\e1\04\06\24\02\41\05\24\00\10\01\d2\00\23\02\e1\06\07\24\03\41"
  "\06\24\00\10\01\23\03\e3\07\00\10\05\0b"
) "stack switching is newer than this assembler")
