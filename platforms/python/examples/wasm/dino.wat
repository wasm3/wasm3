;; Import Math.random from JavaScript.
(import "Math" "random" (func $random (result f32)))

;; *** Memory map ***
;; 0x0                Input: 1=up, 2=down, 3=up+down
;; [0x4, 0x5000)      See below
;; [0x5000, 0x1c000)  Color[300*75]  canvas
(memory (export "mem") 2)

;; Some float constants to reduce size.
(global $f0 f32 (f32.const 0))
(global $f50 f32 (f32.const 50))

;; A timer used for animation.
(global $timer (mut i32) (i32.const 0))

;; The current score (only increments while playing).
(global $score (mut i32) (i32.const 0))

;; The current game state (see the main br_table below)
;;   0 => initial state
;;   1 => dino is running
;;   2 => dino is rising (first part of jump)
;;   3 => dino is falling (second part of jump)
;;   4 => dino is dead
(global $game_state (mut i32) (i32.const 0))

;; The dino's y velocity. Negative is going up, positive is going down.
(global $dino_jump_vy (mut f32) (f32.const 0))

;; The camera's scrolling speed. In actuality there is no camera, this speed is
;; instead used to move all game objects. Each object multiplies its movement
;; speed by this global $speed to provide a parallax effect.
(global $speed (mut f32) (f32.const -0.5))

(data (i32.const 40)
  ;; *** Object Table ***
  ;; addr:4  size:(#14 * 9 => 126 bytes)
  ;;
  ;; The first 4 objects (3 obstacles and dino) must be reset every game, so we
  ;; store the data just once and initialize it in the "init" game state.
  ;;
  ;;   struct {
  ;;     i8 kind;   // index to the "Kind table" below.
  ;;     f32 y, x;  // y and x coordinate of the object, in pixels.
  ;;   }
  ;;
  ;;  kind         y   x
  ;;(i8 11)  (f32 50  22)   ;; dino
  ;; (i8 1)  (f32 55 300)  ;; obstacles x 3
  ;; (i8 1)  (f32 55 600)
  ;; (i8 1)  (f32 55 900)
     (i8 7)  (f32 67   0)  ;; ground x 6
     (i8 7)  (f32 67  64)
     (i8 7)  (f32 67 128)
     (i8 7)  (f32 67 192)
     (i8 7)  (f32 67 256)
     (i8 7)  (f32 67 320)
     (i8 6)  (f32 40   0)  ;; clouds x 4
     (i8 6)  (f32 40 128)
     (i8 6)  (f32 40 256)
     (i8 6)  (f32 40 384)

  ;; *** Kind Table ***
  ;; addr:130  size:(#12 * 7 = 84 bytes)
  ;;
  ;; The kind table contains all information about an object that is not
  ;; specific to that individual object.
  ;;
  ;;   struct {
  ;;     i8 type;       // Random index (see "Random Table" below)
  ;;     i8 anim;       // Animation index (see "Animation Table" below)
  ;;     i8 img;        // Image offset (not index) (see "Image Table" below)
  ;;     i8 random_8x;  // Add at most 8x this to x-coord for new random object
  ;;     i8 start_y;    // The starting y coordinate for new random object
  ;;     i8 random_y;   // Add at most this to y-coord for new random object
  ;;     i8 speed_4dx;  // Add 4x this value * $speed to object when moving
  ;;   }
  ;;
  ;;type anim  img  +8x  y +y *4dx
  (i8  0    0   18   75 46  0   4)  ;;  0 cactus1
  (i8  0    0   21   75 54  0   4)  ;;  1 cactus2
  (i8  0    0   24   75 54  0   4)  ;;  2 cactus3
  (i8  0    0   27   75 54  0   4)  ;;  3 cactus4
  (i8  0    0   30   75 46  0   4)  ;;  4 cactus5
  (i8  0    3   39   75 25 25   5)  ;;  5 bird
  (i8  1    0   33   30 15 25   1)  ;;  6 cloud
  (i8  2    0   36    0 67  0   4)  ;;  7 ground
  (i8  3    0    3    0  0  0   0)  ;;  8 dino stand
  (i8  3    1    6    0  0  0   0)  ;;  9 dino run
  (i8  3    2   12    0  0  0   0)  ;; 10 dino duck
  (i8  3    0    0    0  0  0   0)  ;; 11 dino dead

  ;; *** Random Table ***
  ;; addr:214  size:(#3 * 2 = 6 bytes)
  ;;
  ;; Each element of the random table produces a new "kind" given a current
  ;; kind's "type". This way an obstacle always creates a new obstacle, a
  ;; ground object creates a new ground object, and a cloud creates a new
  ;; cloud. The dino is never respawned, so it doesn't need an entry.
  ;;
  ;;   struct {
  ;;     i8 start;   // Minimum kind value for this type.
  ;;     i8 len;     // Number of kinds for this type.
  ;;   }
  ;;
  ;; start len
  (i8 0 6)
  (i8 6 1)
  (i8 7 1)

  ;; *** Animation Y Table ***
  ;; addr:220  size:(#4 * 4 = 16 bytes)
  ;;
  ;; An object has an "anim" value which index into a row of this table. Each
  ;; element is the value to add to the object's y-coordinate over time. This
  ;; allows us to move the dino's graphic down without chaning the dino y
  ;; coordinate, and to properly animate the bird so its head doesn't move when
  ;; its wings flap.
  ;;
  ;;  time ->
  (i8 0 0 0 0)  ;; 0 none
  (i8 0 0 0 0)  ;; 1 run
  (i8 9 9 9 9)  ;; 2 duck
  (i8 0 0 3 3)  ;; 3 bird

  ;; *** Animation Image Table ***
  ;; addr:236  size:(#4 * 4 = 16 bytes)
  ;;
  ;; An object has a "anim" value which index into a row of this table. Each
  ;; element is the value to add to the object's img offset over time. This
  ;; animates the dino's legs and the bird's wings.
  ;;
  ;;  time ->
  (i8 0  0  0  0)  ;; 0 none
  (i8 0  3  0  3)  ;; 1 run
  (i8 0  3  0  3)  ;; 2 duck
  (i8 0  0  3  3)  ;; 3 bird

  ;; *** Image Table ***
  ;; addr:252  size:(#15 * 3 = 45 bytes)
  ;;
  ;; Each image is a pair of the whcol (width/height/color) data, and an offset
  ;; to the actual graphics data after compression.
  ;;
  ;; struct {
  ;;   i8 whcol;   // Width/Height/Color offset (see "WHcol Table" below)
  ;;   i16 data;   // Offset of image data.
  ;; }
  ;;
  ;; whcol     data
  (i8  0) (i16    0)  ;; dead     = 0
  (i8  0) (i16  440)  ;; stand    = 3
  (i8  0) (i16  880)  ;; run1     = 6
  (i8  0) (i16 1320)  ;; run2     = 9
  (i8  3) (i16 1760)  ;; duck1    = 12
  (i8  3) (i16 2124)  ;; duck2    = 15
  (i8  6) (i16 2488)  ;; cactus1  = 18
  (i8  9) (i16 2826)  ;; cactus2  = 21
  (i8 12) (i16 3168)  ;; cactus3  = 24
  (i8 15) (i16 3672)  ;; cactus4  = 27
  (i8 18) (i16 3834)  ;; cactus5  = 30
  (i8 21) (i16 4874)  ;; cloud    = 33
  (i8 24) (i16 5082)  ;; ground   = 36
  (i8 27) (i16 5402)  ;; bird1    = 39
  (i8 30) (i16 5724)  ;; bird2    = 42

  ;; *** WHCol Table (Width/Height/Color) ***
  ;; addr:297 size:(#13 * 3 = 39 bytes)
  ;;
  ;; The WHcol table stores a width and height in pixels, and a color. Some of
  ;; these values are shared by multiple images, which can save a few bytes.
  ;; The color stores the alpha of a black pixel. Since the background is drawn
  ;; in white, this produces various shades of gray. Collision only occurs w/
  ;; color 172 (the obstacles).
  ;;
  ;;  struct {
  ;;    i8 width;    // Width in pixels.
  ;;    i8 height;   // Height in pixels.
  ;;    i8 alpha;    // Alpha value of a black pixel, e.g. AA000000.
  ;;  }
  ;;
  ;;   w  h col
  (i8 20 22 171)  ;;  0 dead,stand,run1,run2
  (i8 28 13 171)  ;;  3 duck1,duck2
  (i8 13 26 172)  ;;  6 cactus1
  (i8 19 18 172)  ;;  9 cactus2
  (i8 28 18 172)  ;; 12 cactus3
  (i8  9 18 172)  ;; 15 cactus4
  (i8 40 26 172)  ;; 18 cactus5
  (i8 26  8  37)  ;; 21 cloud
  (i8 64  5 171)  ;; 24 ground
  (i8 23 14 172)  ;; 27 bird1
  (i8 23 16 172)  ;; 30 bird2
  (i8  3  5 172)  ;; 33 digits
  (i8 50  8 172)  ;; 36 gameover

  ;; *** Compressed Graphics Data ***
  ;; addr:336 size:491 bytes
  ;;
  ;; The original graphics data are 13 images stored as 1 bit-per-pixel.
  ;;
  ;;   0, 1, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, ...
  ;;
  ;; To compress the data, first the bits from all images are grouped. Next
  ;; the bits are grouped into alternating runs of 0 and 1, starting with 0.
  ;; The maximum run length is 15, so longer runs can be marked as 15, 0, N.
  ;;
  ;;   1, 1, 3, 4, 4, 3, 4, ...
  ;;
  ;; Then repeated patterns are found and marked with a backward reference and
  ;; length (shown as (-dist, len) below). If the length is longer than the
  ;; distance, then the values will be repeated, e.g. 1, (-1, 4) is equivalent
  ;; to 1, 1, 1, 1, 1.
  ;;
  ;;   1, 1, 3, 4, 4, (-3, 2), ...
  ;;
  ;; Now the regions of literal values and back-references are grouped so they
  ;; alternate. The literal values are prefixed by a count. (The count is shown
  ;; as #n below). As before, if there are two adjacent back-references then
  ;; they must be separated by a count of 0, e.g. (-2, 5), [#0], (-6, 3).
  ;;
  ;;   [#5, 1, 1, 3, 4, 4], (-3, 2), ...
  ;;
  ;; These values are then encoded as bits, where the count, distance and
  ;; length are 7 bits, and the literal values are 4 bits. The distance is
  ;; always stored as a positive number.
  ;;
  ;;   [#0000101, 0001, 0001, 0011, 0100, 0100], (0000011, 0000010), ...
  ;;
  ;; Finally these bits are stored, little-endian. The final byte is
  ;; zero-extended.
  ;;
  ;;   10000101 10001000 00100001 00011010 00001000 00000000
  ;;
  ;;     0x85     0x44     0x21     0x1a     0x08     0x00
  ;;
  ;; When data is decompressed, the process is reversed. The decompression is
  ;; separated into two passes. First the literals and back-references are
  ;; written to one region (see "First Pass Decompressed output" below). Then
  ;; this region is scanned and the 1 bit-per-pixel data is written (see "Final
  ;; Decompressed Output" below). The final data is written as one-byte-per
  ;; pixel, so it's easier to blit to the screen.

  "\91\c5\95\29\95\88\28\95\29\0d\18\72\28\81\65\71\66\42\4a\23\19\41\6e\7e"
  "\9c\ab\c9\e7\13\e2\32\e1\41\e1\32\f2\40\b8\aa\12\07\3c\08\ea\85\e3\87\0c"
  "\cb\c3\c4\03\c9\43\02\61\48\11\79\91\78\a0\78\20\c1\78\18\8c\a1\94\87\8f"
  "\8b\07\96\87\9d\07\a5\07\52\96\43\9b\78\98\90\19\79\10\09\79\d8\d4\21\20"
  "\40\15\62\64\42\1e\c4\b7\28\e8\64\a1\20\91\90\90\90\0a\0b\82\a6\f0\b1\83"
  "\32\05\40\60\e8\9c\a0\dc\dc\d8\48\84\c4\44\d0\84\84\c4\c4\88\02\21\a0\50"
  "\60\2a\16\14\26\15\14\55\7c\25\75\63\43\d0\08\4e\0c\08\80\d1\30\20\09\46"
  "\63\10\32\60\44\41\42\42\e0\0b\00\04\15\30\c7\c1\55\41\51\22\31\21\d2\24"
  "\31\21\44\52\64\21\24\36\85\24\36\57\25\36\37\67\a8\00\04\86\41\50\10\20"
  "\07\8c\0a\a3\12\21\2a\36\36\29\0f\2a\4a\19\4d\49\38\6c\58\2c\70\82\92\70"
  "\42\4a\d1\38\54\0c\89\90\e3\30\90\12\c7\87\10\12\e6\68\14\6a\42\d0\91\28"
  "\0c\c9\9c\83\21\20\73\28\18\21\21\21\43\22\c1\90\05\01\63\c5\a1\28\1c\59"
  "\99\0a\41\48\b2\62\28\08\38\81\08\c3\42\8c\28\09\87\c4\10\a1\d4\db\03\49"
  "\c5\83\c4\c9\8b\85\0c\92\4b\c5\43\05\c5\83\49\c4\3c\08\10\81\4c\20\18\15"
  "\0f\1b\0f\1f\2a\1f\45\71\84\23\e3\21\e5\c1\e6\a1\e8\21\44\c8\47\8a\2b\4c"
  "\2d\0e\df\d5\1f\02\99\7b\f3\2f\e5\a1\e6\81\ea\41\ec\21\40\09\e0\7a\2e\6b"
  "\d3\03\cd\43\c9\83\c9\83\85\53\48\48\98\84\84\44\98\94\98\90\54\48\90\84"
  "\94\8c\a0\50\1c\c8\20\92\8a\0a\0c\92\90\08\9a\19\11\11\9a\11\09\09\0a\0a"
  "\03\1d\04\15\12\83\e0\24\67\20\60\08\0d\0f\87\90\91\a0\12\19\06\12\24\3a"
  "\00\80\06\82\c4\84\86\02\40\22\00\82\03\83\24\24\2a\64\24\10\b3\38\90\94"
  "\8c\90\ef\d1\90\a0\19\8a\a1\90\10"
  ;; A few extra bytes to bump up to 2020 :-)
  "\00\00\00"

  ;; *** First Pass Decompressed Output ***
  ;; addr:827  size:1490

  ;; *** Final Decompressed Output ***
  ;; addr:2317  size:6640  end=8957

  ;; Most graphic addresses are used above in the Image Table, but a few
  ;; additional graphics are used in the code below:
  ;;
  ;; Digits 0-9, 15 bytes each.  addr:6092
  ;; GameOver                    addr:6242
)

(data (i32.const 0x4fdb)
  ;; Starting obstacles + dino
  ;; id        x   y
  (i8 9) (f32 50 22)   ;; dino
  (i8 1) (f32 55 300)  ;; obstacle1
  (i8 1) (f32 55 600)  ;; obstacle2
  (i8 1) (f32 55 900)  ;; obstacle3

  ;; Byte to replicate when clearing the screen.
  (i8 0xff)
)

;; Copy len bytes from $src to $dst. We can emulate a fill if the regions
;; overlap. Always copies at least one byte!
(func $memcpy (param $dst i32) (param $src i32) (param $dstend i32)
  (loop $copy
    (i32.store8 (local.get $dst) (i32.load8_u (local.get $src)))
    (local.set $src (i32.add (local.get $src) (i32.const 1)))
    (br_if $copy
      (i32.lt_u
        (local.tee $dst (i32.add (local.get $dst) (i32.const 1)))
        (local.get $dstend))))
)

;; The main decompression routine, which runs when the module is loaded (in the
;; start function).
(start $decompress)
(func $decompress
  (local $data_left i32)    ;; rest of currently read byte
  (local $bits_left i32)    ;; number of bits available
  (local $bits_to_read i32) ;; number of bits to read
  (local $read_data i32)    ;; currently read value
  (local $lit_count i32)    ;; number of 4-bit literals to read
  (local $ref_dist i32)     ;; backreference distance
  (local $src i32)
  (local $dst i32)
  (local $temp_dst i32)
  (local $state i32)        ;; see below

  (local $run_count i32)    ;; number of bits in this run
  (local $run_byte i32)     ;; byte to write

  (local.set $bits_to_read (i32.const 7))
  (local.set $dst (i32.const 827))

  ;; First pass, decode back-references.
  (loop $loop
    ;; Read in new bits when the number of bits left is less than 16. This
    ;; works because we never read more than 7 bits.
    (if
      (i32.lt_u (local.get $bits_left) (i32.const 16))
      (then
        ;; Read 16 bits into the top of $data_left
        (local.set $data_left
          (i32.or
            (local.get $data_left)
            (i32.shl
              (i32.load16_u offset=336 (local.get $src))
              (local.get $bits_left))))
        ;; Add 16 bits to count
        (local.set $bits_left
          (i32.add (local.get $bits_left) (i32.const 16)))
        ;; Increment the src pointer
        (local.set $src (i32.add (local.get $src) (i32.const 2)))))

    ;; Save bits that were read (masked)
    (local.set $read_data
      (i32.and (local.get $data_left)
        (i32.sub
          (i32.shl (i32.const 1) (local.get $bits_to_read))
          (i32.const 1))))
    ;; Remove bits that were read from $data_left
    (local.set $data_left
      (i32.shr_u (local.get $data_left) (local.get $bits_to_read)))
    ;; Reduce the number of $bits_left
    (local.set $bits_left
      (i32.sub (local.get $bits_left) (local.get $bits_to_read)))

    block $read_backref_dist
    block $read_backref_len
    block $goto2
    block $read_literal
    block $read_lit_count
      (br_table $read_lit_count $read_literal $read_backref_dist $read_backref_len
        (local.get $state))

    ;; 0: read literal count (7 bits)
    end $read_lit_count
      ;; skip if count is 0
      (br_if $goto2 (i32.eqz (local.tee $lit_count (local.get $read_data))))
      (local.set $state (i32.const 1))
      (local.set $bits_to_read (i32.const 4))
      (br $loop)

    ;; 1: read literal (4 bits)
    end $read_literal
      ;; Write literal and increment $dst, as long as $lit_count is > 0.
      (i32.store8 (local.get $dst) (local.get $read_data))
      (local.set $dst (i32.add (local.get $dst) (i32.const 1)))
      (br_if $loop
        (local.tee $lit_count (i32.sub (local.get $lit_count) (i32.const 1))))
      ;; fallthrough

    end $goto2
      (local.set $state (i32.const 2))
      (local.set $bits_to_read (i32.const 7))
      (br $loop)

    ;; 3: read backreference length
    end $read_backref_len
      ;; $memcpy always copies at least one byte. That may happen here but it's
      ;; OK because we'll not advance $dst, so the value we copied will be
      ;; overwritten.
      (call $memcpy
        (local.get $dst)
        (i32.sub (local.get $dst) (local.get $ref_dist))
        (local.tee $dst (i32.add (local.get $dst) (local.get $read_data))))

      (local.set $state (i32.const 0))
      (br $loop)

    ;; 2: read backreference distance
    end $read_backref_dist
      ;; Check for end, since compressed data ends with literals.
      (local.set $ref_dist (local.get $read_data))
      (local.set $state (i32.const 3))
      (br_if $loop (i32.lt_u (local.get $src) (i32.const 494)))
      ;; fall out of loop
  )

  ;; Second pass, decode 1bpp runs
  ;;
  ;; We know the values of $src and $dst here, since they will have been set by
  ;; the loop above:
  ;;
  ;;   $src == 494
  ;;   $dst == 2317
  ;;
  ;; We want to read from 827, so we can use a load offset which is the
  ;; difference between the two: 827-494 = 333. Similarly, we want to end
  ;; reading at 2317, so we can instead stop when $src is 2317-333 = 1984.
  ;;
  (loop $loop
    (if (local.tee $run_count (i32.load8_u offset=333 (local.get $src)))
      (then
        ;; Set first byte.
        (i32.store8 (local.get $dst) (local.get $run_byte))
        ;; Then replicate with memcpy.
        (call $memcpy
          (i32.add (local.get $dst) (i32.const 1))
          (local.get $dst)
          (local.tee $dst (i32.add (local.get $dst) (local.get $run_count))))))

    ;; Flip the written byte between 0 and 1.
    (local.set $run_byte (i32.eqz (local.get $run_byte)))
    (br_if $loop
      (i32.lt_u
        (local.tee $src (i32.add (local.get $src) (i32.const 1)))
        (i32.const 1984))))
)

;; Blit an 8bpp image to the screen at ($x, $y). The $whcol_offset and
;; $src_offset values are the same ones as are stored in the Image Table above.
;; They're supplied as parameters here so the Digits and GameOver images can
;; provide them directly without having to add extra entries for each of them
;; in the Image Table.
(func $blit
      (param $x i32) (param $y i32)
      (param $whcol_offset i32)
      (param $src_offset i32) (result i32)
  (local $w i32)             ;; destination width of the sprite (maybe clipped)
  (local $h i32)             ;; height of the sprite (never clipped)
  (local $color i32)         ;; full 32-bit ARGB color
  (local $dst_offset i32)    ;; destination offset, updated per-row
  (local $dst_addr i32)      ;; destination address, calculated per-pixel
  (local $src_stride i32)    ;; the original width of the sprite
  (local $ix i32)
  (local $hit i32)           ;; 0=no collision, 1=collision

  (local.set $src_stride
    (local.tee $w (i32.load8_u offset=297 (local.get $whcol_offset))))
  (local.set $h (i32.load8_u offset=298 (local.get $whcol_offset)))
  (local.set $color
    (i32.shl
      (i32.load8_u offset=299 (local.get $whcol_offset))
      (i32.const 24)))

  ;; if (x < 0)
  (if
    (i32.lt_s (local.get $x) (i32.const 0))
    (then
      ;; reduce width by x
      (local.set $w (i32.add (local.get $w) (local.get $x)))

      ;; advance src_addr by x
      (local.set $src_offset (i32.sub (local.get $src_offset) (local.get $x)))

      ;; set x to 0
      (local.set $x (i32.const 0)))
    (else
      ;; if (x + w > SCREEN_WIDTH)
      (if
        (i32.gt_s (i32.add (local.get $x) (local.get $w)) (i32.const 300))
        (then
          ;; w = SCREEN_WIDTH - x
          (local.set $w (i32.sub (i32.const 300) (local.get $x)))))))

  ;; if (w <= 0) { return 0; }
  (if (i32.gt_s (local.get $w) (i32.const 0))
    (then
      ;; dst_addr = y * SCREEN_WIDTH + x
      (local.set $dst_offset
        (i32.add (i32.mul (local.get $y) (i32.const 300)) (local.get $x)))

      (loop $yloop
        ;; ix = 0;
        (local.set $ix (i32.const 0))

        (loop $xloop
          ;; data = i8_mem[src_addr]
          (if
            (i32.load8_u offset=2317
              (i32.add (local.get $src_offset) (local.get $ix)))
            (then
              ;; get alpha value of previous pixel. If it is 172, then it is an
              ;; obstacle.
              (local.set $hit
                (i32.or
                  (local.get $hit)
                  (i32.eq
                    (i32.load8_u offset=0x5003
                      (local.tee $dst_addr
                        (i32.shl (i32.add (local.get $dst_offset)
                                          (local.get $ix))
                                 (i32.const 2))))
                    (i32.const 172))))
              ;; set new pixel
              (i32.store offset=0x5000 (local.get $dst_addr) (local.get $color))))

          ;; loop while (++ix < w)
          (br_if $xloop
            (i32.lt_s
              (local.tee $ix (i32.add (local.get $ix) (i32.const 1)))
              (local.get $w))))

        ;; dst_addr += SCREEN_WIDTH
        (local.set $dst_offset (i32.add (local.get $dst_offset) (i32.const 300)))
        ;; src_addr += src_stride;
        (local.set $src_offset (i32.add (local.get $src_offset) (local.get $src_stride)))

        ;; loop while (--h != 0)
        (br_if $yloop
          (local.tee $h (i32.sub (local.get $h) (i32.const 1)))))
      ))

  (local.get $hit)
)

(func (export "run")
  (local $input i32)        ;; button input read per frame
  (local $dino_id i32)      ;; new dino id to set, updated each frame
  (local $obj_addr i32)     ;; object address when drawing/moving
  (local $kind i32)         ;; kind of the current object
  (local $anim_offset i32)  ;; offset into either Animation Table
  (local $img_offset i32)   ;; offset into the Image Table
  (local $kind_offset i32)  ;; offset into the Kind Table
  (local $rand_offset i32)  ;; offset into the Random Table

  (local $score_x i32)
  (local $score i32)        ;; temporarily stored $score value

  (local $obj_x f32)        ;; x-coord of the current object
  (local $dino_y f32)       ;; new dino y, updated each frame

  ;; Clear the screen (the byte at 0x4fff is initialized to 0xff)
  (call $memcpy (i32.const 0x5000) (i32.const 0x4fff) (i32.const 0x1af90))

  ;; Animation timer
  (global.set $timer (i32.add (global.get $timer) (i32.const 1)))

  ;; Slowly increment the speed, up to -1. More negative speeds are faster.
  (global.set $speed
    (f32.max
      (f32.sub (global.get $speed) (f32.const 0.001953125))
      (f32.const -1)))

  (local.set $input (i32.load8_u (i32.const 0)))
  (local.set $dino_y (f32.load (i32.const 5)))

  block $done
  block $playing
  block $falling
  block $rising
  block $running
  block $init
  block $dead
    (br_table $init $running $rising $falling $dead (global.get $game_state))

  end $dead
    (local.set $dino_id (i32.const 11))
    (global.set $speed (global.get $f0))

    ;; Wait until button pressed.
    (br_if $done
      (i32.or
        (i32.eqz (local.get $input))
        ;; Wait at least 20 frames before restarting.
        (i32.le_u
          (i32.sub (global.get $timer) (global.get $score))
          (i32.const 20))))

    ;; only need to reset score, dino state, and obstacles.
    (global.set $score (i32.const 0))
    (global.set $timer (i32.const 0))
    (global.set $dino_jump_vy (global.get $f0))
    (global.set $speed (f32.const -0.5))

    ;; fallthrough
  end $init
    ;; init dino
    (local.set $dino_id (i32.const 9))   ;; dino running animation
    (local.set $dino_y (global.get $f50))

    ;; reset obstacles
    (call $memcpy (i32.const 4) (i32.const 0x4fdb) (i32.const 40))
    (global.set $game_state (i32.const 1)) ;; running state

    ;; fallthrough
  end $running
    ;; If down pressed, duck (id=10) else running (id=9)
    (local.set $dino_id
      (i32.add (i32.eq (local.get $input) (i32.const 2)) (i32.const 9)))

    ;; if up is not pressed, skip over jumping code
    (br_if $playing (i32.ne (local.get $input) (i32.const 1)))

    ;; start jumping.
    (global.set $game_state (i32.const 2))  ;; rising state
    (global.set $dino_jump_vy (f32.const -6))

    ;; fallthrough
  end $rising
    ;; Stop jumping if the button is released and we've reached the minimum
    ;; height, or we've reached the maximum height.
    (br_if $falling
      (i32.and
        (i32.or
          (i32.eq (local.get $input) (i32.const 1))
          (f32.ge (local.get $dino_y) (f32.const 30)))  ;; min height
        (f32.ge (local.get $dino_y) (f32.const 10))))   ;; max height

    ;; start falling.
    (global.set $game_state (i32.const 3))  ;; falling state
    (global.set $dino_jump_vy (f32.const -1))

    ;; fallthrough
  end $falling
    (local.set $dino_id (i32.const 8))
    (local.set $dino_y (f32.add (local.get $dino_y) (global.get $dino_jump_vy)))
    (global.set $dino_jump_vy (f32.add (global.get $dino_jump_vy) (f32.const 0.4)))

    ;; Stop falling if the ground is reached.
    (br_if $playing (f32.le (local.get $dino_y) (global.get $f50)))

    (global.set $game_state (i32.const 1))  ;; running state
    (local.set $dino_y (global.get $f50))
    (global.set $dino_jump_vy (global.get $f0))

    ;; fallthrough
  end $playing
    ;; Add 1 to the score
    (global.set $score (i32.add (global.get $score) (i32.const 1)))

    ;; fallthrough
  end $done

  ;; Update dino id and y-coordinate.
  (i32.store8 (i32.const 4) (local.get $dino_id))
  (f32.store (i32.const 5) (local.get $dino_y))

  ;; loop over objects backward, drawing and moving
  (local.set $obj_addr (i32.const 121))
  (loop $loop
    ;;; Draw and check for collision.
    (if
      (i32.and
        ;; $blit returns true if we collided
        (call $blit
          ;; x => obj.x
          (i32.trunc_f32_s (f32.load offset=5 (local.get $obj_addr)))
          ;; y => obj.y + AnimYTable[anim_offset]
          (i32.add
            (i32.trunc_f32_s (f32.load offset=1 (local.get $obj_addr)))
            (i32.load8_u offset=220
              ;; $anim_offset = KindTable[$kind_offset].anim + ((timer&15)>>2)
              (local.tee $anim_offset
                (i32.add
                  (i32.shl
                    (i32.load8_u offset=131
                      ;; $kind_offset = $obj_addr->kind * sizeof(Kind)
                      (local.tee $kind_offset
                        (i32.mul
                          (i32.load8_u (local.get $obj_addr))
                          (i32.const 7))))
                    (i32.const 2))
                  (i32.shr_u
                    (i32.and (global.get $timer) (i32.const 15))
                    (i32.const 2))))))
          ;; whcol_offset => ImageTable[$img_offset].whcol
          (i32.load8_u offset=252
            ;; $img_offset = KindTable[$kind_offset].img + AnimImgTable[$anim_offset]
            (local.tee $img_offset
              (i32.add
                (i32.load8_u offset=132 (local.get $kind_offset))
                (i32.load8_u offset=236 (local.get $anim_offset)))))
          ;; src_offset => ImageTable[$img_offset].data
          (i32.load16_u offset=253 (local.get $img_offset)))
         ;; ... if this object is a dino...
        (i32.eq (local.get $obj_addr) (i32.const 4)))
        (then
          ;; ... then set state to dead.
          (global.set $game_state (i32.const 4))))

    ;;; Move
    (if
      ;; If object goes off screen to the left...
      (f32.lt
        ;; $obj_x = $obj_addr->x + KindTable[$kind_offset].speed_4dx * $speed
        (local.tee $obj_x
          (f32.add
            (f32.load offset=5 (local.get $obj_addr))
            (f32.mul
              (f32.convert_i32_u (i32.load8_u offset=136 (local.get $kind_offset)))
              (global.get $speed))))
        (f32.const -64))
      (then
        ;; Write new object kind.
        (i32.store8
          (local.get $obj_addr)
          ;; Pick a random item.
          ;; $kind = RandTable[$rand_offset].start +
          ;;           (random() * RandTable[$rand_offset].len)
          (local.tee $kind
            (i32.add
              (i32.trunc_f32_s
                (f32.mul
                  (call $random)
                  (f32.convert_i32_u
                    ;; RandTable[$rand_offset].len
                    (i32.load8_u offset=215
                      ;; $rand_offset = KindTable[$kind_offset].type << 1
                      (local.tee $rand_offset
                        (i32.shl
                          (i32.load8_u offset=130 (local.get $kind_offset))
                          (i32.const 1)))))))
              ;; RandTable[$rand_offset].start
              (i32.load8_u offset=214 (local.get $rand_offset)))))

        ;; Set new object x (stored below).
        ;; $obj_x = SCREEN_WIDTH + 64 + (KindTable[$kind_offset].random_8x << 3)
        (local.set $obj_x
          (f32.add
            (f32.add
              (local.get $obj_x)
              (f32.const 384))    ;; SCREEN_WIDTH + 64
            (f32.convert_i32_u
              (i32.shl
                (i32.load8_u offset=133
                  (local.tee $kind_offset
                    (i32.mul (local.get $kind) (i32.const 7))))
                (i32.const 3)))))

        ;; Write new object y.
        ;; $obj_addr->y = KindTable[$kind_offset].start_y +
        ;;                  (random() * KindTable[$kind_offset].random_y)
        (f32.store offset=1
          (local.get $obj_addr)
          (f32.add
            (f32.convert_i32_u
              (i32.load8_u offset=134 (local.get $kind_offset)))
            (f32.mul
              (call $random)
              (f32.convert_i32_u
                (i32.load8_u offset=135 (local.get $kind_offset))))))))

    ;; Write object x coordinate.
    (f32.store offset=5 (local.get $obj_addr) (local.get $obj_x))

    ;; loop over all objects backward.
    (br_if $loop
      (i32.gt_s
        (local.tee $obj_addr (i32.sub (local.get $obj_addr) (i32.const 9)))
        (i32.const 0))))

  ;; draw score
  (local.set $score (global.get $score))
  (local.set $score_x (i32.const 300))
  (loop $loop
    (drop
      (call $blit
        ;; x
        (local.tee $score_x (i32.sub (local.get $score_x) (i32.const 4)))
        ;; y
        (i32.const 4)
        ;; whcol_offset
        (i32.const 33)
        ;; src_offset
        (i32.add
          (i32.const 6092)
          (i32.mul
            (i32.rem_u (local.get $score) (i32.const 10))
            (i32.const 15)))))
    (br_if $loop
      (local.tee $score (i32.div_u (local.get $score) (i32.const 10)))))

  ;; draw game over
  (if (i32.eq (global.get $game_state) (i32.const 4))
    (then
      ;; GAME OVER
      (drop
        (call $blit
          ;; x y
          (i32.const 125) (i32.const 33)
          ;; whcol_offset
          (i32.const 36)
          ;; src_offset
          (i32.const 6242)))))
)
