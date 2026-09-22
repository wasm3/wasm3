;; A back edge from a br_table, to loops that take parameters. The frame holds
;; the stack as it was when the target loop was entered - the f64 from before
;; both loops and the i64 pushed inside $a - then the loop's parameters, which
;; the branch has just written. The i64 pushed inside $b and the br_table's
;; index are left behind.
;;
;; The first branch taken goes to $b; later ones go to $a, then out.
;;
;; @pause interrupt
;; @frame root 0 back-edge loop@+0x23 : i32 i32 f32 | f64 i64 i32 f32
(module
  (func (export "run")
    (local $i i32) (local $y i32) (local $x f32)
    (f64.const 0.5)
    (block $out (result i32 f32)
      (i32.const 1)
      (f32.const 1)
      (loop $a (param i32 f32) (result i32 f32)
        (local.set $x)
        (local.set $y)
        (i64.const 2)
        (local.get $y)
        (local.get $x)
        (loop $b (param i32 f32) (result i32 f32)
          (local.set $x)
          (local.set $y)
          (i64.const 3)
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (local.get $y)
          (local.get $x)
          ;; 1 while $i is odd, 2 while it is even, 0 once it reaches 5
          (select
            (i32.const 0)
            (i32.sub (i32.const 2) (i32.and (local.get $i) (i32.const 1)))
            (i32.ge_u (local.get $i) (i32.const 5)))
          (br_table $out $b $a))
        (local.set $x)
        (local.set $y)
        (drop)
        (local.get $y)
        (local.get $x)))
    (drop)
    (drop)
    (drop)))
