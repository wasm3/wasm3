;; A tail-call chain. Each return_call replaces its caller, so the frames are
;; `run`, waiting on the call it made, and $spin, which stands where $a was
;; called: neither $a nor $b is anywhere, whether or not an engine reuses the
;; frame.
;;
;; @pause interrupt
;; @frame root 3 call : | i64
;; @frame root 0 back-edge loop@+0x3 : i32 i32 |
(module
  (func $spin (param i32) (result i32) (local $i i32)
    (loop $l
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get 0))))
    (local.get $i))
  (func $b (param i32) (result i32)
    (return_call $spin (i32.add (local.get 0) (i32.const 1))))
  (func $a (param i32) (result i32)
    (return_call $b (i32.add (local.get 0) (i32.const 1))))
  (func (export "run")
    (i64.const 5)
    (call $a (i32.const 1))
    (drop)
    (drop)))
