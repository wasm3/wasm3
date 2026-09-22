;; A try region left behind by a resume's `on` clause must not catch a later
;; throw - the stack-switching sibling of exceptions-stale-try.
;;
;; op_TryTable's native frame outlives the region when control leaves it
;; forward, so whatever leaves it retires the region's handlers with
;; op_PopHandlers: a branch, a catch clause, and the stub an `on` clause jumps
;; through. Returning 2 means the try_table caught the throw that follows it;
;; correct is an escape.
;;
;; The bundled WABT predates stack switching. The malformed wrapper lets it
;; emit these bytes without reading the stack-switching opcodes, and the runner
;; takes the module out of it all the same. The bytes spell out:
;;
;; (module
;;   (type $ft (func))
;;   (type $ct (cont $ft))
;;   (tag $e)
;;   (tag $x)
;;   (elem declare func $child)
;;   (func $child suspend $e)
;;   (func (export "to_test") (result i32)
;;     block $out
;;       block $h (result (ref $ct))
;;         try_table (catch $x $out)
;;           ref.func $child cont.new $ct
;;           resume $ct (on $e $h)
;;         end
;;         i32.const 1 return
;;       end
;;       drop throw $x
;;     end
;;     i32.const 2))

(assert_malformed (module binary
  "\00\61\73\6d\01\00\00\00\01\0a\03\60\00\00\5d\00\60\00\01\7f\03\03\02\00"
  "\02\0d\05\02\00\00\00\00\07\0b\01\07\74\6f\5f\74\65\73\74\00\01\09\05\01"
  "\03\00\01\00\0a\29\02\04\00\e2\00\0b\22\00\02\40\02\64\01\1f\40\01\00\01"
  "\01\d2\00\e0\01\e3\01\01\00\00\01\0b\41\01\0f\0b\1a\08\01\0b\41\02\0b"
) "stack switching is newer than this assembler")
