#include "common.h"
enum JtagVpiCommand {
  CMD_RESET,
  CMD_TMS_SEQ,
  CMD_SCAN_CHAIN,
  CMD_SCAN_CHAIN_FLIP_TMS,
  CMD_STOP_SIMU
};

struct jtag_vpi_cmd {
  uint32_t cmd;
  uint8_t buffer_out[512];
  uint8_t buffer_in[512];
  uint32_t length;
  uint32_t nb_bits;
};

bool jtag_vpi_init() {
  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket");
    return false;
  }

  // set non blocking
  fcntl(listen_fd, F_SETFL, O_NONBLOCK);

  int reuseaddr = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int)) <
      0) {
    perror("setsockopt");
    return false;
  }

  int port = 12345;
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    return false;
  }

  if (listen(listen_fd, 1) == -1) {
    perror("listen");
    return false;
  }
  printf("JTAG vpi server listening at :12345\n");

  mpsse_init();

  return true;
}

void jtag_vpi_tick() {
  // ref jtag_vpi project jtagServer.cpp

  static uint8_t jtag_vpi_buffer[sizeof(struct jtag_vpi_cmd)];
  static size_t jtag_vpi_recv = 0;

  struct jtag_vpi_cmd *cmd = (struct jtag_vpi_cmd *)jtag_vpi_buffer;

  if (client_fd >= 0) {
    ssize_t num_read = read(client_fd, &jtag_vpi_buffer[jtag_vpi_recv],
                            sizeof(jtag_vpi_cmd) - jtag_vpi_recv);
    if (num_read > 0) {
      jtag_vpi_recv += num_read;
    } else if (num_read == 0) {
      // remote socket closed
      printf("JTAG debugger detached\n");
      close(client_fd);
      client_fd = -1;

      // reset mpsse
      ftdi_set_bitmode(ftdi, 0, 0);
    }

    if (jtag_vpi_recv == sizeof(struct jtag_vpi_cmd)) {
      // cmd valid
      jtag_vpi_recv = 0;
      memset(cmd->buffer_in, 0, sizeof(cmd->buffer_in));
      if (cmd->cmd == CMD_RESET) {
        jtag_fsm_reset();
      } else if (cmd->cmd == CMD_TMS_SEQ) {
        jtag_tms_seq(cmd->buffer_out, cmd->nb_bits);
      } else if (cmd->cmd == CMD_SCAN_CHAIN) {
        jtag_scan_chain(cmd->buffer_out, cmd->buffer_in, cmd->nb_bits, false);
        write_full(client_fd, jtag_vpi_buffer, sizeof(struct jtag_vpi_cmd));
      } else if (cmd->cmd == CMD_SCAN_CHAIN_FLIP_TMS) {
        jtag_scan_chain(cmd->buffer_out, cmd->buffer_in, cmd->nb_bits, true);
        write_full(client_fd, jtag_vpi_buffer, sizeof(struct jtag_vpi_cmd));
      }
    }
  } else {
    client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd > 0) {
      fcntl(client_fd, F_SETFL, O_NONBLOCK);

      // set nodelay
      int flags = 1;
      if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, (void *)&flags,
                     sizeof(flags)) < 0) {
        perror("setsockopt");
      }
      printf("JTAG debugger attached\n");
    }
  }
}