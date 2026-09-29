;; An atomic read-modify-write on a cell narrower than its operand works in the width
;; of the cell: the operand is truncated to it, the result wraps inside it, and the
;; bytes around it are left alone. On a host that swaps bytes, or one that has no
;; narrow atomic and builds the update out of a wider compare-and-swap, that is where
;; a neighbour is disturbed.
(module
  (memory 1)

  ;; 0xff + 1 wraps to 0 inside its byte and does not carry into the next
  (func (export "rmw8_wrap") (result i32)
    (i32.store8 (i32.const 0) (i32.const 0xff))
    (i32.store8 (i32.const 1) (i32.const 0x55))
    (drop (i32.atomic.rmw8.add_u (i32.const 0) (i32.const 1)))
    (i32.load16_u (i32.const 0)))

  ;; the same, the other way round and at 32 bits inside an i64
  (func (export "rmw32_sub_wrap") (result i64)
    (i64.store (i32.const 8) (i64.const 0x1122334400000000))
    (drop (i64.atomic.rmw32.sub_u (i32.const 8) (i64.const 1)))
    (i64.load (i32.const 8)))

  ;; the expected value is compared as its low byte, and so is the replacement
  ;; stored as one. The old value comes back zero-extended.
  (func (export "cmpxchg8_truncates") (result i32)
    (local i32)
    (i32.store8 (i32.const 4) (i32.const 0xff))
    (local.set 0 (i32.atomic.rmw8.cmpxchg_u (i32.const 4) (i32.const 0x1ff) (i32.const 0x107)))
    (i32.add (i32.shl (local.get 0) (i32.const 8))
             (i32.load8_u (i32.const 4))))

  ;; a mismatch leaves the cell as it was
  (func (export "cmpxchg16_mismatch") (result i32)
    (i32.store16 (i32.const 12) (i32.const 0x1234))
    (drop (i32.atomic.rmw16.cmpxchg_u (i32.const 12) (i32.const 0x1235) (i32.const 0)))
    (i32.load16_u (i32.const 12)))

  ;; xchg hands back the old value, extended, and stores the new one truncated
  (func (export "xchg8") (result i32)
    (i32.store8 (i32.const 16) (i32.const 0x80))
    (i32.add (i32.shl (i32.atomic.rmw8.xchg_u (i32.const 16) (i32.const 0x1fe)) (i32.const 8))
             (i32.load8_u (i32.const 16))))

  ;; the widest and the narrowest loads and stores round-trip through the byte order
  (func (export "store_load") (result i64)
    (i64.atomic.store (i32.const 24) (i64.const 0x0102030405060708))
    (i64.add (i64.atomic.load8_u (i32.const 24))
             (i64.shl (i64.atomic.load16_u (i32.const 26)) (i64.const 8))))

  (func (export "unaligned") (result i32)
    (i32.atomic.load (i32.const 2)))

  ;; unaligned wins over out of bounds
  (func (export "unaligned_oob") (result i32)
    (i32.atomic.load (i32.const 65537)))

  (func (export "oob") (result i32)
    (i32.atomic.load (i32.const 65536))))
