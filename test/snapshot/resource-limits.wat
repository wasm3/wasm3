(module
  (memory 1)
  (table 2 funcref)
  (func (export "prepare")
    i32.const 1 memory.grow drop
    ref.null func i32.const 2 table.grow drop)
  (func (export "run")
    loop $again br $again end))
