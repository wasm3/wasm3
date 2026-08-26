;; memory.grow on a 64-bit memory answers the address type, so a failed grow is
;; 2^64-1 rather than the 2^32-1 a 32-bit memory would give, and the size is
;; left where it was. Expects 1.
(module
  (memory i64 1 2)
  (func (export "to_test") (result i32)
    (drop (memory.grow (i64.const 1)))          ;; 1 -> 2 pages
    (i32.and
      ;; past the declared maximum
      (i64.eq (memory.grow (i64.const 100)) (i64.const -1))
      (i64.eq (memory.size) (i64.const 2)))))
