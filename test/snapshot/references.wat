(module
  (type $value (func (result i32)))
  (type $other (func (param i32) (result i32)))
  (global $stage (mut i32) (i32.const 0))
  (global $nullable (mut (ref null $value)) (ref.func $value))
  (global $nonnull (ref $value) (ref.func $value))
  (table 1 (ref null $value))
  (elem (i32.const 0) (ref null $value) (ref.func $value))
  (elem declare func $other)
  (func $value (type $value) i32.const 42)
  (func $other (type $other) local.get 0)
  (func $pause (local $n i32)
    i32.const 200 local.set $n
    loop $next
      local.get $n i32.const 1 i32.sub local.tee $n br_if $next
    end)
  (func (export "_start")
    (local $ref (ref null $value))
    (local $strict (ref $value))
    call $pause
    ref.func $value local.set $ref
    ref.func $value local.set $strict
    i32.const 1 global.set $stage call $pause
    local.get $ref call_ref $value drop
    local.get $strict call_ref $value drop
    i32.const 2 global.set $stage
    global.get $nonnull call $pause call_ref $value drop))
