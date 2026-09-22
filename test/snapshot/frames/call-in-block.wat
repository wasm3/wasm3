;; A call inside a block that takes parameters. The block's parameters are
;; ordinary values on the stack, the value pushed before the block is still
;; below them, and the call's own argument is consumed.
;;
;; @pause interrupt
;; @frame root 1 call : | i64 i32 i32 f64
;; @frame root 0 back-edge loop@+0x3 : f32 i32 |
(module
  (func $spin (param f32) (local $i i32)
    (loop $l
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 3)))))
  (func (export "run")
    (i64.const 7)
    (i32.const 1)
    (i32.const 2)
    (block (param i32 i32) (result i32)
      (f64.const 1.5)
      (call $spin (f32.const 2.5))
      (drop)
      (i32.add))
    (drop)
    (drop)))
