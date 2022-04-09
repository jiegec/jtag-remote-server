#ifndef __COMMON_H__
#define __COMMON_H__

#include <algorithm>
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <ftdi.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <unistd.h>
#include <vector>

extern struct ftdi_context *ftdi;
extern int listen_fd;
extern int client_fd;

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

JtagState next_state(JtagState cur, int bit);
const char *state_to_string(JtagState state);

bool mpsse_init();
bool jtag_tms_seq(uint8_t *data, size_t num_bits);
bool jtag_fsm_reset();
void print_bitvec(uint8_t *data, size_t bits);

#endif