;; A function-entry pause. The frame holds the arguments, each declared local at
;; its default, and an empty stack; its offset is the first instruction's, just
;; past the local declarations. The caller waits at the call with the f64 it
;; pushed first, its argument consumed.
;;
;; @pause interrupt
;; @frame root 1 call : | f64
;; @frame root 0 entry : i32 i64 f32 |
(module
  (func $f (param i32) (local i64 f32)
    (drop (local.get 0)))
  (func (export "run")
    (f64.const 1)
    (call $f (i32.const 7))
    (drop)))
