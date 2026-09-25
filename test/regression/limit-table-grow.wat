(module
  (table 2 funcref)
  (func (export "to_test") (result i32)
    ref.null func i32.const 1 table.grow i32.const 2 i32.ne if unreachable end
    ref.null func i32.const 1 table.grow i32.const -1 i32.ne if unreachable end
    ref.null func i32.const 0 table.grow i32.const 3 i32.ne if unreachable end
    table.size))
