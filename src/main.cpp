#include "common.h"
#include "usb_blaster.h"
#include "jtagd.h"
#include "rbb.h"
#include "vpi.h"
#include "xvc.h"
#include <assert.h>
#include <inttypes.h>
#include <sys/signal.h>
#include <time.h>
#include <unistd.h>

int client_fd = -1;
int listen_fd = -1;
JtagState state = TestLogicReset;
bool debug = false;

int ftdi_vid = 0x0403;
int ftdi_pid = 0x6011;
enum ftdi_interface ftdi_channel = INTERFACE_A;
bool stop = false;
uint64_t bits_send = 0;
uint64_t freq_mhz = 15;

uint8_t buffer[BUFFER_SIZE];
size_t buffer_begin = 0;
size_t buffer_end = 0;

void sigint_handler(int sig) {
  printf("Gracefully shutdown\n");
  stop = true;
}

uint64_t get_time_ns() {
  struct timeval tv = {};
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000000000 + (uint64_t)tv.tv_usec * 1000;
}

enum Protocol { VPI, RBB, XVC, JTAGD };

int main(int argc, char *argv[]) {
  signal(SIGINT, sigint_handler);
  signal(SIGPIPE, SIG_IGN);

  // https://man7.org/linux/man-pages/man3/getopt.3.html
  int opt;
  Protocol proto = Protocol::VPI;
  while ((opt = getopt(argc, argv, "dvrxjbc:V:p:f:")) != -1) {
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
    case 'x':
      proto = Protocol::XVC;
      break;
    case 'j':
      proto = Protocol::JTAGD;
      break;
    case 'b':
      adapter = &usb_blaster_driver;
      break;
    case 'c':
      if ('A' <= optarg[0] && optarg[0] <= 'D') {
        ftdi_channel = (ftdi_interface)(optarg[0] - 'A' + 1);
      }
      break;
    case 'V':
      sscanf(optarg, "%x", &ftdi_vid);
      break;
    case 'p':
      sscanf(optarg, "%x", &ftdi_pid);
      break;
    case 'f':
      sscanf(optarg, "%" SCNu64, &freq_mhz);
      break;
    default: /* '?' */
      fprintf(stderr, "Usage: %s [-d] [-v|-r] [-V vid] [-p pid] [-f freq]\n",
              argv[0]);
      fprintf(stderr, "\t-d: Enable debug messages\n");
      fprintf(stderr, "\t-v: Use jtag_vpi protocol\n");
      fprintf(stderr, "\t-r: Use remote bitbang protocol\n");
      fprintf(stderr, "\t-x: Use xilinx virtual cable protocol\n");
      fprintf(stderr, "\t-j: Use intel jtag server protocol\n");
      fprintf(stderr, "\t-b: Use USB Blaster adapter\n");
      fprintf(stderr, "\t-c A|B|C|D: Select ftdi channel\n");
      fprintf(stderr, "\t-V VID: Specify usb vid\n");
      fprintf(stderr, "\t-p PID: Specify usb pid\n");
      fprintf(stderr, "\t-f FREQ: Specify jtag clock frequency in MHz\n");
      return 1;
    }
  }

  if (!adapter_init()) {
    return 1;
  }

  if (proto == Protocol::RBB) {
    printf("Use remote bitbang protocol\n");
    jtag_rbb_init();
  } else if (proto == Protocol::VPI) {
    printf("Use jtag_vpi protocol\n");
    jtag_vpi_init();
  } else if (proto == Protocol::XVC) {
    printf("Use xilinx virtual cable protocol\n");
    jtag_xvc_init();
  } else if (proto == Protocol::JTAGD) {
    printf("Use intel jtag server protocol\n");
    jtag_jtagd_init();
  }
  uint64_t last_time = get_time_ns();
  uint64_t last_bits_send = 0;
  while (!stop) {
    uint64_t current_time = get_time_ns();
    if (current_time - last_time > 1000000000l) {
      fprintf(stderr, "\rSpeed: %.2lf kbps",
              (double)((bits_send - last_bits_send) * 1000000000l / 1000) /
                  (current_time - last_time));
      last_time = current_time;
      last_bits_send = bits_send;
    }
    if (proto == Protocol::RBB) {
      jtag_rbb_tick();
    } else if (proto == Protocol::VPI) {
      jtag_vpi_tick();
    } else if (proto == Protocol::XVC) {
      jtag_xvc_tick();
    } else if (proto == Protocol::JTAGD) {
      jtag_jtagd_tick();
    }
  }
  fflush(stdout);
  return 0;
}