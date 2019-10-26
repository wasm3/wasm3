(module
 (export "fib" (func $fib))
 (func $fib (param $n i32) (result i32)
  (if
   (i32.lt_u
    (get_local $n)
    (i32.const 2)
   )
   (return
    (get_local $n)
   )
  )
  (return
   (i32.add
    (call $fib
     (i32.sub
      (get_local $n)
      (i32.const 2)
     )
    )
    (call $fib
     (i32.sub
      (get_local $n)
      (i32.const 1)
     )
    )
   )
  )
 )
)
