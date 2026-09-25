(module
  (memory 1)
  (table 2 funcref)
  (func (export "_start")
    i32.const 1 memory.grow drop
    ref.null func i32.const 2 table.grow drop
    loop $again br $again end))
