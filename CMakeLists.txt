cmake_minimum_required(VERSION 3.12)
project(local-xvc)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "-fsanitize=address ${CMAKE_CXX_FLAGS}")
add_executable(main main.cpp)
