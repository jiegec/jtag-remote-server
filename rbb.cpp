#include "common.h"

bool jtag_rbb_init() {
  if (!setup_tcp_server(12345)) {
    return false;
  }
  printf("Start remote bitbang server at :12345\n");

  return true;
}

std::vector<unsigned char> jtag_write_buffer;
unsigned char jtag_last_write_data;

void jtag_write_xfer() {
  int written = 0;
  while (written < jtag_write_buffer.size()) {
    written += ftdi_write_data(ftdi, &jtag_write_buffer[written],
                               jtag_write_buffer.size() - written);
  }

  int read = 0;
  while (read < jtag_write_buffer.size()) {
    read += ftdi_read_data(ftdi, &jtag_write_buffer[read],
                           jtag_write_buffer.size() - read);
  }
}

void jtag_write(int tck, int tms, int tdi) {
  if (tck) {
    JtagState new_state = next_state(state, tms);
    if (new_state != state) {
      dprintf("%s -> %s\n", state_to_string(state), state_to_string(new_state));
    }
    state = new_state;
  }

  if (ftdi) {
    unsigned char data = (tck << 0) | (tdi << 1) | (tms << 3);
    jtag_last_write_data = data;
    jtag_write_buffer.push_back(data);

    if (jtag_write_buffer.size() >= 16) {
      jtag_write_xfer();
      jtag_write_buffer.clear();
    }
  }
}

int jtag_read() {
  unsigned char buf[1] = {};
  if (ftdi) {
    jtag_write_buffer.push_back(jtag_last_write_data);
    jtag_write_xfer();

    int res = (jtag_write_buffer[jtag_write_buffer.size() - 1] >> 2) & 1;

    jtag_write_buffer.clear();
    return res;
  } else {
    return 0;
  }
}

void jtag_rbb_tick() {
  if (client_fd >= 0) {
    static char read_buffer[128];
    static size_t read_buffer_count = 0;
    static size_t read_buffer_offset = 0;

    if (read_buffer_offset == read_buffer_count) {
      ssize_t num_read = read(client_fd, read_buffer, sizeof(read_buffer));
      if (num_read > 0) {
        read_buffer_count = num_read;
        read_buffer_offset = 0;
      } else if (num_read == 0) {
        // remote socket closed
        printf("JTAG debugger detached\n");
        close(client_fd);
        client_fd = -1;
      }
    }

    if (read_buffer_offset < read_buffer_count) {
      char command = read_buffer[read_buffer_offset++];
      if ('0' <= command && command <= '7') {
        // set
        char offset = command - '0';
        jtag_write((offset >> 2) & 1, (offset >> 1) & 1, (offset >> 0) & 1);
      } else if (command == 'R') {
        // read
        char send = jtag_read() ? '1' : '0';

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
      } else if (command == 'r' || command == 's') {
        // trst = 0;
      } else if (command == 't' || command == 'u') {
        // trst = 1;
      }
    }
  } else {
    // accept connection
    client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd > 0) {
      fcntl(client_fd, F_SETFL, O_NONBLOCK);

      // set nodelay
      int flags = 1;
      if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, (void *)&flags,
                     sizeof(flags)) < 0) {
        perror("setsockopt");
      }
      printf("JTAG debugger attached\n");

      // D0, D1, D3 output, D2 input 0b1011
      int ret = ftdi_set_bitmode(ftdi, 0x0b, BITMODE_SYNCBB);
      assert(ret == 0);
    }
  }
}