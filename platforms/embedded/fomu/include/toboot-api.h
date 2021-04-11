#ifndef TOBOOT_API_H_
#define TOBOOT_API_H_

#include <stdint.h>

/// Store this configuration struct at offset 0x94 from the start
/// of your binary image.
/// You may set all RESERVED values to 0. as they will be calculated
/// when the program is written to flash.
struct toboot_configuration {
    /// Set to 0x907070b2 to indicate a valid configuration header.
    uint32_t magic;
    
    /// Reserved value.  Used as a generational counter.  Toboot will
    /// overwrite this value with a monotonically-increasing counter
    /// every time a new image is burned.  This is used to determine
    /// which image is the newest.
    uint16_t reserved_gen;

    /// The starting page for your program in 1024-byte blocks.
    /// Toboot itself sets this to 0.  If this is nonzero, then it
    /// must be located after the Toboot image.  Toboot is currently
    /// under 5 kilobytes, so make sure this value is at least 6.
    uint8_t  start;

    /// Configuration bitmask.  See below for values.
    uint8_t  config;

    /// Set to 0x18349420 to prevent the user from entering Toboot manually.
    /// Use this value with caution, as it can be used to lockout a Tomu.
    uint32_t lock_entry;

    /// A bitmask of sectors to erase when updating the program.  Each "1"
    /// causes that sector to be erased, unless it's Toboot itself.  Bit values
    /// determine flash blocks 0-31.
    uint32_t erase_mask_lo;

    /// A bitmask of sectors to erase when updating the program.  Each "1"
    /// causes that sector to e erased.  Use these two values to e.g. force
    /// private keys to be erased when updating.  Bit values determine flash
    /// blocks 32-63.
    uint32_t erase_mask_hi;

    /// A hash of the entire header, minus this entry.  Toboot calculates
    /// this when it programs the first block.  A Toboot configuration
    /// header MUST have a valid hash in order to be considered valid.
    uint32_t reserved_hash;
} __attribute__((packed));

/// Toboot V1.0 leaves IRQs enabled, mimicking the behavior of
/// AN0042.  Toboot V2.0 makes this configurable by adding a
/// bit to the configuration area.
#define TOBOOT_CONFIG_FLAG_ENABLE_IRQ_MASK  0x01
#define TOBOOT_CONFIG_FLAG_ENABLE_IRQ_SHIFT 0
#define TOBOOT_CONFIG_FLAG_ENABLE_IRQ       (1 << 0)
#define TOBOOT_CONFIG_FLAG_DISABLE_IRQ      (0 << 0)

/// When running a normal program, you won't want Toboot to run.
/// However, when developing new software it is handy to have
/// Toboot run at poweron, instead of jumping straight to your
/// program.  Set this flag to enter your program whenever the
/// system has just powered on.
#define TOBOOT_CONFIG_FLAG_AUTORUN_MASK         0x02
#define TOBOOT_CONFIG_FLAG_AUTORUN_SHIFT        1
#define TOBOOT_CONFIG_FLAG_AUTORUN              (1 << 1)
#define TOBOOT_CONFIG_FLAG_AUTORUN_DISABLED     (0 << 1)

/// When we create a fake header, this flag will be set.  Otherwise,
/// leave the flag cleared.  This field has no meaning in user
/// applications, and is only used internally.
#define TOBOOT_CONFIG_FAKE_MASK 0x04
#define TOBOOT_CONFIG_FAKE_SHIFT 2
#define TOBOOT_CONFIG_FAKE (1 << 2)

/// Various magic values describing Toboot configuration headers.
#define TOBOOT_V1_MAGIC         0x6fb0
#define TOBOOT_V1_MAGIC_MASK    0x0000ffff
#define TOBOOT_V2_MAGIC         0x907070b2
#define TOBOOT_V2_MAGIC_MASK    0xffffffff

/// This value is used to prevent manual entry into Toboot.
#define TOBOOT_LOCKOUT_MAGIC    0x18349420

/// The seed value for the hash of Toboot's configuration header
#define TOBOOT_HASH_SEED 0x037a5cf1

/// This is the runtime part that lives at the start of RAM.
/// Running programs can use this to force entry into Toboot
/// during the next reboot.
struct toboot_runtime {
    /// Set this to 0x74624346 and reboot to enter bootloader,
    /// even if LOCKOUT is enabled.
    uint32_t magic;

    /// Set this to 0 when your program starts.
    uint8_t  boot_count;

    /// The bootloader should set this to 0x23 for Tomu.
    uint8_t  board_model;

    /// Unused.
    uint16_t reserved;
};

/// Set runtime.magic to this value and reboot to force
/// entry into Toboot.
#define TOBOOT_FORCE_ENTRY_MAGIC    0x74624346

#endif /* TOBOOT_API_H_ */