(module
  (memory 1)
  (memory 1)
  (func (export "to_test") (result i32)
    i32.const 1 memory.grow 1 i32.const 1 i32.ne if unreachable end
    i32.const 1 memory.grow 0 i32.const -1 i32.ne if unreachable end
    memory.size 0
    memory.size 1 i32.const 10 i32.mul
    i32.add))
