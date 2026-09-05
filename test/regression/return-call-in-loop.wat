;; op_Loop claims a native frame, and a return_call walks away from it instead of
;; unwinding: the callee takes over the caller's m3 frame, so the loop that issued
;; the call never comes back around to give its native frame back. Each lap leaves
;; one more standing while the m3 stack stays flat, so nothing else can notice.
;; Expects a stack overflow trap rather than a native stack overflow.
(module
  (func $spin (export "to_test")
    (loop $L
      (return_call $spin))))
