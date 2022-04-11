#include "common.h"
#include <assert.h>
#include <stdarg.h>

JtagState next_state(JtagState cur, int bit) {
  switch (cur) {
  case TestLogicReset:
    return bit ? TestLogicReset : RunTestIdle;
  case RunTestIdle:
    return bit ? SelectDRScan : RunTestIdle;
  case SelectDRScan:
    return bit ? SelectIRScan : CaptureDR;
  case CaptureDR:
    return bit ? Exit1DR : ShiftDR;
  case ShiftDR:
    return bit ? Exit1DR : ShiftDR;
  case Exit1DR:
    return bit ? UpdateDR : PauseDR;
  case PauseDR:
    return bit ? Exit2DR : PauseDR;
  case Exit2DR:
    return bit ? UpdateDR : ShiftDR;
  case UpdateDR:
    return bit ? SelectDRScan : RunTestIdle;
  case SelectIRScan:
    return bit ? TestLogicReset : CaptureIR;
  case CaptureIR:
    return bit ? Exit1IR : ShiftIR;
  case ShiftIR:
    return bit ? Exit1IR : ShiftIR;
  case Exit1IR:
    return bit ? UpdateIR : PauseIR;
  case PauseIR:
    return bit ? Exit2IR : PauseIR;
  case Exit2IR:
    return bit ? UpdateIR : ShiftIR;
  case UpdateIR:
    return bit ? SelectDRScan : RunTestIdle;
  default:
    assert(false);
  }
  return TestLogicReset;
}

const char *state_to_string(JtagState state) {
  switch (state) {
  case TestLogicReset:
    return "TestLogicReset";
  case RunTestIdle:
    return "RunTestIdle";
  case SelectDRScan:
    return "SelectDRScan";
  case CaptureDR:
    return "CaptureDR";
  case ShiftDR:
    return "ShiftDR";
  case Exit1DR:
    return "Exit1DR";
  case PauseDR:
    return "PauseDR";
  case Exit2DR:
    return "Exit2DR";
  case UpdateDR:
    return "UpdateDR";
  case SelectIRScan:
    return "SelectIRScan";
  case CaptureIR:
    return "CaptureIR";
  case ShiftIR:
    return "ShiftIR";
  case Exit1IR:
    return "Exit1IR";
  case PauseIR:
    return "PauseIR";
  case Exit2IR:
    return "Exit2IR";
  case UpdateIR:
    return "UpdateIR";
  default:
    assert(false);
  }
}

bool mpsse_init() {
  // reset mpsse and enable
  ftdi_set_bitmode(ftdi, 0, 0);
  ftdi_set_bitmode(ftdi, 0, BITMODE_MPSSE);

  // set clock to base / ((1 + 1) * 2)
  // when "divide by 5" is disabled, base clock is 60MHz
  int divisor = (60 / 2 + freq_mhz - 1) / freq_mhz - 1;
  int actual_freq = 60 / ((1 + divisor) * 2);
  printf("Requested jtag tck: %d MHz\n", freq_mhz);
  printf("Actual jtag tck: %d MHz\n", actual_freq);
  uint8_t setup[256] = {
      SET_BITS_LOW,     0x88, 0x8b,      SET_BITS_HIGH, 0, 0, TCK_DIVISOR,
      (uint8_t)divisor, 0x00, DIS_DIV_5, SEND_IMMEDIATE};
  if (ftdi_write_data(ftdi, setup, 10) != 10) {
    printf("Error: %s\n", ftdi_get_error_string(ftdi));
    return false;
  }

  return true;
}

bool jtag_fsm_reset() {
  // 11111: Goto Test-Logic-Reset
  uint8_t tms[] = {0x1F};
  return jtag_tms_seq(tms, 5);
}

bool jtag_tms_seq(const uint8_t *data, size_t num_bits) {
  bits_send += num_bits;
  dprintf("Sending TMS Seq ");
  print_bitvec(data, num_bits);
  dprintf("\n");

  // compute state transition
  JtagState new_state = state;
  for (size_t i = 0; i < num_bits; i++) {
    uint8_t bit = (data[i / 8] >> (i % 8)) & 1;
    new_state = next_state(new_state, bit);
  }
  dprintf("JTAG state: %s -> %s\n", state_to_string(state),
          state_to_string(new_state));
  state = new_state;

  for (size_t i = 0; i < (num_bits + 7) / 8; i++) {
    uint8_t cur_bits = std::min((size_t)8, num_bits - i * 8);

    // Clock Data to TMS pin (no read)
    uint8_t idle[256] = {MPSSE_WRITE_TMS | MPSSE_LSB | MPSSE_BITMODE |
                             MPSSE_WRITE_NEG,
                         // length in bits -1
                         (uint8_t)(cur_bits - 1),
                         // data
                         data[i]};
    if (ftdi_write_data(ftdi, idle, 3) != 3) {
      printf("Error: %s\n", ftdi_get_error_string(ftdi));
      return false;
    }
  }

  return true;
}

void print_bitvec(const uint8_t *data, size_t bits) {
  if (!debug) {
    return;
  }

  for (size_t i = 0; i < bits; i++) {
    int off = i % 8;
    int bit = ((data[i / 8]) >> off) & 1;
    printf("%c", bit ? '1' : '0');
  }
  printf("(0x");
  int bytes = (bits + 7) / 8;
  for (int i = bytes - 1; i >= 0; i--) {
    printf("%02X", data[i]);
  }
  printf(")");
}

bool jtag_scan_chain(const uint8_t *data, uint8_t *recv, size_t num_bits,
                     bool flip_tms, bool do_read) {
  bits_send += num_bits;
  dprintf("Write TDI%s %d bits: ", flip_tms ? "+TMS" : "", num_bits);
  print_bitvec(data, num_bits);
  dprintf("\n");

  size_t bulk_bits = num_bits;
  if (flip_tms) {
    // last bit should be sent along TMS 0->1
    bulk_bits -= 1;
  }
  uint8_t do_read_flag = do_read ? MPSSE_DO_READ : 0;

  // send whole bytes first
  size_t length_in_bytes = bulk_bits / 8;
  if (length_in_bytes) {
    uint8_t buf[256] = {
        (uint8_t)(do_read_flag | MPSSE_DO_WRITE | MPSSE_LSB | MPSSE_WRITE_NEG),
        (uint8_t)((length_in_bytes - 1) & 0xff),
        (uint8_t)((length_in_bytes - 1) >> 8)};
    if (ftdi_write_data(ftdi, buf, 3) != 3) {
      printf("Error: %s\n", ftdi_get_error_string(ftdi));
      return false;
    }

    if (ftdi_write_data(ftdi, data, length_in_bytes) != length_in_bytes) {
      printf("Error: %s\n", ftdi_get_error_string(ftdi));
      return false;
    }
  }

  // sent rest bits
  if (bulk_bits % 8) {
    uint8_t buf[256] = {
        (uint8_t)(do_read_flag | MPSSE_DO_WRITE | MPSSE_LSB | MPSSE_WRITE_NEG |
                  MPSSE_BITMODE),
        // length in bits -1
        (uint8_t)((bulk_bits % 8) - 1),
        // data
        data[length_in_bytes],
    };
    if (ftdi_write_data(ftdi, buf, 3) != 3) {
      printf("Error: %s\n", ftdi_get_error_string(ftdi));
      return false;
    }
  }

  if (flip_tms) {
    // send last bit along TMS=1
    JtagState new_state = next_state(state, 1);
    dprintf("JTAG state: %s -> %s\n", state_to_string(state),
            state_to_string(new_state));
    state = new_state;

    uint8_t bit = (data[(num_bits - 1) / 8] >> ((num_bits - 1) % 8)) & 1;
    uint8_t buf[3] = {(uint8_t)(do_read_flag | MPSSE_WRITE_TMS | MPSSE_LSB |
                                MPSSE_BITMODE | MPSSE_WRITE_NEG),
                      // length in bits -1
                      0x00,
                      // data
                      // 7-th bit: last bit
                      // TMS=1
                      (uint8_t)(0x01 | (bit << 7))};
    if (ftdi_write_data(ftdi, buf, 3) != 3) {
      printf("Error: %s\n", ftdi_get_error_string(ftdi));
      return false;
    }
  }

  // speedup if read is not required
  if (!do_read) {
    return true;
  }

  // read bulk
  size_t len = (bulk_bits + 7) / 8;
  memset(recv, 0, len);
  size_t offset = 0;
  while (len > offset) {
    int read = ftdi_read_data(ftdi, &recv[offset], len - offset);
    if (read < 0) {
      printf("Error: %s\n", ftdi_get_error_string(ftdi));
      return false;
    }
    offset += read;
  }

  if (bulk_bits % 8) {
    // a length of 1 bit will have the data bit sampled in bit 7 of the byte
    // sent back to the PC, so we need to shift this
    recv[bulk_bits / 8] >>= 8 - (bulk_bits % 8);
  }

  // handle last bit when TMS=1
  if (flip_tms) {
    uint8_t last_bit;
    while (ftdi_read_data(ftdi, &last_bit, 1) != 1)
      ;

    // the bit read is at BIT 7
    recv[(num_bits - 1) / 8] |= ((last_bit >> 7) & 1) << ((num_bits - 1) % 8);
  }

  dprintf("Read TDO: ");
  print_bitvec(recv, num_bits);
  dprintf("\n");

  return true;
}

bool write_full(int fd, const uint8_t *data, size_t count) {
  size_t num_sent = 0;
  while (num_sent < count) {
    ssize_t res = write(fd, &data[num_sent], count - num_sent);
    if (res > 0) {
      num_sent += res;
    } else if (count < 0) {
      return false;
    }
  }

  return true;
}

void dprintf(const char *fmt, ...) {
  if (!debug) {
    return;
  }
  va_list va_args;
  va_start(va_args, fmt);
  vprintf(fmt, va_args);
  va_end(va_args);
}

bool setup_tcp_server(uint16_t port) {
  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket");
    return false;
  }

  // set non blocking
  fcntl(listen_fd, F_SETFL, O_NONBLOCK);

  int reuseaddr = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int)) <
      0) {
    perror("setsockopt");
    return false;
  }

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    return false;
  }

  if (listen(listen_fd, 1) == -1) {
    perror("listen");
    return false;
  }

  return true;
}

bool try_accept() {
  // accept connection
  client_fd = accept(listen_fd, NULL, NULL);
  if (client_fd > 0) {
    // fcntl(client_fd, F_SETFL, O_NONBLOCK);

    // set nodelay
    int flags = 1;
    if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, (void *)&flags,
                   sizeof(flags)) < 0) {
      perror("setsockopt");
    }
    printf("JTAG debugger attached\n");
    return true;
  }
  return false;
}

bool jtag_clock_tck(size_t times) {
  bits_send += times;
  size_t times_8 = times / 8;
  if (times_8) {
    // Clock For n x 8 bits with no data transfer
    uint8_t buf[256] = {
        0x8F,
        (uint8_t)((times_8 - 1) & 0xFF),
        (uint8_t)((times_8 - 1) >> 8),
    };
    if (ftdi_write_data(ftdi, buf, 3) != 3) {
      printf("Error: %s\n", ftdi_get_error_string(ftdi));
      return false;
    }
  }

  if (times % 8) {
    // Clock For n bits with no data transfer
    uint8_t buf[256] = {
        0x8E,
        (uint8_t)((times % 8) - 1),
    };
    if (ftdi_write_data(ftdi, buf, 2) != 2) {
      printf("Error: %s\n", ftdi_get_error_string(ftdi));
      return false;
    }
  }
  return true;
}

std::vector<Region> analyze_bitbang(const uint8_t *tms, size_t bits,
                                    JtagState &cur_state) {
  int shift_pos = 0;
  std::vector<Region> regions;
  cur_state = state;
  for (int i = 0; i < bits; i++) {
    uint8_t tms_bit = (tms[i / 8] >> (i % 8)) & 0x1;
    JtagState new_state = next_state(cur_state, tms_bit);
    if ((cur_state != ShiftDR && new_state == ShiftDR) ||
        (cur_state != ShiftIR && new_state == ShiftIR)) {
      Region region;
      region.is_tms = true;
      region.begin = shift_pos;
      region.end = i + 1;
      regions.push_back(region);

      shift_pos = i + 1;
    } else if ((cur_state == ShiftDR && new_state != ShiftDR) ||
               (cur_state == ShiftIR && new_state != ShiftIR)) {
      // end
      Region region;
      region.is_tms = false;
      region.flip_tms = true;
      region.begin = shift_pos;
      region.end = i + 1;
      regions.push_back(region);

      shift_pos = i + 1;
    }
    cur_state = new_state;
  }

  if (shift_pos != bits) {
    Region region;
    region.is_tms = cur_state != ShiftDR && cur_state != ShiftIR;
    region.flip_tms = false;
    region.begin = shift_pos;
    region.end = bits;
    regions.push_back(region);
  }
  return regions;
}