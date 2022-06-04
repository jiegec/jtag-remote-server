#include "common.h"
#include "mpsse.h"
#include <assert.h>
#include <stdarg.h>

driver *adapter = &mpsse_driver;

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
    return "";
  }
}

bool jtag_goto_tlr() {
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

  return adapter->jtag_tms_seq(data, num_bits);
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
  if (!jtag_scan_chain_send(data, num_bits, flip_tms, do_read)) {
    return false;
  }
  if (do_read) {
    if (!jtag_scan_chain_recv(recv, num_bits, flip_tms)) {
      return false;
    }

    dprintf("Read TDO %d bits: ", num_bits);
    print_bitvec(recv, num_bits);
    dprintf("\n");
  }
  return true;
}

bool jtag_scan_chain_send(const uint8_t *data, size_t num_bits, bool flip_tms,
                          bool do_read) {
  bits_send += num_bits;
  dprintf("Write TDI%s %d bits: ", flip_tms ? "+TMS" : "", num_bits);
  print_bitvec(data, num_bits);
  dprintf("\n");

  return adapter->jtag_scan_chain_send(data, num_bits, flip_tms, do_read);
}

bool jtag_scan_chain_recv(uint8_t *recv, size_t num_bits, bool flip_tms) {
  return adapter->jtag_scan_chain_recv(recv, num_bits, flip_tms);
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
  return adapter->jtag_clock_tck(times);
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

bool read_socket() {
  if (buffer_begin > 0 && buffer_end == BUFFER_SIZE) {
    // buffer is full, move to zero
    memmove(buffer, &buffer[buffer_begin], buffer_end - buffer_begin);
    buffer_end -= buffer_begin;
    buffer_begin = 0;
  } else if (buffer_begin == buffer_end) {
    // buffer is empty
    buffer_begin = 0;
    buffer_end = 0;
  }

  if (buffer_end < BUFFER_SIZE) {
    // buffer is not full, read something
    ssize_t num_read =
        read(client_fd, &buffer[buffer_begin], BUFFER_SIZE - buffer_end);
    if (num_read == 0) {
      // remote socket closed
      printf("JTAG debugger detached\n");
      close(client_fd);
      client_fd = -1;
      buffer_begin = 0;
      buffer_end = 0;
      return false;
    } else if (num_read > 0) {
      buffer_end += num_read;
    }
  }

  return true;
}

std::vector<uint32_t> jtag_probe_devices() {
  // list of detected idcode
  std::vector<uint32_t> res;

  // find the number of devices in the daisy chain
  // https://www.fpga4fun.com/JTAG3.html
  // step 1: go to test-logic-reset
  jtag_goto_tlr();

  // step 2: go to shift-ir 01100
  jtag_tms_seq_to(JtagState::ShiftIR);

  // step 3: send plenty of ones into ir register
  const int MAX_TAPS = 8;
  const int MAX_IR = 16;
  uint8_t ones[MAX_TAPS * MAX_IR / 8];
  memset(ones, 0xFF, sizeof(ones));
  jtag_scan_chain_send(ones, MAX_TAPS * MAX_IR, true, false);

  // step 4: go from exit1-ir to shift-dr 1100
  jtag_tms_seq_to(JtagState::ShiftDR);

  // step 5: send plenty of zeros into dr register
  uint8_t zeros[MAX_TAPS / 8];
  memset(zeros, 0, sizeof(zeros));
  jtag_scan_chain_send(zeros, MAX_TAPS, false, false);

  // step 6: send plenty of ones again into dr register
  uint8_t ones2[MAX_TAPS / 8];
  memset(ones2, 0xFF, sizeof(ones2));
  uint8_t read_buffer[MAX_TAPS / 8];
  jtag_scan_chain(ones2, read_buffer, MAX_TAPS, true, true);

  size_t num_devices = 0;
  for (int i = 0; i < MAX_TAPS; i++) {
    int read_bit = (read_buffer[i / 8] >> (i % 8)) & 1;
    if (read_bit) {
      num_devices = i;
      break;
    }
  }

  dprintf("Found %zu devices on the chain\n", num_devices);
  if (num_devices == 0) {
    return res;
  }

  // step 7: restore test logic reset
  // now all device has ir = idcode
  jtag_goto_tlr();

  // step 8: go from test-logic-reset to shift-dr 0100
  uint8_t shift_dr2_tms[] = {0x02};
  jtag_tms_seq(shift_dr2_tms, 4);

  // step 9: read id out
  uint8_t zeros2[MAX_TAPS * 32 / 8];
  memset(zeros2, 0, sizeof(zeros2));
  uint8_t read_buffer2[MAX_TAPS * 32 / 8];
  jtag_scan_chain(zeros2, read_buffer2, MAX_TAPS * 32, true, true);

  // collect idcode
  for (int i = 0; i < num_devices; i++) {
    uint32_t idcode = (read_buffer2[i * 4]) | (read_buffer2[i * 4 + 1] << 8) |
                      (read_buffer2[i * 4 + 2] << 16) |
                      (read_buffer2[i * 4 + 3] << 24);
    res.push_back(idcode);
    dprintf("Device %d has IDCODE=0x%08X\n", i, idcode);
  }

  // step 7: restore test logic reset
  jtag_goto_tlr();

  return res;
}

void jtag_get_tms_seq(JtagState from, JtagState to, uint8_t &tms,
                      size_t &num_bits) {
  if (from == to) {
    tms = 0;
    num_bits = 0;
    return;
  }

  if (from == JtagState::TestLogicReset) {
    if (to == JtagState::ShiftIR) {
      // go to shift-ir 01100
      tms = 0x06;
      num_bits = 5;
      return;
    } else if (to == JtagState::ShiftDR) {
      // go to shift-dr 0100
      tms = 0x02;
      num_bits = 4;
      return;
    }
  } else if (from == JtagState::RunTestIdle) {
    if (to == JtagState::ShiftIR) {
      // go to shift-ir 1100
      tms = 0x3;
      num_bits = 4;
      return;
    } else if (to == JtagState::ShiftDR) {
      // go to shift-dr 100
      tms = 0x1;
      num_bits = 3;
      return;
    }
  } else if (from == JtagState::Exit1IR) {
    if (to == JtagState::ShiftDR) {
      // from exit1-ir to shift-dr 1100
      tms = 0x3;
      num_bits = 4;
      return;
    } else if (to == JtagState::RunTestIdle) {
      // from exit1-ir to run-test-idle 10
      tms = 0x1;
      num_bits = 2;
      return;
    }
  } else if (from == JtagState::Exit1DR) {
    if (to == JtagState::RunTestIdle) {
      // from exit1-dr to run-test-idle 10
      tms = 0x1;
      num_bits = 2;
      return;
    }
  }
  assert(false);
  return;
}

bool jtag_tms_seq_to(JtagState to) {
  uint8_t tms;
  size_t num_bits;
  jtag_get_tms_seq(state, to, tms, num_bits);
  if (num_bits > 0) {
    return jtag_tms_seq(&tms, num_bits);
  } else {
    return true;
  }
}

bool adapter_init() { return adapter->init(); }

bool adapter_deinit() { return adapter->deinit(); }

bool adapter_set_tck_freq(uint64_t freq_mhz) {
  return adapter->set_tck_freq(freq_mhz);
}

bool ftdi_read_retry(struct ftdi_context *ftdi, uint8_t *data, size_t len) {
  int retry = 100;
  size_t offset = 0;
  while (offset < len && (retry > 0)) {
    int res = ftdi_read_data(ftdi, &data[offset], len - offset);
    if (res < 0) {
      printf("Error @ %s:%d : %s\n", __FILE__, __LINE__,
             ftdi_get_error_string(ftdi));
      return false;
    }
    offset += res;
    retry--;
  }

  return offset == len;
}