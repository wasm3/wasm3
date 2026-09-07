;; The deterministic profile replaces the host's answers about time and entropy with
;; the runtime's own, so that the same module produces the same run wherever it is
;; run - see d_m3DeterministicProfile.
;; This asserts every part of that at once:
;;
;;   - the clock moves by exactly one tick per reading, and by exactly what a wait
;;     asked for - so a one second sleep shows up as one second even though the run
;;     takes no time at all. A host clock could not land on the number exactly.
;;   - the wall clock is that same count offset to a fixed 2020 epoch, and the
;;     monotonic one is not: only where they are counted from tells them apart.
;;   - poll_oneoff still reports the event it was asked about.
;;   - random_get produces the pinned stream. The two values below are the first
;;     sixteen bytes of SplitMix64 from the fixed seed; they are written down here
;;     because replaying a run means getting the same bytes back, which makes them
;;     part of the contract rather than an implementation detail.
;;
;; Memory layout:
;;   0    the subscription    (48 bytes, preview1)
;;   64   the event           (32 bytes)
;;   128  nevents             (i32)
;;   136  a clock reading     (i64)
;;   144  a second one        (i64)
;;   152  random bytes        (16)
;;   168  a wall-clock reading (i64)
(module
  (import "wasi_snapshot_preview1" "poll_oneoff"
    (func $poll (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "clock_time_get"
    (func $clock (param i32 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "random_get"
    (func $random (param i32 i32) (result i32)))

  (memory (export "memory") 1)

  (global $wait i64 (i64.const 1000000000))   ;; 1s, which passes instantly
  (global $tick i64 (i64.const 1000000))      ;; what one reading of the clock costs
  (global $epoch i64 (i64.const 1577836800000000000))   ;; 2020-01-01T00:00:00Z

  ;; Each failure has a number of its own, so a red run says which part gave way
  (func (export "to_test") (result i32)
    ;; subscription: userdata = 42, type = clock, monotonic, relative $wait
    (i64.store   (i32.const 0)  (i64.const 42))
    (i32.store8  (i32.const 8)  (i32.const 0))
    (i32.store   (i32.const 16) (i32.const 1))
    (i64.store   (i32.const 24) (global.get $wait))
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

    (if (i64.ne (i64.load (i32.const 64)) (i64.const 42))
      (then (return (i32.const 5))))

    ;; the first reading, the wait, and nothing else
    (if (i64.ne (i64.sub (i64.load (i32.const 144)) (i64.load (i32.const 136)))
                (i64.add (global.get $wait) (global.get $tick)))
      (then (return (i32.const 6))))

    (if (call $random (i32.const 152) (i32.const 16))
      (then (return (i32.const 7))))

    (if (i64.ne (i64.load (i32.const 152)) (i64.const 0x6e789e6aa1b965f4))
      (then (return (i32.const 8))))
    (if (i64.ne (i64.load (i32.const 160)) (i64.const 0x06c45d188009454f))
      (then (return (i32.const 9))))

    (if (call $clock (i32.const 0) (i64.const 0) (i32.const 168))
      (then (return (i32.const 10))))

    ;; the wall clock counts from the epoch, the monotonic one from zero
    (if (i64.lt_u (i64.load (i32.const 168)) (global.get $epoch))
      (then (return (i32.const 11))))
    (if (i64.ge_u (i64.load (i32.const 144)) (global.get $epoch))
      (then (return (i32.const 12))))

    (i32.const 0)
  )
)
