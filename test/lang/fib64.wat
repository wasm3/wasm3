(module
 (export "fib" (func $fib))
 (func $fib (param $n i64) (result i64)
  (if
   (i64.lt_u
    (get_local $n)
    (i64.const 2)
   )
   (return
    (get_local $n)
   )
  )
  (return
   (i64.add
    (call $fib
     (i64.sub
      (get_local $n)
      (i64.const 2)
     )
    )
    (call $fib
     (i64.sub
      (get_local $n)
      (i64.const 1)
     )
    )
   )
  )
 )
)
