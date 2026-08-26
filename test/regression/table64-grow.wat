;; table.grow on a table64 answers the table's index type, so a failed grow is
;; 2^64-1 rather than the 2^32-1 a plain table would give, and the size is left
;; where it was. Expects 1.
(module
  (table i64 1 2 funcref)
  (func (export "to_test") (result i32)
    (drop (table.grow 0 (ref.null func) (i64.const 1)))      ;; 1 -> 2 entries
    (i32.and
      ;; past the declared maximum
      (i64.eq (table.grow 0 (ref.null func) (i64.const 100)) (i64.const -1))
      (i64.eq (table.size 0) (i64.const 2)))))
