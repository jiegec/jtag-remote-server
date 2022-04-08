#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <ftdi.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <unistd.h>

int listen_fd = -1;
int client_fd = -1;
struct ftdi_context *ftdi = NULL;

int jtag_rbb_init() {
  // ref rocket chip remote_bitbang.cc
  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket");
    return -1;
  }

  // set non blocking
  fcntl(listen_fd, F_SETFL, O_NONBLOCK);

  int reuseaddr = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int)) <
      0) {
    perror("setsockopt");
    return -1;
  }

  int port = 12345;
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    return -1;
  }

  if (listen(listen_fd, 1) == -1) {
    perror("listen");
    return -1;
  }
  fprintf(stderr, "Remote bitbang server listening at :12345\n");

  return 0;
}

void jtag_write(int tck, int tms, int tdi) {
  if (ftdi) {
    unsigned char buf[] = {
        (unsigned char)((tck << 0) | (tdi << 1) | (tms << 3))};
    while (ftdi_write_data(ftdi, buf, 1) != 1)
      ;
    printf("Write TCK=%d TMS=%d TDI=%d\n", tck, tms, tdi);
  }
}

int jtag_read() {
  unsigned char buf[1] = {};
  if (ftdi) {
    while (ftdi_read_data(ftdi, buf, 1) != 1)
      ;
    int res = (buf[0] >> 2) & 1;
    printf("Read %d(%x)\n", res, buf[0]);
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
        fprintf(stderr, "JTAG debugger detached\n");
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
      fprintf(stderr, "JTAG debugger attached\n");
    }
  }
}

void jtag_init() {
  ftdi = ftdi_new();
  assert(ftdi);

  int ret = ftdi_set_interface(ftdi, INTERFACE_B);
  assert(ret == 0);
  ret = ftdi_usb_open(ftdi, 0x0403, 0x6011);
  assert(ret == 0);
  // D0, D1, D3 output, D2 input 0b1011
  ret = ftdi_set_bitmode(ftdi, 0x0b, BITMODE_BITBANG);
  assert(ret == 0);
}

int main(int argc, char *argv[]) {
  jtag_init();
  jtag_rbb_init();
  for (;;) {
    jtag_rbb_tick();
  }
  return 0;
}