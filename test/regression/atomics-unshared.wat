;; A wait on an unshared memory is valid, and traps when it runs - after the address
;; checks, which is why an unaligned address is reported as that. Notify has nothing
;; to wake on such a memory and answers 0.
(module
  (memory 1)

  (func (export "wait") (result i32)
    (memory.atomic.wait32 (i32.const 0) (i32.const 0) (i64.const 0)))

  (func (export "wait_unaligned") (result i32)
    (memory.atomic.wait32 (i32.const 1) (i32.const 0) (i64.const 0)))

  (func (export "notify") (result i32)
    (memory.atomic.notify (i32.const 0) (i32.const 100))))
