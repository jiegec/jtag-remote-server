#include "common.h"

bool jtag_rbb_init() {
  if (!setup_tcp_server(12345)) {
    return false;
  }

  printf("Start remote bitbang server at :12345\n");
  return true;
}

const int BUFFER_SIZE = 4096;

void jtag_rbb_tick() {
  if (client_fd >= 0) {
    char tms_input[BUFFER_SIZE];
    char tdi_input[BUFFER_SIZE];
    char read_input[BUFFER_SIZE];
    char read_buffer[BUFFER_SIZE];
    char send_buffer[BUFFER_SIZE];

    ssize_t num_read = read(client_fd, read_buffer, sizeof(read_buffer));
    if (num_read <= 0) {
      // remote socket closed
      printf("JTAG debugger detached\n");
      close(client_fd);
      client_fd = -1;
      return;
    }

    size_t bits = 0;
    size_t read_bits = 0;
    memset(tms_input, 0, (num_read + 7) / 8);
    memset(tdi_input, 0, (num_read + 7) / 8);
    memset(read_input, 0, (num_read + 7) / 8);
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
        // NOTE: We made assumption of when OpenOCD sends the 'R' command
        read_input[bits / 8] |= 1 << (bits % 8);
        read_bits++;
      } else if (command == 'r' || command == 's') {
        // trst = 0;
      } else if (command == 't' || command == 'u') {
        // trst = 1;
      }
    }

    dprintf(" tms:");
    print_bitvec((unsigned char *)tms_input, bits);
    dprintf("\n");
    dprintf(" tdi:");
    print_bitvec((unsigned char *)tdi_input, bits);
    dprintf("\n");
    dprintf("read:");
    print_bitvec((unsigned char *)read_input, bits);
    dprintf("\n");

    JtagState cur_state = state;
    std::vector<Region> regions =
        analyze_bitbang((uint8_t *)tms_input, bits, cur_state);

    uint32_t actual_read_bits = 0;
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
        jtag_tms_seq(tms_buffer, region.length());
      } else {
        uint8_t tdi_buffer[BUFFER_SIZE] = {};
        bool do_read = false;
        for (int i = region.begin; i < region.end; i++) {
          uint8_t tdi_bit = (tdi_input[i / 8] >> (i % 8)) & 0x1;
          int off = i - region.begin;
          tdi_buffer[off / 8] |= tdi_bit << (off % 8);

          uint8_t read_bit = (read_input[i / 8] >> (i % 8)) & 0x1;
          if (read_bit) {
            do_read = true;
          }
          // verify our assumption: all read_bit remains the same
          assert(do_read == read_bit);
        }

        uint8_t tdo_buffer[BUFFER_SIZE] = {};
        jtag_scan_chain(tdi_buffer, tdo_buffer, region.length(),
                        region.flip_tms, do_read);

        // handle read
        if (do_read) {
          actual_read_bits += region.length();
          for (int i = 0; i < region.length(); i++) {
            uint8_t tdo_bit = (tdo_buffer[i / 8] >> (i % 8)) & 0x1;

            send_buffer[i] = tdo_bit ? '1' : '0';
          }

          write_full(client_fd, (uint8_t *)send_buffer, region.length());
        }
      }
    }
    assert(cur_state == state);
    assert(read_bits == actual_read_bits);
  } else {
    // accept connection
    try_accept();
  }
}