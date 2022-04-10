#include "common.h"

bool jtag_rbb_init() {
  if (!setup_tcp_server(12345)) {
    return false;
  }

  printf("Start remote bitbang server at :12345\n");

  mpsse_init();
  return true;
}

const int BUFFER_SIZE = 4096;

void jtag_rbb_tick() {
  if (client_fd >= 0) {
    char tms_input[BUFFER_SIZE];
    char tdi_input[BUFFER_SIZE];
    char read_buffer[BUFFER_SIZE];

    ssize_t num_read = read(client_fd, read_buffer, sizeof(read_buffer));
    if (num_read <= 0) {
      // remote socket closed
      printf("JTAG debugger detached\n");
      close(client_fd);
      client_fd = -1;
      return;
    }

    size_t bits = 0;
    memset(tms_input, 0, (num_read + 7) / 8);
    memset(tdi_input, 0, (num_read + 7) / 8);
    for (int i = 0; i < num_read; i++) {
      char command = read_buffer[i];
      if ('0' <= command && command <= '7') {
        // set
        char offset = command - '0';
        int tck = (offset >> 2) & 1;
        int tms = (offset >> 1) & 1;
        int tdi = (offset >> 0) & 1;
        if (tck) {
          tms_input[bits / 8] |= tms << (bits % 8);
          tdi_input[bits / 8] |= tdi << (bits % 8);
          bits++;
        }
      } else if (command == 'R') {
        // read
      } else if (command == 'r' || command == 's') {
        // trst = 0;
      } else if (command == 't' || command == 'u') {
        // trst = 1;
      }
    }

    JtagState cur_state = state;
    std::vector<Region> regions =
        analyze_bitbang((uint8_t *)tms_input, bits, cur_state);

    for (auto region : regions) {
      assert(region.begin < region.end && region.end <= bits);
      dprintf("[%d:%d]: %s\n", region.begin, region.end,
              region.is_tms ? "TMS" : "DATA");
      if (region.is_tms) {
        uint8_t tms_buffer[BUFFER_SIZE] = {};
        for (int i = region.begin; i < region.end; i++) {
          uint8_t tms_bit = (tms_input[i / 8] >> (i % 8)) & 0x1;
          int off = i - region.begin;
          tms_buffer[off / 8] |= tms_bit << (off % 8);
        }
        jtag_tms_seq(tms_buffer, region.end - region.begin);
      } else {
        uint8_t tdi_buffer[BUFFER_SIZE] = {};
        for (int i = region.begin; i < region.end; i++) {
          uint8_t tdi_bit = (tdi_input[i / 8] >> (i % 8)) & 0x1;
          int off = i - region.begin;
          tdi_buffer[off / 8] |= tdi_bit << (off % 8);
        }

        uint8_t tdo_buffer[BUFFER_SIZE] = {};
        jtag_scan_chain(tdi_buffer, tdo_buffer, region.end - region.begin,
                        region.flip_tms);

        for (int i = 0; i < region.end - region.begin; i++) {
          uint8_t tdo_bit = (tdo_buffer[i / 8] >> (i % 8)) & 0x1;

          char send = tdo_bit ? '1' : '0';

          while (1) {
            ssize_t sent = write(client_fd, &send, sizeof(send));
            if (sent > 0) {
              break;
            } else if (send < 0) {
              close(client_fd);
              client_fd = -1;
              break;
            }
          }
        }
      }
    }
    assert(cur_state == state);
  } else {
    // accept connection
    try_accept();
  }
}