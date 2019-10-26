#ifndef __USB_H
#define __USB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct usb_setup_request;

void usb_isr(void);
void usb_init(void);
void usb_connect(void);
void usb_disconnect(void);

int usb_irq_happened(void);
void usb_setup(const struct usb_setup_request *setup);
int usb_send(const void *data, int total_count);
void usb_ack_in(void);
void usb_ack_out(void);
void usb_err_in(void);
void usb_err_out(void);
int usb_recv(void *buffer, unsigned int buffer_len);
void usb_poll(void);
int usb_wait_for_send_done(void);
void usb_recv_done(void);
void usb_set_address(uint8_t new_address);

void usb_putc(char c);
int usb_getc(void);
int usb_write(const char *buf, int count);
int usb_can_getc(void);
int usb_can_putc(void);

#ifdef __cplusplus
}
#endif

#endif
