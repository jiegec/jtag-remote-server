# jtag-remote-server

This tool can help you debug chips remotely. It requires libftdi to communicate with FTDI chips and exposes various protocol for remote debugging.

An example scenario where this tool might be useful:

```
┌────────┐ JTAG ┌────────┐ USB ┌──────────┐ TCP ┌──────────┐
│  FPGA  ├──────┤  FTDI  ├─────┤  HOST 1  ├─────┤  HOST 2  │
└────────┘      └────────┘     └──────────┘     └──────────┘
```

You can run this tool on `HOST 1` and debug the chip on `HOST 2` remotely.

Supported protocols:

- Xilinx virtual cable: for Vivado
- Remote bitbang: for OpenOCD
- JTAG vpi: for OpenOCD

Some example OpenOCD configs are provided under `examples` directory.

## Performance

Some testing reveals that this tool can run at 4Mbps(jtag_vpi mode) & 2Mbps(xvc mode), while local OpenOCD can run at 15Mbps(or higher). The difference here is mainly due to the sequential firing of usb requests:

In OpenOCD, there is a global jtag command queue and the ftdi driver can send many asynchronous requests at the same time upon `ftdi_execute_queue()`; however, jtag_vpi processes the commands in order(write to socket, read from socket, loop); for bitbang-like protocols(remote bitbang/xilinx virtual cable), it heavily depends on the client implementation(this tool serves as the server). In OpenOCD, the remote bitbang driver collects commands in a queue and sends them in bulk, so there is some possiblity to improve the performance by decoding the bit sequence and sending request ahead. Vivado is closed-source, so we do not know the implentation details.

So, if you have local access, you should consider the performance loss before using this tool.