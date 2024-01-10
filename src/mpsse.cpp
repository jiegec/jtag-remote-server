#include "mpsse.h"
#include "common.h"
#include "mpsse_buffer.h"
#include <algorithm>
#include <ftdi.h>

static struct ftdi_context *ftdi;

bool mpsse_init(enum AdapterTypes adapter_type) {
  printf("Initialize ftdi\n");
  ftdi = ftdi_new();
  assert(ftdi);

  printf("Use channel %c\n", (int)ftdi_channel - 1 + 'A');
  int ret = ftdi_set_interface(ftdi, ftdi_channel);
  if (ret) {
    printf("Error @ %s:%d : %s\n", __FILE__, __LINE__,
           ftdi_get_error_string(ftdi));
    return false;
  }

  printf("Open device vid=0x%04x pid=0x%04x\n", ftdi_vid, ftdi_pid);
  ret = ftdi_usb_open(ftdi, ftdi_vid, ftdi_pid);
  if (ret) {
    printf("Error @ %s:%d : %s\n", __FILE__, __LINE__,
           ftdi_get_error_string(ftdi));
    return false;
  }

  ret = ftdi_usb_reset(ftdi);
  assert(ret == 0);
  ret = ftdi_set_baudrate(ftdi, 115200);
  assert(ret == 0);
  // set maximum latency to avoid usb bulk write errors
  ret = ftdi_set_latency_timer(ftdi, 255);
  assert(ret == 0);

  // reset mpsse and enable
  printf("Enable mpsse\n");
  ftdi_set_bitmode(ftdi, 0, 0);
  ftdi_set_bitmode(ftdi, 0, BITMODE_MPSSE);
  
  uint16_t initialOutput = 0;
  uint16_t direction = 0;
  switch(adapter_type) {
  case Adapter_Xilinx:
    initialOutput = 0x0088;
    direction = 0x008b;
    break;
  case Adapter_DigilentHS2:
    initialOutput = 0x00e8;
    direction = 0x60eb;
    break;
  default:
    printf("Unhandled adapter %d\n", adapter_type);
    break;
  }

  uint8_t setup[256] = {SET_BITS_LOW,
                        (uint8_t)(initialOutput & 0xff),
                        (uint8_t)(direction & 0xff),
                        SET_BITS_HIGH,
                        (uint8_t)(initialOutput >> 8),
                        (uint8_t)(direction >> 8),
                        SEND_IMMEDIATE};
  if (!ftdi_write_retry(ftdi, setup, 7)) {
    printf("Error @ %s:%d : %s\n", __FILE__, __LINE__,
           ftdi_get_error_string(ftdi));
    return false;
  }

  mpsse_buffer_init(ftdi);

  if (!mpsse_set_tck_freq(freq_mhz)) {
    return false;
  }

  return true;
}

bool mpsse_deinit() {
  ftdi_set_bitmode(ftdi, 0, 0);
  return true;
}

bool mpsse_jtag_tms_seq(const uint8_t *data, size_t num_bits) {
  for (size_t i = 0; i < (num_bits + 7) / 8; i++) {
    uint8_t cur_bits = std::min((size_t)8, num_bits - i * 8);
    if (!mpsse_buffer_ensure_space(3))
      return false;

    // Clock Data to TMS pin (no read)
    mpsse_buffer_append_byte(MPSSE_WRITE_TMS | MPSSE_LSB | MPSSE_BITMODE |
                             MPSSE_WRITE_NEG);
    // length in bits -1
    mpsse_buffer_append_byte((uint8_t)(cur_bits - 1));
    // data
    mpsse_buffer_append_byte(data[i]);
  }

  return true;
}

bool mpsse_jtag_scan_chain_send(const uint8_t *data, size_t num_bits,
                                bool flip_tms, bool do_read) {
  size_t bulk_bits = num_bits;
  if (flip_tms) {
    // last bit should be sent along TMS 0->1
    bulk_bits -= 1;
  }
  uint8_t do_read_flag = do_read ? MPSSE_DO_READ : 0;

  // send whole bytes first
  size_t length_in_bytes = bulk_bits / 8;
  if (length_in_bytes) {
    if (!mpsse_buffer_ensure_space(3 + length_in_bytes))
      return false;
    mpsse_buffer_append_byte((uint8_t)(do_read_flag | MPSSE_DO_WRITE | MPSSE_LSB |
                             MPSSE_WRITE_NEG));
    mpsse_buffer_append_byte((uint8_t)((length_in_bytes - 1) & 0xff));
    mpsse_buffer_append_byte((uint8_t)((length_in_bytes - 1) >> 8));
    mpsse_buffer_append(data, length_in_bytes);
  }

  // sent rest bits
  if (bulk_bits % 8) {
    if (!mpsse_buffer_ensure_space(3))
      return false;
    mpsse_buffer_append_byte((uint8_t)(do_read_flag | MPSSE_DO_WRITE | MPSSE_LSB |
                             MPSSE_WRITE_NEG | MPSSE_BITMODE));
    // length in bits -1
    mpsse_buffer_append_byte((uint8_t)((bulk_bits % 8) - 1));
    // data
    mpsse_buffer_append_byte(data[length_in_bytes]);
  }

  if (flip_tms) {
    // send last bit along TMS=1
    JtagState new_state = next_state(state, 1);
    dprintf("JTAG state: %s -> %s\n", state_to_string(state),
            state_to_string(new_state));
    state = new_state;

    uint8_t bit = (data[(num_bits - 1) / 8] >> ((num_bits - 1) % 8)) & 1;
    if (!mpsse_buffer_ensure_space(3))
      return false;
    mpsse_buffer_append_byte((uint8_t)(do_read_flag | MPSSE_WRITE_TMS | MPSSE_LSB |
                             MPSSE_BITMODE | MPSSE_WRITE_NEG));
    // length in bits -1
    mpsse_buffer_append_byte(0x00);
    // data
    // 7-th bit: last bit
    // TMS=1
    mpsse_buffer_append_byte((uint8_t)(0x01 | (bit << 7)));
  }

  return true;
}

bool mpsse_jtag_scan_chain_recv(uint8_t *recv, size_t num_bits, bool flip_tms) {
  if (!mpsse_buffer_ensure_space(1))
    return false;
  // flush FTDI buffers after all write commands are sent
  mpsse_buffer_append_byte(SEND_IMMEDIATE);
  // send all write commands
  if (!mpsse_buffer_flush())
    return false;
  size_t bulk_bits = num_bits;
  if (flip_tms) {
    // last bit should be sent along TMS 0->1
    bulk_bits -= 1;
  }

  // read bulk
  size_t len = (bulk_bits + 7) / 8;
  memset(recv, 0, len);
  size_t offset = 0;
  while (len > offset) {
    int read = ftdi_read_data(ftdi, &recv[offset], len - offset);
    if (read < 0) {
      printf("Error @ %s:%d : %s\n", __FILE__, __LINE__,
             ftdi_get_error_string(ftdi));
      return false;
    }
    offset += read;
  }

  if (bulk_bits % 8) {
    // a length of 1 bit will have the data bit sampled in bit 7 of the byte
    // sent back to the PC, so we need to shift this
    recv[bulk_bits / 8] >>= 8 - (bulk_bits % 8);
  }

  // handle last bit when TMS=1
  if (flip_tms) {
    uint8_t last_bit;
    while (ftdi_read_data(ftdi, &last_bit, 1) != 1)
      ;

    // the bit read is at BIT 7
    recv[(num_bits - 1) / 8] |= ((last_bit >> 7) & 1) << ((num_bits - 1) % 8);
  }

  return true;
}

bool mpsse_set_tck_freq(uint64_t freq_mhz) {
  // send any pending commands
  if (!mpsse_buffer_flush())
    return false;
  // set clock to base / ((1 + 1) * 2)
  // when "divide by 5" is disabled, base clock is 60MHz
  int divisor = (60 / 2 + freq_mhz - 1) / freq_mhz - 1;
  int actual_freq = 60 / ((1 + divisor) * 2);
  printf("Requested jtag tck: %lld MHz\n", freq_mhz);
  printf("Actual jtag tck: %d MHz\n", actual_freq);
  uint8_t setup[256] = {TCK_DIVISOR, (uint8_t)divisor, 0x00, DIS_DIV_5};
  if (!ftdi_write_retry(ftdi, setup, 4)) {
    printf("Error @ %s:%d : %s\n", __FILE__, __LINE__,
           ftdi_get_error_string(ftdi));
    return false;
  }

  return true;
}

bool mpsse_jtag_clock_tck(size_t times) {
  // send any pending commands
  if (!mpsse_buffer_flush())
    return false;
  size_t times_8 = times / 8;
  if (times_8) {
    // Clock For n x 8 bits with no data transfer
    uint8_t buf[256] = {
        0x8F,
        (uint8_t)((times_8 - 1) & 0xFF),
        (uint8_t)((times_8 - 1) >> 8),
    };
    if (!ftdi_write_retry(ftdi, buf, 3)) {
      printf("Error @ %s:%d : %s\n", __FILE__, __LINE__,
             ftdi_get_error_string(ftdi));
      return false;
    }
  }

  if (times % 8) {
    // Clock For n bits with no data transfer
    uint8_t buf[256] = {
        0x8E,
        (uint8_t)((times % 8) - 1),
    };
    if (!ftdi_write_retry(ftdi, buf, 2)) {
      printf("Error @ %s:%d : %s\n", __FILE__, __LINE__,
             ftdi_get_error_string(ftdi));
      return false;
    }
  }
  return true;
}

driver mpsse_driver = {
    .init = mpsse_init,
    .deinit = mpsse_deinit,
    .set_tck_freq = mpsse_set_tck_freq,
    .jtag_tms_seq = mpsse_jtag_tms_seq,
    .jtag_scan_chain_send = mpsse_jtag_scan_chain_send,
    .jtag_scan_chain_recv = mpsse_jtag_scan_chain_recv,
    .jtag_clock_tck = mpsse_jtag_clock_tck,
};