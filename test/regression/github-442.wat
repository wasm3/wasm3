(module
  (func $to_test (result i32)
    i32.const -1
    memory.grow)
  (memory 1 5)
  (export "to_test" (func $to_test))
)
