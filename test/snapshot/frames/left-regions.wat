;; A back edge after a try_table and a loop the function has already left. An
;; engine may still keep something of those, but they are no part of the frame,
;; and the back edge names the loop it goes to: $l, not $once.
;;
;; @pause interrupt
;; @frame root 0 back-edge loop@+0x11 : i32 |
(module
  (func (export "run")
    (local $i i32)
    (block $b
      (try_table (catch_all $b)
        (nop)))
    (loop $once
      (nop))
    (loop $l
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (i32.const 3))))))
