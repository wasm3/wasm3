(module
  ;; wasm-coremark's minimal build imports clock_ms in the i32 form
  (import "env" "clock_ms" (func $clock_ms (result i32)))

  ;; the value is a clock reading, so only the fact that the call went through
  ;; is worth asserting on
  (func (export "to_test") (result i32)
    (drop (call $clock_ms))
    (i32.const 1)
  )
)
