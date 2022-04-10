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

Some testing reveals that this tool can run at 14Mbps(rbb mode)/4Mbps(jtag_vpi mode)/2Mbps(xvc mode) when programming bitstream to FPGA. The speed of 14Mbps is mainly limited by the 15MHz jtag clock and could be improved by using a faster clock if the jtag tap can work under 30MHz(maximum clock frequency is 60MHz / 2). As per DS893, maximum TCK frequency of Xilinx Virtex Ultrascale devices is 20MHz(SLR-based) or 50MHz(others).

Let us analyze the difference. Both jtag_vpi & xvc mode reads tdo upon shifting, while remote bitbang might not. We made a small optimization to skip reading from mpsse for remote bitbang mode when the client does not send `R` to the server. In jtag_vpi & xvc mode however, it has to read every bit shifted out of tdo, and runs `write, read` sequence in a loop. This leads to a great bandwidth loss, because we have to wait the latency for each request.

A possible improvement is to group all the write requests and send them at once, and then read the responses back. This is exactly what OpenOCD has done. In OpenOCD, there is a global jtag command queue and the ftdi driver can send many asynchronous requests at the same time upon `ftdi_execute_queue()`; the jtag_vpi driver on the other hand, always runs `write + read` for each request in the queue.

The analysis above mainly focues on fpga programming, where in the most time, tdo is omitted to optimize performance. In other cases, like gdb debugging, further optimization needs to be employed.