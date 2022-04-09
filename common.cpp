#include "common.h"
#include <assert.h>

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
  }
}

bool mpsse_init() {
  ftdi_set_bitmode(ftdi, 0, BITMODE_MPSSE);

  uint8_t setup[256] = {SET_BITS_LOW,  0x88, 0x8b, TCK_DIVISOR,   0x01, 0x00,
                        SET_BITS_HIGH, 0,    0,    SEND_IMMEDIATE};
  if (ftdi_write_data(ftdi, setup, 10) != 10) {
    printf("error: %s\n", ftdi_get_error_string(ftdi));
    return false;
  }

  return true;
}

bool jtag_fsm_reset() {
  // 11111: Goto Test-Logic-Reset
  uint8_t tms[] = {0x1F};
  return jtag_tms_seq(tms, 5);
}

bool jtag_tms_seq(uint8_t *data, size_t num_bits) {
  printf("Sending TMS Seq ");
  print_bitvec(data, num_bits);
  printf("\n");

  for (size_t i = 0; i < (num_bits + 7) / 8; i++) {
    size_t cur_bits = std::min((size_t)8, num_bits - i * 8);

    // Clock Data to TMS pin (no read)
    uint8_t idle[256] = {MPSSE_WRITE_TMS | MPSSE_LSB | MPSSE_BITMODE |
                             MPSSE_WRITE_NEG,
                         // length in bits -1
                         cur_bits - 1,
                         // data
                         data[i]};
    if (ftdi_write_data(ftdi, idle, 3) != 3) {
      printf("error: %s\n", ftdi_get_error_string(ftdi));
      return false;
    }
  }

  return true;
}

void print_bitvec(uint8_t *data, size_t bits) {
  for (size_t i = 0; i < bits; i++) {
    int off = i % 8;
    int bit = ((data[i / 8]) >> off) & 1;
    printf("%c", bit ? '1' : '0');
  }
  printf("(0x");
  int bytes = (bits + 7) / 8;
  for (int i = 0; i < bytes; i++) {
    printf("%02X", data[i]);
  }
  printf(")");
}
