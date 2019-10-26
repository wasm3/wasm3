#ifndef TOBOOT_INTERNAL_H_
#define TOBOOT_INTERNAL_H_

#include <stdint.h>

/// This describes the structure that allows the OS to communicate
/// with the bootloader.  It keeps track of how many times we've
/// tried booting, as well as a magic value that tells us to enter
/// the bootloader instead of booting the app.
/// It also keeps track of the board model.
__attribute__((section(".boot_token"))) extern struct toboot_runtime boot_token;

enum bootloader_reason
{
  NOT_ENTERING_BOOTLOADER = 0,
  BOOT_TOKEN_PRESENT = 1,
  BOOT_FAILED_TOO_MANY_TIMES = 2,
  NO_PROGRAM_PRESENT = 3,
  BUTTON_HELD_DOWN = 4,
  COLD_BOOT_CONFIGURATION_FLAG = 5,
};

extern enum bootloader_reason bootloader_reason;

/// Legacy Toboot V1 configuration values
#ifndef TOBOOT_V1_CFG_FLAGS
#define TOBOOT_V1_CFG_FLAGS 0
#endif
#define TOBOOT_V1_CFG_MAGIC_MASK 0xffff
#define TOBOOT_V1_CFG_MAGIC 0x70b0

#ifndef TOBOOT_V1_APP_FLAGS
#define TOBOOT_V1_APP_FLAGS 0
#endif

#define TOBOOT_V1_APP_MAGIC_MASK 0xffff
#define TOBOOT_V1_APP_MAGIC 0x6fb0
#define TOBOOT_V1_APP_PAGE_MASK 0x00ff0000
#define TOBOOT_V1_APP_PAGE_SHIFT 16

uint32_t tb_first_free_address(void);
uint32_t tb_first_free_sector(void);
const struct toboot_configuration *tb_get_config(void);
uint32_t tb_config_hash(const struct toboot_configuration *cfg);
void tb_sign_config(struct toboot_configuration *cfg);
uint32_t tb_generation(const struct toboot_configuration *cfg);
int tb_valid_signature_at_page(uint32_t page);

#endif /* TOBOOT_INTERNAL_H_ */