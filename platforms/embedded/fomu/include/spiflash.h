#ifndef __SPIFLASH_H
#define __SPIFLASH_H

void write_to_flash_page(unsigned int addr, const unsigned char *c, unsigned int len);
void erase_flash_sector(unsigned int addr);
void write_to_flash(unsigned int addr, const unsigned char *c, unsigned int len);

#endif /* __SPIFLASH_H */
