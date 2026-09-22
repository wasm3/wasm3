(module
  (import "env" "_debug" (func $print (param i32 i32) (result i32)))
  (memory 1 1)
  (table 0 0 funcref)
  (global $counter (mut i32) (i32.const 0))
  ;; a second global, so the store sections carry more than one record and
  ;; their index fields have something to disagree with
  (global $limit i32 (i32.const 1000))
  (data (i32.const 0) "snapshot-start")
  (func $init
    i32.const 0
    i32.const 14
    call $print
    drop)
  (start $init)
  ;; an unused v128 local, as an LLVM +simd128 build emits: the frame has to
  ;; carry it across a snapshot even though no SIMD operation ever runs
  (func (export "_start") (local $vec v128)
    (loop $next
      global.get $counter
      i32.const 1
      i32.add
      global.set $counter
      global.get $counter
      global.get $limit
      i32.lt_u
      br_if $next)))
