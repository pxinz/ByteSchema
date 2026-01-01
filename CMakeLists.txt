cmake_minimum_required(VERSION 4.0)
project(byteschema)

set(CMAKE_CXX_STANDARD 20)

add_library(byteschema STATIC include/bsp.hpp)
