;; The effective address of a 64-bit access is address + offset, which the spec
;; computes as a u65. wasm3 adds them in a plain u64, which is only safe because
;; an address at or above 2^52 is rejected before the addition - see
;; d_m3AddressLimit. Every function here has to trap; a value coming back means
;; the sum wrapped somewhere into range.
(module
  (memory i64 1)

  ;; the memory is real and reachable, so a trap below means something
  (func (export "ok") (result i64)
    (i64.load (i64.const 0)))

  ;; -1 + 8 is 7 if the addition wraps, which is in bounds
  (func (export "wrap_max") (result i64)
    (i64.load offset=8 (i64.const -1)))

  ;; lands exactly on the limit
  (func (export "wrap_limit") (result i64)
    (i64.load offset=4503599627370495 (i64.const 1)))

  (func (export "at_limit") (result i64)
    (i64.load (i64.const 4503599627370496)))

  (func (export "below_limit") (result i64)
    (i64.load (i64.const 4503599627370495)))

  ;; an offset no address could bring into range. The module still has to load:
  ;; it is a trap when the access runs, not a malformed or invalid module.
  (func (export "far_offset") (result i64)
    (i64.load offset=18446744073709551615 (i64.const 0)))

  ;; a store reaches its address from under the value, so it takes the other
  ;; path through the compiler
  (func (export "wrap_store")
    (i64.store offset=8 (i64.const -1) (i64.const 0))))
