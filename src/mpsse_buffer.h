#ifndef __MPSSE_BUFFER_H__
#define __MPSSE_BUFFER_H__

#include <stddef.h>
#include <stdint.h>

void mpsse_buffer_init(struct ftdi_context *ftdi);
bool mpsse_buffer_ensure_space(size_t num_bytes);
void mpsse_buffer_append_byte(uint8_t data);
void mpsse_buffer_append(const uint8_t* data, size_t num_bytes);
bool mpsse_buffer_flush();

#endif