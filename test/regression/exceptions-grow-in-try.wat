;; The linear memory can grow inside a try body; the catch handler has to see
;; the new memory, not the one op_TryTable cached on the way in.
;; Expects 7.
(module
  (memory 1)
  (tag $e (param i32))
  (func (export "to_test") (result i32)
    (i32.store
      (i32.const 65536)
      (block $h (result i32)
        (try_table (result i32) (catch $e $h)
          (drop (memory.grow (i32.const 1)))
          (i32.const 7)
          (throw $e))))
    (i32.load (i32.const 65536))))
