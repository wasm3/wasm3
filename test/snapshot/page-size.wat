(module
  ;; a page of one byte: the snapshot has to say so itself, or a reader
  ;; without the module takes these three pages for 192 KiB
  (memory 3 (pagesize 1))
  (global $counter (mut i32) (i32.const 0))
  (data (i32.const 0) "abc")
  (func (export "_start")
    (loop $next
      global.get $counter
      i32.const 1
      i32.add
      global.set $counter
      global.get $counter
      i32.const 1000
      i32.lt_u
      br_if $next)))
