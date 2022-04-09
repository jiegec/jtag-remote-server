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
  ret = ftdi_set_latency_timer(ftdi, 1); // reduce latency
  assert(ret == 0);
}

enum Protocol { VPI, RBB };

int main(int argc, char *argv[]) {
  // https://man7.org/linux/man-pages/man3/getopt.3.html
  int opt;
  Protocol proto = Protocol::VPI;
  while ((opt = getopt(argc, argv, "dvr")) != -1) {
    switch (opt) {
    case 'd':
      debug = true;
      break;
    case 'v':
      proto = Protocol::VPI;
      break;
    case 'r':
      proto = Protocol::RBB;
      break;
    default: /* '?' */
      fprintf(stderr, "Usage: %s [-d] [-v|-r] name\n", argv[0]);
      return 1;
    }
  }

  ftdi_init();
  if (proto == Protocol::RBB) {
    printf("Using remote bitbang protocol\n");
    jtag_rbb_init();
  } else if (proto == Protocol::VPI) {
    printf("Using jtag_vpi protocol\n");
    jtag_vpi_init();
  }
  for (;;) {
    if (proto == Protocol::RBB) {
      jtag_rbb_tick();
    } else if (proto == Protocol::VPI) {
      jtag_vpi_tick();
    }
  }
  return 0;
}