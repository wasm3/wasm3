;; The companion to wasi-memory-export-index1: the same module with the
;; exported memory at index 0, so it must keep working.
(module
  (import "wasi_snapshot_preview1" "fd_write"
    (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (memory $m0 1)
  (memory $m1 1)
  (export "memory" (memory $m0))
  ;; iovec{buf=16,len=3} at 0, nwritten scratch at 8, "HI\n" at 16
  (data (memory $m0) (i32.const 0)
    "\10\00\00\00\03\00\00\00\00\00\00\00\00\00\00\00HI\n")
  (func $start
    (drop (call $fd_write (i32.const 1) (i32.const 0) (i32.const 1) (i32.const 8))))
  (export "_start" (func $start)))
