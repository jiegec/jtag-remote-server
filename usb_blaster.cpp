#include "usb_blaster.h"

#include "common.h"
#include <algorithm>
#include <ftdi.h>

static struct ftdi_context *ftdi;

// reference:
// https://github.com/openocd-org/openocd/blob/master/src/jtag/drivers/usb_blaster/usb_blaster.c

static uint8_t recv_buffer[4096];
static size_t recv_buffer_len = 0;

// ublast_build_out
uint8_t build_command(int tms, int tdi, int tck, bool read) {
  uint8_t command = 0;
  // tck
  if (tck) {
    command |= 1 << 0;
  }
  // tms
  if (tms) {
    command |= 1 << 1;
  }
  // nce, ncs
  command |= 1 << 2;
  command |= 1 << 3;
  // tdi
  if (tdi) {
    command |= 1 << 4;
  }
  // led
  command |= 1 << 5;
  // read
  if (read) {
    command |= 1 << 6;
  }
  return command;
}

bool usb_blaster_init() {
  printf("Initialize ftdi\n");
  ftdi = ftdi_new();
  assert(ftdi);

  printf("Open device vid=0x%04x pid=0x%04x\n", ftdi_vid, ftdi_pid);
  int ret = ftdi_usb_open(ftdi, ftdi_vid, ftdi_pid);
  if (ret) {
    printf("Error @ %s:%d : %s\n", __FILE__, __LINE__,
           ftdi_get_error_string(ftdi));
    return false;
  }

  ret = ftdi_usb_reset(ftdi);
  assert(ret == 0);
  ret = ftdi_set_baudrate(ftdi, 115200);
  assert(ret == 0);
  ret = ftdi_set_latency_timer(ftdi, 1); // reduce latency
  assert(ret == 0);

  ftdi_disable_bitbang(ftdi);

  printf("Initialize usb blaster\n");
  // flush queue
  uint8_t buffer[4096];
  for (int i = 0; i < 4096; i++) {
    buffer[i] = build_command(0, 0, (i % 2), false);
  }

  if (ftdi_write_data(ftdi, buffer, 4096) != 4096) {
    printf("Error @ %s:%d : %s\n", __FILE__, __LINE__,
           ftdi_get_error_string(ftdi));
    return false;
  }

  // read anything from the queue
  uint8_t data;
  while (ftdi_read_data(ftdi, &data, 1) > 0)
    ;

  // reset JTAG
  uint8_t tms_reset = 0xff;
  usb_blaster_jtag_tms_seq(&tms_reset, 5);
  return true;
}

bool usb_blaster_deinit() { return true; }

bool usb_blaster_jtag_tms_seq(const uint8_t *data, size_t num_bits) {
  // for each bit
  // clock tms with tck=0 and tck=1
  std::vector<uint8_t> buffer;
  buffer.reserve(num_bits * 2 + 1);
  uint8_t bit;
  for (int i = 0; i < num_bits; i++) {
    bit = (data[i / 8] >> (i % 8)) & 1;
    // tck=0
    buffer.push_back(build_command(bit, 0, 0, false));
    // tck=1
    buffer.push_back(build_command(bit, 0, 1, false));
  }
  // set tck=0
  buffer.push_back(build_command(bit, 0, 0, false));

  if (ftdi_write_data(ftdi, buffer.data(), buffer.size()) != buffer.size()) {
    printf("Error @ %s:%d : %s\n", __FILE__, __LINE__,
           ftdi_get_error_string(ftdi));
    return false;
  }
  return true;
}

bool usb_blaster_jtag_scan_chain_send(const uint8_t *data, size_t num_bits,
                                      bool flip_tms, bool do_read) {
  size_t bulk_bits = num_bits;
  if (flip_tms) {
    // last bit should be sent along TMS 0->1
    bulk_bits -= 1;
  }
  uint8_t do_read_flag = do_read ? (1 << 6) : 0;

  assert(recv_buffer_len == 0);

  // send whole bytes first
  size_t length_in_bytes = bulk_bits / 8;
  uint8_t buffer[256];
  size_t buffer_len = 0;
  if (length_in_bytes) {
    const size_t MAX_PACKET_SIZE = 32;
    for (int i = 0; i < length_in_bytes;) {
      buffer_len = 0;
      int trans = std::min(length_in_bytes - i, MAX_PACKET_SIZE);

      // byte-shift mode
      buffer[buffer_len++] = (1 << 7) | do_read_flag | trans;
      memcpy(&buffer[buffer_len], &data[i], trans);
      buffer_len += trans;

      if (ftdi_write_data(ftdi, buffer, buffer_len) != buffer_len) {
        printf("Error @ %s:%d : %s\n", __FILE__, __LINE__,
               ftdi_get_error_string(ftdi));
        return false;
      }

      if (do_read) {
        // read immediately
        if (!ftdi_read_retry(ftdi, &recv_buffer[recv_buffer_len], trans)) {
          return false;
        }
        recv_buffer_len += trans;
      }

      i += trans;
    }
  }

  // sent rest bits
  if (bulk_bits % 8) {
    buffer_len = 0;

    for (int i = 0; i < bulk_bits % 8; i++) {
      int offset = length_in_bytes * 8 + i;
      uint8_t bit = (data[offset / 8] >> (offset % 8)) & 1;

      // tck=0
      buffer[buffer_len++] = build_command(0, bit, 0, false);
      // tck=1
      buffer[buffer_len++] = build_command(0, bit, 1, do_read);
    }

    if (ftdi_write_data(ftdi, buffer, buffer_len) != buffer_len) {
      printf("Error @ %s:%d : %s\n", __FILE__, __LINE__,
             ftdi_get_error_string(ftdi));
      return false;
    }

    if (do_read) {
      // read immediately
      int trans = bulk_bits % 8;
      if (!ftdi_read_retry(ftdi, &recv_buffer[recv_buffer_len], trans)) {
        printf("Error @ %s:%d : %s\n", __FILE__, __LINE__,
               ftdi_get_error_string(ftdi));
        return false;
      }
      recv_buffer_len += trans;
    }
  }

  if (flip_tms) {
    // send last bit along TMS=1
    JtagState new_state = next_state(state, 1);
    dprintf("JTAG state: %s -> %s\n", state_to_string(state),
            state_to_string(new_state));
    state = new_state;

    buffer_len = 0;

    uint8_t bit = (data[(num_bits - 1) / 8] >> ((num_bits - 1) % 8)) & 1;

    // tck=0
    buffer[buffer_len++] = build_command(1, bit, 0, false);
    // tck=1
    buffer[buffer_len++] = build_command(1, bit, 1, do_read);
    // tck=0
    buffer[buffer_len++] = build_command(1, bit, 0, false);

    if (ftdi_write_data(ftdi, buffer, buffer_len) != buffer_len) {
      printf("Error @ %s:%d : %s\n", __FILE__, __LINE__,
             ftdi_get_error_string(ftdi));
      return false;
    }

    if (do_read) {
      // read immediately
      if (!ftdi_read_retry(ftdi, &recv_buffer[recv_buffer_len], 1)) {
        printf("Error @ %s:%d : %s\n", __FILE__, __LINE__,
               ftdi_get_error_string(ftdi));
        return false;
      }
      recv_buffer_len += 1;
    }
  }
  return true;
}

bool usb_blaster_jtag_scan_chain_recv(uint8_t *recv, size_t num_bits,
                                      bool flip_tms) {

  size_t bulk_bits = num_bits;
  if (flip_tms) {
    // last bit should be sent along TMS 0->1
    bulk_bits -= 1;
  }

  size_t len = (bulk_bits + 7) / 8;
  memset(recv, 0, len);

  // read whole bytes first
  size_t offset = 0;
  size_t length_in_bytes = bulk_bits / 8;
  if (length_in_bytes) {
    memcpy(recv, &recv_buffer[offset], length_in_bytes);
    offset += length_in_bytes;
  }

  // read rest bits
  if (bulk_bits % 8) {
    for (int i = 0; i < bulk_bits % 8; i++) {
      uint8_t last_bit = recv_buffer[offset++];

      if (last_bit & 1) {
        recv[bulk_bits / 8] |= (1 << i);
      }
    }
  }

  // handle last bit when TMS=1
  if (flip_tms) {
    uint8_t last_bit = recv_buffer[offset++];

    if (last_bit & 1) {
      recv[(num_bits - 1) / 8] |= 1 << ((num_bits - 1) % 8);
    }
  }

  assert(offset == recv_buffer_len);
  recv_buffer_len = 0;
  return true;
}

bool usb_blaster_set_tck_freq(uint64_t freq_mhz) { return true; }

bool usb_blaster_jtag_clock_tck(size_t times) { return true; }

driver usb_blaster_driver = {
    .init = usb_blaster_init,
    .deinit = usb_blaster_deinit,
    .set_tck_freq = usb_blaster_set_tck_freq,
    .jtag_tms_seq = usb_blaster_jtag_tms_seq,
    .jtag_scan_chain_send = usb_blaster_jtag_scan_chain_send,
    .jtag_scan_chain_recv = usb_blaster_jtag_scan_chain_recv,
    .jtag_clock_tck = usb_blaster_jtag_clock_tck,
};