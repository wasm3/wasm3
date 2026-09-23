;; The wide arithmetic ops read every operand from a slot, and give back the high
;; half of their result in r0 and the low half in a newly allocated slot. The spec
;; suite only ever hands them locals and returns what they give, so it never
;; reaches the rest of that: operands still in r0 when the op comes, operands out
;; of the constant table, a value in r0 underneath the operands, a result slot
;; that is one an operand was just read from, and one op's results going straight
;; into the next.
;;
;; Each check that fails sets its own bit. Expects 0.
(module
  (func (export "to_test") (result i32)
    (local $a i64) (local $b i64) (local $lo i64) (local $hi i64) (local $err i32)

    (local.set $a (i64.const 0x0123456789abcdef))
    (local.set $b (i64.const -2))

    ;; every operand computed, so each is in r0 when the next one is and has to
    ;; be spilled to make room
    (i64.add (local.get $a) (i64.const 0))
    (i64.add (i64.const 0) (i64.const 1))
    (i64.add (local.get $b) (i64.const 1))
    (i64.add (i64.const 1) (i64.const 1))
    i64.add128
    local.set $hi
    local.set $lo
    (if (i64.ne (local.get $lo) (i64.const 0x0123456789abcdee))
      (then (local.set $err (i32.or (local.get $err) (i32.const 1)))))
    (if (i64.ne (local.get $hi) (i64.const 4))
      (then (local.set $err (i32.or (local.get $err) (i32.const 2)))))

    ;; constants only, one of them three times over
    (i64.sub128 (i64.const 0) (i64.const 0) (i64.const 1) (i64.const 0))
    local.set $hi
    local.set $lo
    (if (i64.ne (local.get $lo) (i64.const -1))
      (then (local.set $err (i32.or (local.get $err) (i32.const 4)))))
    (if (i64.ne (local.get $hi) (i64.const -1))
      (then (local.set $err (i32.or (local.get $err) (i32.const 8)))))

    ;; each result pair consumed by the next op: the high half under two
    ;; constants for i64.add128, then on top, still in r0, for i64.mul_wide_s -
    ;; which gets its low half back in the slot its low operand just came out of
    (i64.mul_wide_u (local.get $a) (local.get $b))
    (i64.const 5)
    (i64.const 7)
    i64.add128
    i64.mul_wide_s
    local.set $hi
    local.set $lo
    (if (i64.ne (local.get $lo) (i64.const 4520864271062013011))
      (then (local.set $err (i32.or (local.get $err) (i32.const 16)))))
    (if (i64.ne (local.get $hi) (i64.const -728760259702106))
      (then (local.set $err (i32.or (local.get $err) (i32.const 32)))))

    ;; a value in r0 below the operands, which has to survive the op
    (i64.add (local.get $a) (i64.const 1))
    (i64.mul_wide_u (local.get $b) (local.get $b))
    drop
    i64.xor
    (if (i64.ne (i64.const 81985529216486900))
      (then (local.set $err (i32.or (local.get $err) (i32.const 64)))))

    (local.get $err)))
