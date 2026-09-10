;; poll_oneoff over a descriptor and a clock at once, which is the shape a guest's
;; select() takes. stdout is always ready to be written, so the call has to come back
;; on that and not on the five-second deadline sitting beside it: exactly one event,
;; the descriptor's, and no five seconds gone.
;;
;; Memory layout:
;;   0    subscription 0, the descriptor  (48 bytes, preview1)
;;   48   subscription 1, the clock       (48 bytes)
;;   128  the events it writes            (2 x 32 bytes)
;;   256  nevents                         (i32)
;;   264  a clock reading                 (i64)
;;   272  a second one                    (i64)
(module
  (import "wasi_snapshot_preview1" "poll_oneoff"
    (func $poll (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "clock_time_get"
    (func $clock (param i32 i64 i32) (result i32)))

  (memory (export "memory") 1)

  (global $deadline i64 (i64.const 5000000000))   ;; 5s, which must not be reached
  (global $allowed  i64 (i64.const 1000000000))   ;; 1s, which the call must beat

  ;; Each failure has a number of its own, so a red run says which part gave way
  (func (export "to_test") (result i32)
    ;; subscription 0: userdata = 7, type = fd_write, fd = 1
    (i64.store   (i32.const 0)  (i64.const 7))
    (i32.store8  (i32.const 8)  (i32.const 2))
    (i32.store   (i32.const 16) (i32.const 1))

    ;; subscription 1: userdata = 9, type = clock, monotonic, relative $deadline
    (i64.store   (i32.const 48) (i64.const 9))
    (i32.store8  (i32.const 56) (i32.const 0))
    (i32.store   (i32.const 64) (i32.const 1))
    (i64.store   (i32.const 72) (global.get $deadline))
    (i64.store   (i32.const 80) (i64.const 0))
    (i32.store16 (i32.const 88) (i32.const 0))

    (if (call $clock (i32.const 1) (i64.const 0) (i32.const 264))
      (then (return (i32.const 1))))

    (if (call $poll (i32.const 0) (i32.const 128) (i32.const 2) (i32.const 256))
      (then (return (i32.const 2))))

    (if (call $clock (i32.const 1) (i64.const 0) (i32.const 272))
      (then (return (i32.const 3))))

    ;; the clock had five seconds to run and must not have been one of them
    (if (i32.ne (i32.load (i32.const 256)) (i32.const 1))
      (then (return (i32.const 4))))

    (if (i64.ne (i64.load (i32.const 128)) (i64.const 7))
      (then (return (i32.const 5))))
    (if (i32.load16_u (i32.const 136))
      (then (return (i32.const 6))))
    (if (i32.ne (i32.load8_u (i32.const 138)) (i32.const 2))
      (then (return (i32.const 7))))

    (if (i64.ge_u (i64.sub (i64.load (i32.const 272)) (i64.load (i32.const 264)))
                  (global.get $allowed))
      (then (return (i32.const 8))))

    (i32.const 0)
  )
)
