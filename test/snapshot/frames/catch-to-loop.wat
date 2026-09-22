;; A catch clause that branches back to a loop, through a try_table it leaves on
;; the way. Its pause point is recorded at its own try_table, and the frame is
;; the loop's: the f64 from before it, then its parameter, which is the
;; exception's payload.
;;
;; @pause interrupt
;; @frame root 0 back-edge loop@+0x15 : i32 | f64 i32
(module
  (tag $x (param i32))
  (func (export "run")
    (local $i i32)
    (f64.const 0.5)
    (block $done
      (try_table (catch_all $done)
        (i32.const 0)
        (loop $l (param i32)
          (local.set $i)
          (br_if $done (i32.ge_u (local.get $i) (i32.const 3)))
          (block $skip
            (try_table (catch_all $skip)
              (try_table (catch $x $l)
                (throw $x (i32.add (local.get $i) (i32.const 1)))))))))
    (drop)))
