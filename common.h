#ifndef __COMMON_H__
#define __COMMON_H__

#include <ftdi.h>
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

JtagState next_state(JtagState cur, int bit);
const char *state_to_string(JtagState state);

#endif