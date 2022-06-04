#include "usb_blaster.h"

#include "common.h"
#include <algorithm>
#include <ftdi.h>

static struct ftdi_context *ftdi;

bool usb_blaster_init() {
  printf("Initialize ftdi\n");
  ftdi = ftdi_new();
  assert(ftdi);

  printf("Use channel %c\n", (int)ftdi_channel - 1 + 'A');
  int ret = ftdi_set_interface(ftdi, ftdi_channel);
  if (ret) {
    printf("Error: %s\n", ftdi_get_error_string(ftdi));
    return false;
  }

  printf("Open device vid=0x%04x pid=0x%04x\n", ftdi_vid, ftdi_pid);
  ret = ftdi_usb_open(ftdi, ftdi_vid, ftdi_pid);
  if (ret) {
    printf("Error: %s\n", ftdi_get_error_string(ftdi));
    return false;
  }

  ret = ftdi_usb_reset(ftdi);
  assert(ret == 0);
  ret = ftdi_set_baudrate(ftdi, 115200);
  assert(ret == 0);
  ret = ftdi_set_latency_timer(ftdi, 1); // reduce latency
  assert(ret == 0);

  ftdi_set_bitmode(ftdi, 0, 0);
  return true;
}

bool usb_blaster_deinit() { return true; }

bool usb_blaster_jtag_tms_seq(const uint8_t *data, size_t num_bits) {
  return true;
}

bool usb_blaster_jtag_scan_chain_send(const uint8_t *data, size_t num_bits,
                                      bool flip_tms, bool do_read) {
  return true;
}

bool usb_blaster_jtag_scan_chain_recv(uint8_t *recv, size_t num_bits,
                                      bool flip_tms) {
  return true;
}

bool usb_blaster_set_tck_freq(uint64_t freq_mhz) { return true; }

bool usb_blaster_jtag_clock_tck(size_t times) { return true; }

driver usb_blaster_driver = {
    .init = usb_blaster_init,
    .deinit = usb_blaster_deinit,
    .set_tck_freq = usb_blaster_set_tck_freq,
    .jtag_tms_seq = usb_blaster_jtag_tms_seq,
    .jtag_scan_chain_send = usb_blaster_jtag_scan_chain_send,
    .jtag_scan_chain_recv = usb_blaster_jtag_scan_chain_recv,
    .jtag_clock_tck = usb_blaster_jtag_clock_tck,
};