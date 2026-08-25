;; A try region left behind by a branch must not catch a later throw.
;; Returns 1 only if the exception was wrongly caught; correct is an escape.
(module
  (tag $e)
  (func (export "to_test") (result i32)
    (block $caught
      (block $out
        (try_table (catch $e $caught)
          (br $out)))
      (throw $e))
    (i32.const 1)))
