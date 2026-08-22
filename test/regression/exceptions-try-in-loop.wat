;; op_TryTable claims a native frame. An iteration that failed to give it back
;; would exhaust the native stack well before a million laps. Expects 1000000.
(module
  (func (export "to_test") (result i32) (local $i i32)
    (loop $L
      (try_table (catch_all $L))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $L (i32.lt_u (local.get $i) (i32.const 1000000))))
    (local.get $i)))
