;; A table64 index and a bulk length are both u64. Table sizes stay far below
;; 2^32, so an index is out of bounds simply by being at or above the size - but
;; a start plus a length still has room to wrap. Every function below has to
;; trap; a value coming back means one of those sums landed back in the table.
(module
  (table i64 2 funcref)
  (elem (table 0) (i64.const 0) func $f)
  (type $void_i32 (func (result i32)))
  (func $f (export "f") (result i32) (i32.const 5))

  ;; the table is real and reachable, so a trap below means something
  (func (export "ok") (result i32)
    (call_indirect (type $void_i32) (i64.const 0)))

  (func (export "call_max") (result i32)
    (call_indirect (type $void_i32) (i64.const -1)))

  ;; past anything an i32 index could have named
  (func (export "call_past_u32") (result i32)
    (call_indirect (type $void_i32) (i64.const 4294967296)))

  (func (export "get_max") (result i32)
    (ref.is_null (table.get (i64.const -1))))

  (func (export "set_max")
    (table.set (i64.const -1) (ref.func $f)))

  (func (export "fill_wrap")
    (table.fill (i64.const 0) (ref.func $f) (i64.const -1)))

  (func (export "fill_at_max")
    (table.fill (i64.const -1) (ref.func $f) (i64.const 2)))

  (func (export "copy_at_max")
    (table.copy 0 0 (i64.const -1) (i64.const 0) (i64.const 2)))

  (func (export "copy_wrap")
    (table.copy 0 0 (i64.const 0) (i64.const 0) (i64.const -1))))
