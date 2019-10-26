(module
  (func $fib2 (param $n i32) (param $a i32) (param $b i32) (result i32)
    (if (result i32)
        (i32.eqz (get_local $n))
        (then (get_local $a))
        (else (return_call $fib2 (i32.sub (get_local $n)
                                   (i32.const 1))
                          (get_local $b)
                          (i32.add (get_local $a)
                                   (get_local $b))))))

  (func $fib (param i32) (result i32)
    (return_call $fib2 (get_local 0)
                (i32.const 0)   ;; seed value $a
                (i32.const 1))) ;; seed value $b

  (export "fib" (func $fib)))
