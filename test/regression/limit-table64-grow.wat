(module
  (table i64 2 funcref)
  (func (export "to_test") (result i64)
    ref.null func i64.const -1 table.grow))
