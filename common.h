#ifndef __COMMON_H__
#define __COMMON_H__

#include <algorithm>
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

extern int listen_fd;
extern int client_fd;
extern bool debug;

enum JtagState {
  TestLogicReset,
  RunTestIdle,
  SelectDRScan,
  CaptureDR,
  ShiftDR,
  Exit1DR,
  PauseDR,
  Exit2DR,
  UpdateDR,
  SelectIRScan,
  CaptureIR,
  ShiftIR,
  Exit1IR,
  PauseIR,
  Exit2IR,
  UpdateIR,
};

extern JtagState state;
extern uint64_t bits_send;
extern uint64_t freq_mhz;

// jtag state transition
JtagState next_state(JtagState cur, int bit);
const char *state_to_string(JtagState state);

// jtag operations
bool jtag_tms_seq(const uint8_t *data, size_t num_bits);
bool jtag_scan_chain(const uint8_t *data, uint8_t *recv, size_t num_bits,
                     bool flip_tms, bool do_read);
bool jtag_scan_chain_send(const uint8_t *data, size_t num_bits, bool flip_tms,
                          bool do_read);
bool jtag_scan_chain_recv(uint8_t *recv, size_t num_bits, bool flip_tms);
bool jtag_clock_tck(size_t times);
bool jtag_goto_tlr();
void jtag_get_tms_seq(JtagState from, JtagState to, uint8_t &tms, size_t &num_bits);
bool jtag_tms_seq_to(JtagState to);
std::vector<uint32_t> jtag_probe_devices();

// debug related
void print_bitvec(const uint8_t *data, size_t bits);
void dprintf(const char *fmt, ...);

// tcp replated
bool write_full(int fd, const uint8_t *data, size_t count);
bool setup_tcp_server(uint16_t port);
bool try_accept();

// analyze regions from bitbang sequence
struct Region {
  bool is_tms;
  bool flip_tms;
  int begin;
  int end;

  int length() const {
    assert(begin < end);
    return end - begin;
  }
};

std::vector<Region> analyze_bitbang(const uint8_t *tms, size_t bits,
                                    JtagState &cur_state);

// read socket buffer
const int BUFFER_SIZE = 4096;
extern uint8_t buffer[BUFFER_SIZE];
extern size_t buffer_begin;
extern size_t buffer_end;

bool read_socket();

#endif