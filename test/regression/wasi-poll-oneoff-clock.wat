;; poll_oneoff with a single relative clock subscription, which is what a guest's
;; sleep() comes down to. Asserts three things at once: the call reports success, it
;; reports exactly the one event and reports it back with the userdata and the type
;; the subscription carried, and it really waited - the clock has to have moved by
;; the timeout the subscription named, less the tick $slack allows for.
;;
;; Memory layout:
;;   0    the subscription      (48 bytes, preview1)
;;   64   the event it writes   (32 bytes)
;;   128  nevents               (i32)
;;   136  a clock reading       (i64)
;;   144  a second one          (i64)
(module
  (import "wasi_snapshot_preview1" "poll_oneoff"
    (func $poll (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "clock_time_get"
    (func $clock (param i32 i64 i32) (result i32)))

  (memory (export "memory") 1)

  ;; 20ms. Long enough that a clock with millisecond resolution can see it, short
  ;; enough not to be felt in a test run.
  (global $timeout i64 (i64.const 20000000))

  ;; How much shorter than the timeout the wait is allowed to come out. A host whose
  ;; timer counts whole milliseconds cannot place a deadline any finer than one, so
  ;; the wait can end just under a millisecond before the clock says the timeout is
  ;; up - uvwasi is one such host, its deadline being libuv's loop time, which is
  ;; milliseconds truncated from the very nanosecond clock clock_time_get reports.
  ;; Two milliseconds is that bound with room to spare, and still nowhere near a wait
  ;; that did not happen: what this catches is a poll_oneoff that returns at once, or
  ;; one that waits on a clock the guest cannot then measure against.
  (global $slack i64 (i64.const 2000000))

  ;; Each failure has a number of its own, so a red run says which part gave way
  (func (export "to_test") (result i32)
    ;; subscription: userdata = 42, type = clock, clock id = monotonic,
    ;; timeout = $timeout, precision = 0, flags = 0 (relative)
    (i64.store   (i32.const 0)  (i64.const 42))
    (i32.store8  (i32.const 8)  (i32.const 0))
    (i32.store   (i32.const 16) (i32.const 1))
    (i64.store   (i32.const 24) (global.get $timeout))
    (i64.store   (i32.const 32) (i64.const 0))
    (i32.store16 (i32.const 40) (i32.const 0))

    (if (call $clock (i32.const 1) (i64.const 0) (i32.const 136))
      (then (return (i32.const 1))))

    (if (call $poll (i32.const 0) (i32.const 64) (i32.const 1) (i32.const 128))
      (then (return (i32.const 2))))

    (if (call $clock (i32.const 1) (i64.const 0) (i32.const 144))
      (then (return (i32.const 3))))

    (if (i32.ne (i32.load (i32.const 128)) (i32.const 1))
      (then (return (i32.const 4))))

    ;; the event: userdata @0, error @8 (i16), type @10 (i8)
    (if (i64.ne (i64.load (i32.const 64)) (i64.const 42))
      (then (return (i32.const 5))))
    (if (i32.load16_u (i32.const 72))
      (then (return (i32.const 6))))
    (if (i32.load8_u (i32.const 74))
      (then (return (i32.const 7))))

    (if (i64.lt_u (i64.sub (i64.load (i32.const 144)) (i64.load (i32.const 136)))
                  (i64.sub (global.get $timeout) (global.get $slack)))
      (then (return (i32.const 8))))

    (i32.const 0)
  )
)
