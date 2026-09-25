(module
  (memory i64 1)
  (func (export "to_test") (result i64)
    i64.const 1 memory.grow i64.const 1 i64.ne if unreachable end
    i64.const 1 memory.grow i64.const -1 i64.ne if unreachable end
    memory.size))
