#include "common.h"
#include "rbb.h"
#include "vpi.h"
#include "xvc.h"
#include <assert.h>
#include <ftdi.h>
#include <sys/signal.h>
#include <time.h>
#include <unistd.h>

struct ftdi_context *ftdi;
int client_fd = -1;
int listen_fd = -1;
JtagState state = TestLogicReset;
bool debug = false;

// default: FD4232H
int ftdi_vid = 0x0403;
int ftdi_pid = 0x6011;
enum ftdi_interface ftdi_channel = INTERFACE_A;

bool stop = false;
uint64_t bits_send = 0;
uint64_t freq_mhz = 15;

void sigint_handler(int sig) {
  printf("Gracefully shutdown\n");
  stop = true;
}

bool ftdi_init() {
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
  return mpsse_init();
}

uint64_t get_time_ns() {
  struct timeval tv = {};
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000000000 + (uint64_t)tv.tv_usec * 1000;
}

enum Protocol { VPI, RBB, XVC };

int main(int argc, char *argv[]) {
  signal(SIGINT, sigint_handler);

  // https://man7.org/linux/man-pages/man3/getopt.3.html
  int opt;
  Protocol proto = Protocol::VPI;
  while ((opt = getopt(argc, argv, "dvrxc:p:f:")) != -1) {
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
    case 'c':
      if ('A' <= optarg[0] && optarg[0] <= 'D') {
        ftdi_channel = (ftdi_interface)(optarg[0] - 'A' + 1);
      }
      break;
    case 'p':
      sscanf(optarg, "%x", &ftdi_pid);
      break;
    case 'f':
      sscanf(optarg, "%d", &freq_mhz);
      break;
    default: /* '?' */
      fprintf(stderr, "Usage: %s [-d] [-v|-r] [-p pid] [-f freq]\n", argv[0]);
      fprintf(stderr, "\t-d: Enable debug messages\n");
      fprintf(stderr, "\t-v: Use jtag_vpi protocol\n");
      fprintf(stderr, "\t-r: Use remote bitbang protocol\n");
      fprintf(stderr, "\t-x: Use xilinx virtual cable protocol\n");
      fprintf(stderr, "\t-c A|B|C|D: Select ftdi channel\n");
      fprintf(stderr, "\t-p PID: Specify usb pid\n");
      fprintf(stderr, "\t-f FREQ: Specify jtag clock frequency in MHz\n");
      return 1;
    }
  }

  if (!ftdi_init()) {
    return false;
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
    }
  }
  fflush(stdout);
  return 0;
}