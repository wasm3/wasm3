#ifndef __CRC_H
#define __CRC_H

#ifdef __cplusplus
extern "C" {
#endif

unsigned short crc16(const unsigned char *buffer, int len);
unsigned int crc32(const unsigned char *buffer, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif
