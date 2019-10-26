/*
 * Copyright (c) 2016, Devan Lai
 *
 * Permission to use, copy, modify, and/or distribute this software
 * for any purpose with or without fee is hereby granted, provided
 * that the above copyright notice and this permission notice
 * appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef WEBUSB_DEFS_H_INCLUDED
#define WEBUSB_DEFS_H_INCLUDED

#include <stdint.h>

#define WEBUSB_REQ_GET_URL             0x02

#define WEBUSB_DT_URL 3

#define WEBUSB_URL_SCHEME_HTTP 0
#define WEBUSB_URL_SCHEME_HTTPS 1

struct webusb_platform_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bDevCapabilityType;
    uint8_t bReserved;
    uint8_t platformCapabilityUUID[16];
    uint16_t bcdVersion;
    uint8_t bVendorCode;
    uint8_t iLandingPage;
} __attribute__((packed));

#define WEBUSB_PLATFORM_DESCRIPTOR_SIZE sizeof(struct webusb_platform_descriptor)

#define WEBUSB_UUID {0x38, 0xB6, 0x08, 0x34, 0xA9, 0x09, 0xA0, 0x47,0x8B, 0xFD, 0xA0, 0x76, 0x88, 0x15, 0xB6, 0x65}

struct webusb_url_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bScheme;
    char URL[];
} __attribute__((packed));

#define WEBUSB_DT_URL_DESCRIPTOR_SIZE 3

#endif
