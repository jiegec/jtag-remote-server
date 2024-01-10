#include <stdlib.h>
#include "mpsse_buffer.h"
#include "common.h"
#include "ftdi.h"

#define BUFFER_LENGTH 16384
static uint8_t mpsse_buffer[BUFFER_LENGTH];
static size_t mpsse_buffer_pos = 0;
static struct ftdi_context* mpsse_ftdi = NULL;

void mpsse_buffer_init(struct ftdi_context *ftdi)
{
  mpsse_buffer_pos = 0;
  mpsse_ftdi = ftdi;
}

bool mpsse_buffer_flush() {
  if (!mpsse_buffer_pos)
    return true;
  dprintf("mpsse_buffer_flush %zu bytes\n", mpsse_buffer_pos);
  if (!ftdi_write_retry(mpsse_ftdi, mpsse_buffer, mpsse_buffer_pos)) {
    printf("Error writing %zu bytes @ %s:%d : %s\n", mpsse_buffer_pos, __FILE__, __LINE__,
           ftdi_get_error_string(mpsse_ftdi));
    mpsse_buffer_pos = 0;
    return false;
  }
  mpsse_buffer_pos = 0;
  return true;
}

bool mpsse_buffer_ensure_space(size_t num_bytes) {
  if (num_bytes >= BUFFER_LENGTH) {
    printf("MPSSE buffer too small\n");
    return false;
  }
  if(mpsse_buffer_pos + num_bytes >= BUFFER_LENGTH)
    return mpsse_buffer_flush();
  return true;
}

void mpsse_buffer_append_byte(uint8_t data) {
  mpsse_buffer[mpsse_buffer_pos++] = data;
}

void mpsse_buffer_append(const uint8_t* data, size_t num_bytes) {
  memcpy(mpsse_buffer + mpsse_buffer_pos, data, num_bytes);
  mpsse_buffer_pos += num_bytes;
}
