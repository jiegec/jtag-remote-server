#include "common.h"
#include <string>

bool jtag_jtagd_init() {
  if (!setup_tcp_server(1309)) {
    return false;
  }

  printf("Start intel jtagd server at :1309\n");
  return true;
}

// Packet structure learned from intel/libaji_client
// Two-byte header: (mux << 12) | (length - 1)
// Body: typed fields
// String: 1-byte length, then the string content
// Integer: 4-byte, big endian

uint8_t send_buffer[BUFFER_SIZE];
uint8_t send_buffer_size;

// jtag_message.h TXMESSAGE::add_string
void add_string(const char *data, size_t length) {
  assert(length <= 0xff);
  assert(send_buffer_size + 1 + length <= BUFFER_SIZE);
  send_buffer[send_buffer_size++] = length;
  memcpy(&send_buffer[send_buffer_size], data, length);
  send_buffer_size += length;
}

void add_string(const std::string &s) { add_string(s.data(), s.length()); }

// jtag_message.h TXMESSAGE::add_int
void add_int(uint32_t value) {
  assert(send_buffer_size + sizeof(uint32_t) <= BUFFER_SIZE);
  uint32_t be = htonl(value);
  memcpy(&send_buffer[send_buffer_size], &be, sizeof(uint32_t));
  send_buffer_size += sizeof(uint32_t);
}

// jtag_message.h RXMESSAGE::remove_response
// 1-byte: resp
// 1-byte: zero
// 2-byte: length in big endian
void add_response(uint8_t resp, uint16_t length) {
  assert(send_buffer_size + 4 <= BUFFER_SIZE);
  send_buffer[send_buffer_size++] = resp;
  send_buffer[send_buffer_size++] = 0;
  send_buffer[send_buffer_size++] = (length >> 8);
  send_buffer[send_buffer_size++] = length;
}

uint16_t compute_header(uint16_t length, uint16_t mux) {
  return (mux << 12) | (length - 1);
}

void fill_header(uint16_t mux) {
  uint16_t actual_length = send_buffer_size - 2;
  uint16_t header = compute_header(actual_length, mux);
  send_buffer[0] = header >> 8;
  send_buffer[1] = header;
}

void do_send(uint16_t mux) {
  fill_header(mux);
  dprintf("Sending:\n");
  for (int i = 0; i < send_buffer_size; i++) {
    dprintf("%02X ", send_buffer[i]);
  }
  dprintf("\n");
  write_full(client_fd, send_buffer, send_buffer_size);
  send_buffer_size = 2;
}

// jtag_message.h add_command
// message header:
// 1-byte command jtag_message.h MESSAGE::COMMAND
// 1-byte zero
// 2-byte length including header
struct MessageHeader {
  uint8_t command;
  uint8_t zero;
  uint16_t be_len; // big endian
};

void jtag_jtagd_tick() {
  // leave space for header
  send_buffer_size = 2;
  if (client_fd >= 0) {
    if (!read_socket()) {
      return;
    }

    // the protocol is learned from intel/libaji_client
    while (buffer_begin + 2 <= buffer_end) {
      // jtag_tcplink.cpp TCPLINK:add_packet
      uint16_t header =
          ((uint16_t)buffer[buffer_begin] << 8) + buffer[buffer_begin + 1];
      uint16_t mux = header >> 12;
      uint16_t length = (header & ((1 << 12) - 1)) + 1;
      if (buffer_begin + 2 + length <= buffer_end) {
        dprintf("Received message of length %d\n", length);
        buffer_begin += 2;

        // one or more messages
        uint8_t *p = (uint8_t *)&buffer[buffer_begin];
        uint8_t *end = (uint8_t *)&buffer[buffer_begin + length];
        while (p < end) {
          MessageHeader *header = (MessageHeader *)p;
          if (header->command == 0x83) {
            // GET_VERSION_INFO
            dprintf("GET_VERSION_INFO\n");
            // response:
            // a string: version info
            // an int: pgmparts version
            // a string: server path
            std::string version_info = "1.0";
            std::string server_path = "jtagd";
            add_response(0, 4 + 1 + version_info.length() + 4 + 1 +
                                server_path.length());
            add_string(version_info);
            add_int(0);
            add_string(server_path);
          } else if (header->command == 0xFE) {
            // USE_PROTOCOL_VERSION
            dprintf("USE_PROTOCOL_VERSION\n");
            // the argument is version
            // response: flags
            int flags = 0;
            // 8: 4 header, 1 int
            add_response(0, 8);
            add_int(flags);
          } else {
            dprintf("Unrecognized command: %x\n", header->command);

            // aji.h AJI_UNIMPLEMENTED
            add_response(126, 4);
          }
          p += ntohs(header->be_len);
        }

        do_send(0);

        buffer_begin += length;
      }
    }

  } else {
    // accept connection
    if (try_accept()) {
      // send initial message
      // jtag_client_link.cpp AJI_CLIENT::prepare_connection
      // string AJI_SIGNATURE
      std::string signature = "JTAG Server\r\n";
      add_string(signature.data(), signature.length());
      // integer server_version AJI_CURRENT_VERSION
      add_int(13);
      // integer authtype
      // no authentication
      add_int(0);
      do_send(0);
      dprintf("Sent hello message\n");
    }
  }
}