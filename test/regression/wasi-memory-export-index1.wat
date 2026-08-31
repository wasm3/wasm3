;; WASI has to address the memory the module exports as "memory", not memory 0.
;; Everything here lives in memory 1, so a host that assumes memory 0 reads a
;; zeroed iovec and writes nothing.
(module
  (import "wasi_snapshot_preview1" "fd_write"
    (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (memory $m0 1)
  (memory $m1 1)
  (export "memory" (memory $m1))
  ;; iovec{buf=16,len=3} at 0, nwritten scratch at 8, "HI\n" at 16
  (data (memory $m1) (i32.const 0)
    "\10\00\00\00\03\00\00\00\00\00\00\00\00\00\00\00HI\n")
  (func $start
    (drop (call $fd_write (i32.const 1) (i32.const 0) (i32.const 1) (i32.const 8))))
  (export "_start" (func $start)))
