cmake_minimum_required(VERSION 3.10.2)
project(socket)

set(CMAKE_CXX_STANDARD 14)

add_executable(socket main.cpp socket_client.cpp socket_service.cpp tool.h pts.cpp pts.h)
