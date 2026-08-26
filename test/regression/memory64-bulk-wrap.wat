;; The bulk operations take 64-bit addresses and lengths on a 64-bit memory, so
;; start + length has the same room to wrap that a load's address + offset does.
;; All four must trap.
(module
  (memory i64 1)

  ;; a length of 2^64-1 from 0
  (func (export "fill_wrap")
    (memory.fill (i64.const 0) (i32.const 0xFF) (i64.const -1)))

  ;; a start at the very top, with a length that would wrap back into the memory
  (func (export "fill_at_max")
    (memory.fill (i64.const -1) (i32.const 0xFF) (i64.const 2)))

  (func (export "copy_wrap")
    (memory.copy (i64.const 0) (i64.const 0) (i64.const -1)))

  (func (export "copy_at_max")
    (memory.copy (i64.const -8) (i64.const 0) (i64.const 16))))
