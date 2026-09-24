(module
  ;; table.get leaves its result in the register, and ref.as_non_null read
  ;; the register's stack-slot alias number as if it were a real slot -
  ;; op_RefAsNonNull then indexed the operand stack with it. OSS-Fuzz
  ;; testcase 5935181438189568.
  (table 1 funcref)
  (func (export "to_test")
    (i32.const 0)
    (table.get 0)
    (ref.as_non_null)
    (drop)
  )
)
