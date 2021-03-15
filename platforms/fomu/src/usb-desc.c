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

#include <usb-desc.h>
#include <usbstd.h>
#include <usbcdc.h>

// USB Descriptors are binary data which the USB host reads to
// automatically detect a USB device's capabilities.  The format
// and meaning of every field is documented in numerous USB
// standards.  When working with USB descriptors, despite the
// complexity of the standards and poor writing quality in many
// of those documents, remember descriptors are nothing more
// than constant binary data that tells the USB host what the
// device can do.  Computers will load drivers based on this data.
// Those drivers then communicate on the endpoints specified by
// the descriptors.

// To configure a new combination of interfaces or make minor
// changes to existing configuration (eg, change the name or ID
// numbers), usually you would edit "usb_desc.h".  This file
// is meant to be configured by the header, so generally it is
// only edited to add completely new USB interfaces or features.

// **************************************************************
//   USB Device
// **************************************************************

#define LSB(n) ((n) & 255)
#define MSB(n) (((n) >> 8) & 255)

#define USB_DT_BOS_SIZE 5
#define USB_DT_BOS 0xf
#define USB_DT_DEVICE_CAPABILITY 0x10
#define USB_DC_PLATFORM 5

struct usb_bos_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumDeviceCaps;
} __attribute__((packed));

// USB Device Descriptor.  The USB host reads this first, to learn
// what type of device is connected.
static const uint8_t device_descriptor[] = {
        18,                                     // bLength
        1,                                      // bDescriptorType
        0x10, 0x02,                             // bcdUSB
        USB_CLASS_CDC,                          // bDeviceClass
        0x00,                                   // bDeviceSubClass
        0x00,                                   // bDeviceProtocol
        64,                                     // bMaxPacketSize0
        LSB(VENDOR_ID), MSB(VENDOR_ID),         // idVendor
        LSB(PRODUCT_ID), MSB(PRODUCT_ID),       // idProduct
        LSB(DEVICE_VER), MSB(DEVICE_VER),       // bcdDevice
        1,                                      // iManufacturer
        2,                                      // iProduct
        0,                                      // iSerialNumber
        1                                       // bNumConfigurations
};


// **************************************************************
//   USB Configuration
// **************************************************************

// USB Configuration Descriptor.  This huge descriptor tells all
// of the devices capbilities.
#define CONFIG_DESC_SIZE 67
static const uint8_t config_descriptor[CONFIG_DESC_SIZE] = {
        // configuration descriptor, USB spec 9.6.3, page 264-266, Table 9-10
        9,                                      // bLength;
        2,                                      // bDescriptorType;
        LSB(CONFIG_DESC_SIZE),                  // wTotalLength
        MSB(CONFIG_DESC_SIZE),
        2,                                      // bNumInterfaces
        1,                                      // bConfigurationValue
        0,                                      // iConfiguration
        0x80,                                   // bmAttributes
        50,                                     // bMaxPower

        // interface descriptor, CDC
        USB_DT_INTERFACE_SIZE,                  // bLength
        USB_DT_INTERFACE,                       // bDescriptorType
        0,                                      // bInterfaceNumber
        0,                                      // bAlternateSetting
        1,                                      // bNumEndpoints
        USB_CLASS_CDC,                          // bInterfaceClass
        USB_CDC_SUBCLASS_ACM,                   // bInterfaceSubClass
        USB_CDC_PROTOCOL_AT,                    // bInterfaceProtocol
        0,                                      // iInterface

        // Header Functional Descriptor
        0x05,                                   // bLength: Endpoint Descriptor size
        CS_INTERFACE,                           // bDescriptorType: CS_INTERFACE
        USB_CDC_TYPE_HEADER,                    // bDescriptorSubtype: Header Func Desc
        0x10,                                   // bcdCDC: spec release number
        0x01,

        // Call Management Functional Descriptor
        0x05,                                   // bFunctionLength
        CS_INTERFACE,                           // bDescriptorType: CS_INTERFACE
        USB_CDC_TYPE_CALL_MANAGEMENT,           // bDescriptorSubtype: Call Management Func Desc
        0,                                      // bmCapabilities: D0+D1
        1,                                      // bDataInterface: 1

        // ACM Functional Descriptor
        0x04,                                   // bFunctionLength
        CS_INTERFACE,                           // bDescriptorType: CS_INTERFACE
        USB_CDC_TYPE_ACM,                       // bDescriptorSubtype: Abstract Control Management desc
        6,                                      // bmCapabilities

        // Union Functional Descriptor
        0x05,                                   // bFunctionLength
        CS_INTERFACE,                           // bDescriptorType: CS_INTERFACE
        USB_CDC_TYPE_UNION,                     // bDescriptorSubtype: Union func desc
        0,                                      // bMasterInterface: Communication class interface
        1,                                      // bSlaveInterface0: Data Class Interface

        // Endpoint descriptor
        USB_DT_ENDPOINT_SIZE,                   // bLength
        USB_DT_ENDPOINT,                        // bDescriptorType
        0x81,                                   // bEndpointAddress
        USB_ENDPOINT_ATTR_INTERRUPT,            // bmAttributes
        LSB(16),                                // wMaxPacketSize
        MSB(16),                                // wMaxPacketSize
        255,                                    // bInterval

        // interface descriptor, CDC
        USB_DT_INTERFACE_SIZE,                  // bLength
        USB_DT_INTERFACE,                       // bDescriptorType
        1,                                      // bInterfaceNumber
        0,                                      // bAlternateSetting
        2,                                      // bNumEndpoints
        USB_CLASS_DATA,                         // bInterfaceClass
        0,                                      // bInterfaceSubClass
        0,                                      // bInterfaceProtocol
        0,                                      // iInterface

        // Endpoint descriptor
        USB_DT_ENDPOINT_SIZE,                   // bLength
        USB_DT_ENDPOINT,                        // bDescriptorType
        0x82,                                   // bEndpointAddress
        USB_ENDPOINT_ATTR_BULK,                 // bmAttributes
        LSB(64),                                // wMaxPacketSize
        MSB(64),                                // wMaxPacketSize
        1,                                      // bInterval

        // Endpoint descriptor
        USB_DT_ENDPOINT_SIZE,                   // bLength
        USB_DT_ENDPOINT,                        // bDescriptorType
        0x02,                                   // bEndpointAddress
        USB_ENDPOINT_ATTR_BULK,                 // bmAttributes
        LSB(64),                                // wMaxPacketSize
        MSB(64),                                // wMaxPacketSize
        1,                                      // bInterval
};


// **************************************************************
//   String Descriptors
// **************************************************************

// The descriptors above can provide human readable strings,
// referenced by index numbers.  These descriptors are the
// actual string data

static const struct usb_string_descriptor_struct string0 = {
    4,
    3,
    {0x0409}
};

// Microsoft OS String Descriptor. See: https://github.com/pbatard/libwdi/wiki/WCID-Devices
static const struct usb_string_descriptor_struct usb_string_microsoft = {
    18, 3,
    {'M','S','F','T','1','0','0', MSFT_VENDOR_CODE}
};
 
// Microsoft WCID
const uint8_t usb_microsoft_wcid[MSFT_WCID_LEN] = {
    MSFT_WCID_LEN, 0, 0, 0,         // Length
    0x00, 0x01,                     // Version
    0x04, 0x00,                     // Compatibility ID descriptor index
    0x01,                           // Number of sections
    0, 0, 0, 0, 0, 0, 0,            // Reserved (7 bytes)

    0,                              // Interface number
    0x01,                           // Reserved
    'W','I','N','U','S','B',0,0,    // Compatible ID
    0,0,0,0,0,0,0,0,                // Sub-compatible ID (unused)
    0,0,0,0,0,0,                    // Reserved
};

const struct webusb_url_descriptor landing_url_descriptor = {
    .bLength = LANDING_PAGE_DESCRIPTOR_SIZE,
    .bDescriptorType = WEBUSB_DT_URL,
    .bScheme = WEBUSB_URL_SCHEME_HTTPS,
    .URL = LANDING_PAGE_URL
};

struct full_bos {
    struct usb_bos_descriptor bos;
    struct webusb_platform_descriptor webusb;
};

static const struct full_bos full_bos = {
    .bos = {
        .bLength = USB_DT_BOS_SIZE,
        .bDescriptorType = USB_DT_BOS,
        .wTotalLength = USB_DT_BOS_SIZE + WEBUSB_PLATFORM_DESCRIPTOR_SIZE,
        .bNumDeviceCaps = 1,
    },
    .webusb = {
        .bLength = WEBUSB_PLATFORM_DESCRIPTOR_SIZE,
        .bDescriptorType = USB_DT_DEVICE_CAPABILITY,
        .bDevCapabilityType = USB_DC_PLATFORM,
        .bReserved = 0,
        .platformCapabilityUUID = WEBUSB_UUID,
        .bcdVersion = 0x0100,
        .bVendorCode = WEBUSB_VENDOR_CODE,
        .iLandingPage = 1,
    },
};

__attribute__((aligned(4)))
static const struct usb_string_descriptor_struct usb_string_manufacturer_name = {
    2 + MANUFACTURER_NAME_LEN,
    3,
    MANUFACTURER_NAME
};

__attribute__((aligned(4)))
struct usb_string_descriptor_struct usb_string_product_name = {
    2 + PRODUCT_NAME_LEN,
    3,
    PRODUCT_NAME
};

// **************************************************************
//   Descriptors List
// **************************************************************

// This table provides access to all the descriptor data above.

__attribute__((section(".data")))
const usb_descriptor_list_t usb_descriptor_list[] = {
    {0x0100, sizeof(device_descriptor), device_descriptor},
    {0x0200, sizeof(config_descriptor), config_descriptor},
    {0x0300, 0, (const uint8_t *)&string0},
    {0x0301, 0, (const uint8_t *)&usb_string_manufacturer_name},
    {0x0302, 0, (const uint8_t *)&usb_string_product_name},
    {0x03EE, 0, (const uint8_t *)&usb_string_microsoft},
    {0x0F00, sizeof(full_bos), (const uint8_t *)&full_bos},
    {0, 0, NULL}
};
