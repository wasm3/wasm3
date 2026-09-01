(module
  ;; Overwriting a local that still has copies on the stack makes the compiler
  ;; preserve the old value in a scratch slot, counting one use per reference.
  ;; An i64 covers two slots and releasing it decrements both, so counting only
  ;; the first underflowed the second on the last release.
  ;;
  ;; 100 / 7 = 14, then 100 % 14 = 2
  (func (export "to_test") (result i64)
    (local $a i64)
    (local.set $a (i64.const 100))
    (local.get $a)                  ;; copy A
    (local.get $a)                  ;; copy B - two live references to one slot
    (local.tee $a (i64.const 7))    ;; overwrite while both are live
    (i64.div_u)
    (i64.rem_u)
  )
)
