#include "common.h"
#include "rbb.h"
#include "vpi.h"
#include <assert.h>
#include <ftdi.h>

struct ftdi_context *ftdi;
int client_fd = -1;
int listen_fd = -1;
JtagState state = TestLogicReset;
bool debug = false;

void ftdi_init() {
  ftdi = ftdi_new();
  assert(ftdi);

  int ret = ftdi_set_interface(ftdi, INTERFACE_B);
  assert(ret == 0);
  ret = ftdi_usb_open(ftdi, 0x0403, 0x6011);
  assert(ret == 0);
  ret = ftdi_usb_reset(ftdi);
  assert(ret == 0);
  ret = ftdi_set_baudrate(ftdi, 62500); // 1MBaud
  assert(ret == 0);
}

int main(int argc, char *argv[]) {
  ftdi_init();
  // jtag_rbb_init();
  jtag_vpi_init();
  for (;;) {
    // jtag_rbb_tick();
    jtag_vpi_tick();
  }
  return 0;
}