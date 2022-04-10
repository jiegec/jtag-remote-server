#include "common.h"
#include <algorithm>
#include <assert.h>
#include <fcntl.h>
#include <ftdi.h>
#include <memory.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// https://github.com/derekmulcahy/xvcpi/blob/e4df3cd5eaa6ca248b93b0c076ed21503d0abaf9/xvcpi.c#L147
static bool sread(int fd, char *target, int len) {
  char *t = target;
  while (len) {
    int r = read(fd, t, len);
    if (r > 0) {
      t += r;
      len -= r;
    } else if (r == 0) {
      // socket disconnected
      return false;
    }
  }
  return true;
}

static int swrite(int fd, char *target, int len) {
  char *t = target;
  while (len) {
    int r = write(fd, t, len);
    if (r <= 0)
      return r;
    t += r;
    len -= r;
  }
  return 0;
}

struct Region {
  bool is_tms;
  bool flip_tms;
  int begin;
  int end;
};

void jtag_xvc_init() {
  ftdi_set_interface(ftdi, INTERFACE_A);
  ftdi_set_bitmode(ftdi, 0, BITMODE_MPSSE);

  // set clock and initial state
  uint8_t setup[256] = {SET_BITS_LOW,  0x88, 0x8b, TCK_DIVISOR,   0x01, 0x00,
                        SET_BITS_HIGH, 0,    0,    SEND_IMMEDIATE};
  if (ftdi_write_data(ftdi, setup, 10) != 10) {
    printf("Error: %s\n", ftdi_get_error_string(ftdi));
    return;
  }

  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(listen_fd >= 0);

  int reuseaddr = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int)) <
      0) {
    perror("setsockopt");
    return;
  }

  sockaddr_in listen_addr = {};
  listen_addr.sin_addr.s_addr = INADDR_ANY;
  listen_addr.sin_port = htons(2542);
  listen_addr.sin_family = AF_INET;

  int res = bind(listen_fd, (sockaddr *)&listen_addr, sizeof(listen_addr));
  assert(res >= 0);

  res = listen(listen_fd, 0);
  assert(res >= 0);
  printf("Start xvc server at :2542\n");
}

const int BUFFER_SIZE = 4096;
void jtag_xvc_tick() {
  if (client_fd >= 0) {
    char buffer[BUFFER_SIZE];
    char tms[BUFFER_SIZE];
    char tdi[BUFFER_SIZE];
    uint8_t tdo[BUFFER_SIZE] = {};

    if (!sread(client_fd, buffer, 2)) {
      // remote socket closed
      printf("JTAG debugger detached\n");
      close(client_fd);
      client_fd = -1;
      return;
    }

    if (memcmp(buffer, "ge", 2) == 0) {
      // getinfo
      dprintf("getinfo:\n");
      assert(sread(client_fd, buffer, strlen("tinfo:")));

      char info[] = "xvcServer_v1.0:2048\n";
      assert(swrite(client_fd, (char *)info, strlen(info)) >= 0);
    } else if (memcmp(buffer, "se", 2) == 0) {
      dprintf("settck:");
      assert(sread(client_fd, buffer, strlen("ttck:")));

      uint32_t tck = 0;
      assert(sread(client_fd, (char *)&tck, sizeof(tck)));
      dprintf("%d\n", tck);

      assert(swrite(client_fd, (char *)&tck, sizeof(tck)) >= 0);
    } else if (memcmp(buffer, "sh", 2) == 0) {
      dprintf("shift:\n");
      assert(sread(client_fd, buffer, strlen("ift:")));

      uint32_t bits = 0;
      assert(sread(client_fd, (char *)&bits, sizeof(bits)));

      uint32_t bytes = (bits + 7) / 8;
      assert(sread(client_fd, tms, bytes));
      assert(sread(client_fd, tdi, bytes));
      dprintf(" tms:");
      print_bitvec((unsigned char *)tms, bits);
      dprintf("\n");
      dprintf(" tdi:");
      print_bitvec((unsigned char *)tdi, bits);
      dprintf("\n");

      // send tms & read
      int shift_pos = 0;
      std::vector<Region> regions;
      JtagState cur_state = state;
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

      Region region;
      region.is_tms = state != ShiftDR && state != ShiftIR;
      region.flip_tms = false;
      region.begin = shift_pos;
      region.end = bits;
      regions.push_back(region);

      for (auto region : regions) {
        assert(region.begin < region.end && region.end <= bits);
        dprintf("[%d:%d]: %s\n", region.begin, region.end,
                region.is_tms ? "TMS" : "DATA");
        if (region.is_tms) {
          uint8_t tms_buffer[BUFFER_SIZE] = {};
          for (int i = region.begin; i < region.end; i++) {
            uint8_t tms_bit = (tms[i / 8] >> (i % 8)) & 0x1;
            int off = i - region.begin;
            tms_buffer[off / 8] |= tms_bit << (off % 8);
          }
          jtag_tms_seq(tms_buffer, region.end - region.begin);
        } else {
          uint8_t tdi_buffer[BUFFER_SIZE] = {};
          for (int i = region.begin; i < region.end; i++) {
            uint8_t tdi_bit = (tdi[i / 8] >> (i % 8)) & 0x1;
            int off = i - region.begin;
            tdi_buffer[off / 8] |= tdi_bit << (off % 8);
          }

          uint8_t tdo_buffer[BUFFER_SIZE] = {};
          jtag_scan_chain(tdi_buffer, tdo_buffer, region.end - region.begin,
                          region.flip_tms);

          for (int i = region.begin; i < region.end; i++) {
            int off = i - region.begin;
            uint8_t tdo_bit = (tdo_buffer[off / 8] >> (off % 8)) & 0x1;
            tdo[i / 8] |= tdo_bit << (i % 8);
          }
        }
      }
      assert(cur_state == state);

      dprintf(" tdo:");
      print_bitvec(tdo, bits);
      dprintf("\n");
      assert(swrite(client_fd, (char *)tdo, bytes) >= 0);
    } else {
      printf("Unsupported command\n");
      close(client_fd);
      client_fd = -1;
    }

    fflush(stdout);
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
    }
  }
}