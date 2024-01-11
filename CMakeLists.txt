cmake_minimum_required(VERSION 3.12)
project(jtag-remote-server)

find_package(PkgConfig)
pkg_check_modules(FTDI REQUIRED libftdi1)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS_DEBUG "-fsanitize=address ${CMAKE_CXX_FLAGS_DEBUG}")

add_executable(jtag-remote-server src/main.cpp src/xvc.cpp src/rbb.cpp src/common.cpp src/vpi.cpp src/jtagd.cpp src/mpsse.cpp src/mpsse_buffer.cpp src/usb_blaster.cpp)
target_link_libraries(jtag-remote-server ${FTDI_LDFLAGS})
target_include_directories(jtag-remote-server PUBLIC ${FTDI_INCLUDE_DIRS})

install(TARGETS jtag-remote-server)
install(FILES README.md LICENSE DESTINATION share/jtag-remote-server)
install(DIRECTORY example DESTINATION share/jtag-remote-server)