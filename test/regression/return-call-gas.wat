;; return_call branches away and terminates a straight-line execution segment.
;; Under gas metering, each tail-call lap must be metered and bounded by the gas budget.
(module
  (func $spin (export "to_test")
    (return_call $spin)))
