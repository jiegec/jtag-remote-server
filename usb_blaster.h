#ifndef __USB_BLASTER_H__
#define __USB_BLASTER_H__

#include "common.h"
#include <stdint.h>
#include <stdlib.h>

// initialize usb_blaster interface of ftdi
bool usb_blaster_init();
bool usb_blaster_deinit();
bool usb_blaster_set_tck_freq(uint64_t freq_mhz);

// jtag functions
bool usb_blaster_jtag_tms_seq(const uint8_t *data, size_t num_bits);
bool usb_blaster_jtag_scan_chain_send(const uint8_t *data, size_t num_bits,
                                bool flip_tms, bool do_read);
bool usb_blaster_jtag_scan_chain_recv(uint8_t *recv, size_t num_bits, bool flip_tms);
bool usb_blaster_jtag_clock_tck(size_t times);

extern driver usb_blaster_driver;

#endif