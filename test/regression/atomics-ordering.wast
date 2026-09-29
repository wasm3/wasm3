;; The acquire-release atomics proposal gives every atomic access an optional ordering:
;; bit 4 of the memarg flags announces a byte after the offset, and the byte that
;; atomic.fence reserved as zero now says which fence it is. There is no text form for
;; it in the assembler here, so the module spells out its own bytes.
;;
;;   flags 0x12  = alignment 2 (natural for 32 bits) | 0x10, an ordering follows
;;   ordering    0x00 seqcst, 0x01 acqrel; a read-modify-write names one for each half
;;               (low nibble the read, high nibble the write), which must agree:
;;               0x00 or 0x11
;;
;; Only invalid orderings are refused, and each function is checked when it is first
;; called, so one module holds every case. The valid ones are:
;;
;;   to_test              stores 5 acqrel, fences, pauses, adds 3 acqrel, loads acqrel: 8
;;   rmw_seqcst_explicit  a read-modify-write that names seqcst rather than leaving it out
;;
;; and the refused ones an ordering nobody defined, one that disagrees between the
;; halves of a read-modify-write, one on notify, which is not an access that orders,
;; a fence that is neither kind, the bit on an instruction that is not atomic, and an
;; alignment so large that shifting by it would mean nothing.

;; It sits inside assert_malformed for the same reason fc-subopcode-toolong does: the
;; assembler's own decoder predates the proposal and refuses a fence that is not zero,
;; and only there does wast2json write out a module it cannot read back. What it is
;; asserted to be does not matter; the case takes the module and runs it.
(assert_malformed
(module binary
  "\00asm" "\01\00\00\00"                      ;; magic, version

  "\01\05\01" "\60\00\01\7f"                   ;; type:   () -> i32
  "\03\09\08" "\00\00\00\00\00\00\00\00"      ;; func:   eight functions of type 0
  "\05\03\01" "\00\01"                         ;; memory: one page, unshared

  "\07\95\01\08"                               ;; export: eight
    "\07to_test\00\00"
    "\11bad_load_ordering\00\01"
    "\10bad_rmw_ordering\00\02"
    "\13bad_notify_ordering\00\03"
    "\09bad_fence\00\04"
    "\16ordering_on_plain_load\00\05"
    "\13rmw_seqcst_explicit\00\06"
    "\0fbad_align_shift\00\07"

  "\0a\6e\08"                                  ;; code:   eight bodies

    "\21\00"                                   ;; to_test, 32 bytes, no locals
      "\41\00\41\05\fe\17\12\00\01"            ;;   i32.atomic.store acqrel (0, 5)
      "\fe\03\01"                              ;;   atomic.fence acqrel
      "\fe\04"                                 ;;   pause
      "\41\00\41\03\fe\1e\12\00\11\1a"         ;;   i32.atomic.rmw.add acqrel (0, 3), dropped
      "\41\00\fe\10\12\00\01"                  ;;   i32.atomic.load acqrel (0)
      "\0b"

    "\09\00" "\41\00\fe\10\12\00\02\0b"        ;; bad_load_ordering: a load naming 0x02
    "\0e\00" "\41\00\41\01\fe\1e\12\00\01\1a\41\00\0b"
                                               ;; bad_rmw_ordering: a read-modify-write naming 0x01, not 0x11
    "\0b\00" "\41\00\41\00\fe\00\12\00\01\0b"  ;; bad_notify_ordering: memory.atomic.notify naming acqrel
    "\07\00" "\fe\03\02\41\00\0b"              ;; bad_fence: a fence that is 0x02
    "\08\00" "\41\00\28\12\00\01\0b"           ;; ordering_on_plain_load: i32.load with the bit set
    "\0b\00" "\41\00\41\01\fe\1e\12\00\00\0b"  ;; rmw_seqcst_explicit: i32.atomic.rmw.add seqcst (0, 1)
    "\08\00" "\41\00\fe\12\20\00\0b"           ;; bad_align_shift: a byte load whose alignment is 32
)
"atomic.fence consistency model must be 0"
)
