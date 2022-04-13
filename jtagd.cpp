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

void add_array(uint8_t *data, size_t len) {
  assert(send_buffer_size + len <= BUFFER_SIZE);
  memcpy(&send_buffer[send_buffer_size], data, len);
  send_buffer_size += len;
}

int last_response = -1;

// jtag_message.h RXMESSAGE::remove_response
// 1-byte: resp
// 1-byte: zero
// 2-byte: length in big endian
void add_response(uint8_t resp) {
  assert(last_response == -1);
  assert(send_buffer_size + 4 <= BUFFER_SIZE);
  last_response = send_buffer_size;
  send_buffer[send_buffer_size++] = resp;
  send_buffer[send_buffer_size++] = 0;
  //  length will be filled below
  send_buffer[send_buffer_size++] = 0;
  send_buffer[send_buffer_size++] = 0;
}

void end_response() {
  assert(last_response != -1);
  uint16_t length = send_buffer_size - last_response;
  send_buffer[last_response + 2] = length >> 8;
  send_buffer[last_response + 3] = length;
  last_response = -1;
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

// saved device list
std::vector<uint32_t> devices;

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
          (((uint16_t)buffer[buffer_begin]) << 8) + buffer[buffer_begin + 1];
      uint16_t mux = header >> 12;
      uint16_t length = (header & ((1 << 12) - 1)) + 1;
      if (buffer_begin + 2 + length <= buffer_end) {
        dprintf("Received message of length %d:\n", length);
        for (int i = 0; i < length; i++) {
          dprintf("%02X ", (uint8_t)buffer[buffer_begin + 2 + i]);
        }
        dprintf("\n");
        buffer_begin += 2;

        // one or more messages
        uint8_t *p = (uint8_t *)&buffer[buffer_begin];
        uint8_t *end = (uint8_t *)&buffer[buffer_begin + length];
        while (p < end) {
          MessageHeader *header = (MessageHeader *)p;
          if (header->command == 0x80) {
            // GET_HARDWARE
            dprintf("GET_HARDWARE\n");
            // jtag_client_link.cpp AJI_CLIENT::get_hardware_from_server
            // response:
            add_response(0);
            // an int: n: number of devices
            int n = 1;
            add_int(n);
            // an int: fifo_len: payload size below
            std::string hw_name = "hw0";
            std::string port = "port0";
            std::string device_name = "device0";
            int fifo_len = 4 + 1 + hw_name.length() + 1 + port.length() + 4 +
                           1 + device_name.length() + 4;
            add_int(fifo_len);
            end_response();
            do_send(0);

            // each payload:
            // an int: chain_id
            int chain_id = 1; // cannot be zero
            add_int(chain_id);
            // a string: hw_name
            add_string(hw_name);
            // a string: port
            add_string(port);
            // an int: chain_type
            int chain_type = 1; // JTAG
            add_int(chain_type);
            // a string: device_name
            add_string(device_name);
            // an int: features
            int features = 0x0800; // AJI_FEATURE_JTAG
            add_int(features);

            do_send(4); // FIFO_MIN
          } else if (header->command == 0x83) {
            // GET_VERSION_INFO
            dprintf("GET_VERSION_INFO\n");
            // response:
            // a string: version info
            // an int: pgmparts version
            // a string: server path
            std::string version_info = "1.0";
            std::string server_path = "jtagd";
            add_response(0);
            add_string(version_info);
            add_int(0);
            add_string(server_path);
            end_response();
          } else if (header->command == 0x84) {
            // GET_DEFINED_DEVICES
            dprintf("GET_DEFINED_DEVICES\n");
            add_response(0);
            // int: defined_tag
            int defined_tag = 1;
            add_int(defined_tag);
            // int: device_count
            int device_count = 0;
            add_int(device_count);
            // int: fifo_len
            int fifo_len = 0;
            add_int(fifo_len);
            end_response();
          } else if (header->command == 0xA2) {
            // LOCK_CHAIN
            dprintf("LOCK_CHAIN\n");
            // success
            add_response(0);
            end_response();
          } else if (header->command == 0xA3) {
            // UNLOCK_CHAIN
            dprintf("UNLOCK_CHAIN\n");
            // success
            add_response(0);
            end_response();
          } else if (header->command == 0xA5) {
            // READ_CHAIN
            dprintf("READ_CHAIN\n");
            // args:
            // int: chain_id
            // int: chain_tag
            // int: autoscan

            // scan jtag
            devices = jtag_probe_devices();

            add_response(0);
            // int: chain_tag
            int chain_tag = 1;
            add_int(chain_tag);
            // int: device_count
            int device_count = devices.size();
            add_int(device_count);
            // int: fifo_len
            std::string device_name = "device0";
            int fifo_len =
                device_count * (4 + 4 + 4 + 4 + 4 + 1 + device_name.length());
            add_int(fifo_len);
            end_response();
            do_send(0);

            // for each device
            for (int i = 0; i < devices.size(); i++) {
              // int: device_id
              int device_id = devices[i];
              add_int(device_id);
              // int: instruction_length
              // TODO: find this in a database
              int instruction_length = 10;
              add_int(instruction_length);
              // int: features
              int features = 0;
              add_int(features);
              // 2x int: dummy
              add_int(0);
              add_int(0);
              // string: device_name
              std::string device_name = "device0";
              add_string(device_name);
            }
            do_send(4); // FIFO_MIN
          } else if (header->command == 0xA8) {
            // OPEN_DEVICE
            dprintf("OPEN_DEVICE\n");
            add_response(0);
            // int: idcode
            // TODO: index from tap_position
            int id = devices[0];
            add_int(id);
            end_response();
          } else if (header->command == 0xAA) {
            // SET_PARAMETER
            dprintf("SET_PARAMETER\n");
            add_response(0);
            end_response();
          } else if (header->command == 0xAB) {
            // GET_PARAMETER
            dprintf("GET_PARAMETER\n");
            add_response(0);
            add_int(0);
            end_response();
          } else if (header->command == 0xC0) {
            // CLOSE_DEVICE
            dprintf("CLOSE_DEVICE\n");
            // success
            add_response(0);
            end_response();
          } else if (header->command == 0xC1) {
            // LOCK_DEVICE
            dprintf("LOCK_DEVICE\n");
            // success
            add_response(0);
            end_response();
          } else if (header->command == 0xC2) {
            // UNLOCK_DEVICE
            dprintf("UNLOCK_DEVICE\n");
            // success
            add_response(0);
            end_response();
          } else if (header->command == 0xC6) {
            // it does not appear in libaji_client
            // but it is adjacent to ACCESS_IR
            // ACCESS_IR_2
            dprintf("ACCESS_IR_2\n");

            // guessed input:
            // int: ?
            // int: 1
            // int: idcode
            // int: instruction
            uint8_t instruction[4] = {};
            // reverse endian
            instruction[0] = p[19];
            instruction[1] = p[18];
            instruction[2] = p[17];
            instruction[3] = p[16];
            uint8_t read_back[4] = {};

            // TODO: do not hardcode irlen
            int ir_len = 10;
            jtag_tms_seq_to(JtagState::ShiftIR);
            jtag_scan_chain(instruction, read_back, ir_len, true, true);

            // success
            add_response(0);
            uint32_t res = read_back[0];
            memcpy(&res, read_back, sizeof(read_back));
            add_int(res);
            end_response();
          } else if (header->command == 0xC8) {
            // it does not appear in libaji_client
            // but it is adjacent to ACCESS_DR
            // ACCESS_DR_2
            dprintf("ACCESS_DR_2\n");

            // guessed input:
            // int: 1
            // int: ?
            // int: idcode
            // int: length_dr
            // int: write_offset
            // int: write_length
            // int: read_offset
            // int: read_length
            uint32_t length_dr;
            memcpy(&length_dr, &p[16], 4);
            length_dr = ntohl(length_dr);
            uint8_t send[BUFFER_SIZE] = {};
            uint8_t recv[BUFFER_SIZE] = {};

            jtag_tms_seq_to(JtagState::ShiftDR);
            jtag_scan_chain(send, recv, length_dr, true, true);

            // success
            add_response(0);
            end_response();
            do_send(0);

            size_t num_bytes = (length_dr + 7) / 8;

            // send data via fifo
            add_array(recv, num_bytes);
            do_send(4); // FIFO_MIN
          } else if (header->command == 0xCA) {
            // RUN_TEST_IDLE
            dprintf("RUN_TEST_IDLE\n");
            jtag_tms_seq_to(JtagState::RunTestIdle);

            add_response(0);
            end_response();
          } else if (header->command == 0xFE) {
            // USE_PROTOCOL_VERSION
            dprintf("USE_PROTOCOL_VERSION\n");
            // the argument is version
            // response: flags
            int flags = 1; // SERVER_ALLOW_REMOTE
            // 8: 4 header, 1 int
            add_response(0);
            add_int(flags);
            end_response();
          } else {
            dprintf("Unrecognized command: %x\n", header->command);

            // aji.h AJI_UNIMPLEMENTED
            add_response(126);
            end_response();
          }
          p += ntohs(header->be_len);
        }

        if (send_buffer_size > 2) {
          do_send(0);
        }

        buffer_begin += length;
      } else {
        break;
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