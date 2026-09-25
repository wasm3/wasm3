(module
  (memory 1)
  (func (export "to_test") (result i32)
    i32.const 1 memory.grow i32.const 1 i32.ne if unreachable end
    i32.const 1 memory.grow i32.const -1 i32.ne if unreachable end
    i32.const 0 memory.grow i32.const 2 i32.ne if unreachable end
    memory.size))
