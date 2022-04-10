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

Some testing reveals that this tool can run at 4Mbps(jtag_vpi mode), while local OpenOCD can run at 15Mbps(or higher). The difference here is mainly due to sequential firing of usb requests: in OpenOCD, there is a jtag queue and the ftdi driver can send many asynchronous requests at the same time; however, jtag_vpi processes the commands in order(write to socket, read from socket, loop).

So, if you have local access, you should consider the performance loss before using this tool.