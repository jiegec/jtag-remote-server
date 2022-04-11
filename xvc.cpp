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

bool jtag_xvc_init() {
  if (!setup_tcp_server(2542)) {
    return false;
  }

  printf("Start xvc server at :2542\n");
  return true;
}

const int BUFFER_SIZE = 4096;
// read socket buffer
char buffer[BUFFER_SIZE];
size_t buffer_begin = 0;
size_t buffer_end = 0;

char tms[BUFFER_SIZE];
char tdi[BUFFER_SIZE];
uint8_t tdo[BUFFER_SIZE] = {};
void jtag_xvc_tick() {
  if (client_fd >= 0) {
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
        return;
      } else if (num_read > 0) {
        buffer_end += num_read;
      }
    }

    // parse & execute commands
    while (true) {
      static size_t getinfo_len = strlen("getinfo:");
      static size_t settck_len = strlen("settck:");
      static size_t shift_len = strlen("shift:");
      if (buffer_begin + getinfo_len <= buffer_end &&
          memcmp(&buffer[buffer_begin], "getinfo:", getinfo_len) == 0) {
        // getinfo
        dprintf("getinfo:\n");
        buffer_begin += getinfo_len;
        char info[] = "xvcServer_v1.0:2048\n";
        assert(write_full(client_fd, (uint8_t *)info, strlen(info)));
      } else if (buffer_begin + settck_len + sizeof(uint32_t) <= buffer_end &&
                 memcmp(&buffer[buffer_begin], "settck:", settck_len) == 0) {
        dprintf("settck:");

        uint32_t tck = 0;
        memcpy(&tck, &buffer[buffer_begin + settck_len], sizeof(uint32_t));
        dprintf("%d\n", tck);
        buffer_begin += settck_len + sizeof(uint32_t);

        assert(write_full(client_fd, (uint8_t *)&tck, sizeof(tck)));
      } else if (buffer_begin + shift_len + sizeof(uint32_t) <= buffer_end &&
                 memcmp(&buffer[buffer_begin], "shift:", shift_len) == 0) {
        dprintf("shift:\n");

        uint32_t bits = 0;
        memcpy(&bits, &buffer[buffer_begin + shift_len], sizeof(uint32_t));

        uint32_t bytes = (bits + 7) / 8;
        uint32_t total_len = shift_len + sizeof(uint32_t) + 2 * bytes;
        if (buffer_begin + total_len > buffer_end) {
          break;
        }
        memcpy(tms, &buffer[buffer_begin + shift_len + sizeof(uint32_t)],
               bytes);
        memcpy(tdi,
               &buffer[buffer_begin + shift_len + sizeof(uint32_t) + bytes],
               bytes);
        buffer_begin += total_len;

        memset(tdo, 0, bytes);
        dprintf(" tms:");
        print_bitvec((unsigned char *)tms, bits);
        dprintf("\n");
        dprintf(" tdi:");
        print_bitvec((unsigned char *)tdi, bits);
        dprintf("\n");

        // send tms & read
        JtagState cur_state = state;
        std::vector<Region> regions =
            analyze_bitbang((uint8_t *)tms, bits, cur_state);

        for (auto region : regions) {
          assert(region.begin < region.end && region.end <= bits);
          dprintf("[%d:%d]: %s\n", region.begin, region.end,
                  region.is_tms ? "TMS" : "DATA");
          if (region.is_tms) {
            uint8_t tms_buffer[BUFFER_SIZE] = {};
            // optimize runtest with a large number of cycles
            bool clock_only = state == JtagState::RunTestIdle;
            for (int i = region.begin; i < region.end; i++) {
              uint8_t tms_bit = (tms[i / 8] >> (i % 8)) & 0x1;
              int off = i - region.begin;
              tms_buffer[off / 8] |= tms_bit << (off % 8);
              if (tms_bit) {
                clock_only = false;
              }
            }
            if (clock_only) {
              jtag_tms_seq(tms_buffer, 1);
              jtag_clock_tck(region.length() - 1);
            } else {
              jtag_tms_seq(tms_buffer, region.length());
            }
          } else {
            uint8_t tdi_buffer[BUFFER_SIZE] = {};
            for (int i = region.begin; i < region.end; i++) {
              uint8_t tdi_bit = (tdi[i / 8] >> (i % 8)) & 0x1;
              int off = i - region.begin;
              tdi_buffer[off / 8] |= tdi_bit << (off % 8);
            }

            // always read
            uint8_t tdo_buffer[BUFFER_SIZE] = {};
            jtag_scan_chain(tdi_buffer, tdo_buffer, region.length(),
                            region.flip_tms, true);

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
        // can not parse
        break;
      }
    }
  } else {
    try_accept();
  }
}