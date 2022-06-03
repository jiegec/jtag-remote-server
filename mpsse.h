#ifndef __MPSSE_H__
#define __MPSSE_H__

#include <stdint.h>
#include <stdlib.h>

// initialize mpsse interface of ftdi
bool mpsse_init();
bool mpsse_deinit();
bool mpsse_set_tck_freq(uint64_t freq_mhz);

// jtag functions
bool mpsse_jtag_tms_seq(const uint8_t *data, size_t num_bits);
bool mpsse_jtag_scan_chain_send(const uint8_t *data, size_t num_bits, bool flip_tms,
                          bool do_read);
bool mpsse_jtag_scan_chain_recv(uint8_t *recv, size_t num_bits, bool flip_tms);
bool mpsse_jtag_clock_tck(size_t times);

#endif