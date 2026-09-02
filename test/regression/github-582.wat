(module
  ;; https://github.com/wasm3/wasm3/issues/582
  ;;
  ;; Branch arguments were dropped on the way back to a loop. Compile_Branch
  ;; emitted op_ContinueLoop for an unconditional `br` without first running
  ;; ResolveBlockResults, so the operands never reached the loop's parameter
  ;; slots and every iteration re-read the values the loop was entered with.
  ;; "run" therefore spun forever on a counter that stayed at 0.
  ;;
  ;; The br_if and br_table paths did copy, but copied unconditionally: in
  ;; unreachable code there are no operands to copy and they rejected the
  ;; module with "incorrect value count on stack". The poly-* cases cover that.

  (type $ret_i32   (func (result i32)))
  (type $loop_1    (func (param i32)))
  (type $loop_2    (func (param i32 i32)))
  (type $loop_1_1  (func (param i32) (result i32)))

  ;; the reported case: counts to 5 through a loop parameter
  (func (export "run") (type $ret_i32)
    (local $i i32)
    block $exit (result i32)
      i32.const 0
      loop $again (type $loop_1)
        local.tee $i
        i32.const 5
        i32.ge_s
        if
          local.get $i
          br $exit
        end
        local.get $i
        i32.const 1
        i32.add
        br $again
      end
      unreachable
    end)

  ;; two parameters that trade places every iteration, so the copies collide
  ;; and CopyStackSlotsR has to route one of them through a temp slot
  (func (export "swap") (type $ret_i32)
    (local $a i32) (local $b i32)
    block $exit (result i32)
      i32.const 0
      i32.const 10
      loop $again (type $loop_2)
        local.set $b
        local.tee $a
        i32.const 5
        i32.ge_s
        if
          local.get $b
          br $exit
        end
        local.get $b
        i32.const 1
        i32.add
        local.get $a
        i32.const 1
        i32.add
        br $again
      end
      unreachable
    end)

  ;; branches to a parameterized loop from unreachable code: valid, and must
  ;; compile down to a plain trap rather than a stack-count error
  (func (export "poly-br") (type $ret_i32)
    i32.const 0
    loop (type $loop_1_1)
      unreachable
      br 0
    end)

  (func (export "poly-br-if") (type $ret_i32)
    i32.const 0
    loop (type $loop_1_1)
      unreachable
      br_if 0
    end)

  (func (export "poly-br-table") (type $ret_i32)
    i32.const 0
    loop (type $loop_1_1)
      unreachable
      br_table 0
    end)
)
