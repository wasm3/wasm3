;; With one thread, nothing can notify a waiter: a wait answers "not equal" when the
;; cell differs, "timed out" once its timeout has passed, and can never end when it
;; has none. Notify wakes nobody.
(module
  (memory 1 1 shared)

  (func (export "not_equal") (result i32)
    (memory.atomic.wait32 (i32.const 0) (i32.const 1) (i64.const -1)))

  (func (export "timed_out") (result i32)
    (memory.atomic.wait32 (i32.const 0) (i32.const 0) (i64.const 1000000)))

  (func (export "timed_out64") (result i32)
    (memory.atomic.wait64 (i32.const 8) (i64.const 0) (i64.const 0)))

  (func (export "forever") (result i32)
    (memory.atomic.wait32 (i32.const 0) (i32.const 0) (i64.const -1)))

  (func (export "notify") (result i32)
    (memory.atomic.notify (i32.const 0) (i32.const 100))))
