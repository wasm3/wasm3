;; One br_table that branches back to two loops, $in and $o. Their back edges
;; leave frames of the same shape, so only the loop a snapshot names tells them
;; apart. Run straight through, $inner ends at 6 and $outer at 4. A snapshot
;; taken at the first back edge, to $in, and sent round $o instead ends with
;; $outer at 5.
;;
;; `run` checks what it got: 604 returns, 605 traps as unreachable, and anything
;; else divides by zero. $pre and $x are loops the br_table cannot go to - $pre
;; is not around it, and $x is, but no label of it names $x - and $done is a
;; block.
(module
  (global $inner (mut i32) (i32.const 0))
  (global $outer (mut i32) (i32.const 0))
  (func (export "run")
    (local $i i32)
    (local $r i32)
    (loop $pre)
    (loop $x
      (block $done
        (loop $o
          (global.set $outer (i32.add (global.get $outer) (i32.const 1)))
          (loop $in
            (global.set $inner (i32.add (global.get $inner) (i32.const 1)))
            (local.set $i (i32.add (local.get $i) (i32.const 1)))
            ;; i = 1, 2 go back to $in; 3, 4, 5 to $o; then out
            (br_table $in $o $done (i32.div_u (local.get $i) (i32.const 3)))))))
    (local.set $r
      (i32.add (i32.mul (global.get $inner) (i32.const 100)) (global.get $outer)))
    (if (i32.eq (local.get $r) (i32.const 605))
      (then (unreachable)))
    (if (i32.ne (local.get $r) (i32.const 604))
      (then (drop (i32.div_u (i32.const 1) (i32.const 0)))))))
