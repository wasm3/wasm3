;; The call APIs marshal the argument list into the runtime stack before any
;; compiled code runs, so op_Entry's own overflow check cannot cover those
;; writes. Twenty-eight i64 arguments are 224 bytes of them, well past the end
;; of what --stack-size 128 allocates. Expects a stack overflow trap rather than
;; a write past that allocation.
(module
  (func (export "to_test")
    (param i64 i64 i64 i64 i64 i64 i64 i64 i64 i64 i64 i64 i64 i64
           i64 i64 i64 i64 i64 i64 i64 i64 i64 i64 i64 i64 i64 i64)))
