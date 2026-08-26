;; A 64-bit address is checked into a scratch slot, never written back over the
;; operand it came from - a constant used as an address has to still be that
;; constant afterwards, since the constant table is shared by the whole function.
;;
;; Storing at 128 with offset 8 makes the effective address 136. Writing that
;; back would leave the third i64.const 128 below reading 136, and the answer
;; would be 143. Expects 135.
(module
  (memory i64 1)
  (func (export "to_test") (result i64)
    (i64.store offset=8 (i64.const 128) (i64.const 7))
    (i64.add
      (i64.load offset=8 (i64.const 128))
      (i64.const 128))))
