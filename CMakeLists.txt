cmake_minimum_required(VERSION 3.12)
project(jtag-xvc-rbb)

find_package(PkgConfig)
pkg_check_modules(FTDI REQUIRED libftdi1)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "-fsanitize=address ${CMAKE_CXX_FLAGS}")

add_executable(xvc xvc.cpp)
target_link_libraries(xvc ${FTDI_LDFLAGS})
target_include_directories(xvc PUBLIC ${FTDI_INCLUDE_DIRS})

add_executable(rbb rbb.cpp)
target_link_libraries(rbb ${FTDI_LDFLAGS})
target_include_directories(rbb PUBLIC ${FTDI_INCLUDE_DIRS})