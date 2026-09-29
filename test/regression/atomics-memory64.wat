;; The atomic instructions take an i64 address when the memory does. The address of
;; an access with operands above it is rewritten in place under them, once for each
;; operand count an atomic can have: one for a load, two for a store or a
;; read-modify-write, three for a cmpxchg.
(module
  (memory i64 1)

  (func (export "rmw") (result i32)
    (i32.store (i64.const 16) (i32.const 40))
    (drop (i32.atomic.rmw.add (i64.const 16) (i32.const 2)))
    (i32.atomic.load (i64.const 16)))

  ;; the static offset is added to the address before it is checked
  (func (export "cmpxchg") (result i64)
    (i64.store (i64.const 24) (i64.const 5))
    (drop (i64.atomic.rmw.cmpxchg offset=8 (i64.const 16) (i64.const 5) (i64.const 9)))
    (i64.atomic.load (i64.const 24)))

  (func (export "store") (result i32)
    (i32.atomic.store offset=8 (i64.const 32) (i32.const 77))
    (i32.load (i64.const 40)))

  (func (export "unaligned") (result i32)
    (i32.atomic.load offset=1 (i64.const 16)))

  ;; the sum of address and offset would wrap a u64
  (func (export "wrap") (result i32)
    (i32.atomic.load offset=8 (i64.const -8)))

  (func (export "oob") (result i32)
    (i32.atomic.load (i64.const 65536)))

  ;; a misaligned address is reported before one out of bounds, as on a 32-bit memory
  (func (export "unaligned_oob") (result i32)
    (i32.atomic.load offset=1 (i64.const 65536)))

  ;; and so is one whose offset is too large to be added without being cut down
  (func (export "unaligned_far_offset") (result i32)
    (i32.atomic.load offset=18446744073709551615 (i64.const 0)))

  (func (export "notify_oob") (result i32)
    (memory.atomic.notify (i64.const 65536) (i32.const 0)))

  (func (export "notify") (result i32)
    (memory.atomic.notify (i64.const 16) (i32.const 1))))
