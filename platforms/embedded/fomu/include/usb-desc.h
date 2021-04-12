/* Teensyduino Core Library
 * http://www.pjrc.com/teensy/
 * Copyright (c) 2013 PJRC.COM, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * 1. The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * 2. If the Software is incorporated into a build system that allows 
 * selection among a list of target devices, then similar target
 * devices manufactured by PJRC.COM must be included in the list of
 * target devices and selectable in the same manner.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _usb_desc_h_
#define _usb_desc_h_

#include <stdint.h>
#include <stddef.h>
#include <webusb-defs.h>

struct usb_setup_request {
    union {
        struct {
            uint8_t bmRequestType;
            uint8_t bRequest;
        };
        uint16_t wRequestAndType;
    };
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
};

struct usb_string_descriptor_struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wString[];
};

#define NUM_USB_BUFFERS           8
#define VENDOR_ID                 0x1209    // pid.codes
#define PRODUCT_ID                0x5bf0    // Assigned to Fomu project
#define DEVICE_VER                0x0101    // Bootloader version
#define MANUFACTURER_NAME         u"Foosn"
#define MANUFACTURER_NAME_LEN     sizeof(MANUFACTURER_NAME)
#define PRODUCT_NAME              u"Fomu App " GIT_VERSION
#define PRODUCT_NAME_LEN          sizeof(PRODUCT_NAME)

// Microsoft Compatible ID Feature Descriptor
#define MSFT_VENDOR_CODE    '~'     // Arbitrary, but should be printable ASCII
#define MSFT_WCID_LEN       40
extern const uint8_t usb_microsoft_wcid[MSFT_WCID_LEN];

typedef struct {
    uint16_t  wValue;
    uint16_t  length;
    const uint8_t *addr;
} usb_descriptor_list_t;

extern const usb_descriptor_list_t usb_descriptor_list[];

// WebUSB Landing page URL descriptor
#define WEBUSB_VENDOR_CODE 2

#ifndef LANDING_PAGE_URL
#define LANDING_PAGE_URL "dfu.tomu.im"
#endif

#define LANDING_PAGE_DESCRIPTOR_SIZE (WEBUSB_DT_URL_DESCRIPTOR_SIZE \
                                    + sizeof(LANDING_PAGE_URL) - 1)

extern const struct webusb_url_descriptor landing_url_descriptor;

#endif
