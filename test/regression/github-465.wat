(module
  (type (;0;) (func (result i32)))
  (func (;0;) (type 0) (result i32)
    block $3 (result f32)
      f32.const 0x1
      f32.const 0x2
      br $3
    end
    drop
    i32.const 0
  )
  (export "_start" (func 0)))
