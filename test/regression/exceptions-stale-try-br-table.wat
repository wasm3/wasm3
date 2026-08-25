;; As exceptions-stale-try, but the try region is left through br_table.
(module
  (tag $e)
  (func (export "to_test") (result i32)
    (block $caught
      (block $out
        (try_table (catch $e $caught)
          (br_table $out $out (i32.const 0))))
      (throw $e))
    (i32.const 1)))
