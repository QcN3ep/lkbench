# SPDX-License-Identifier: MPL-2.0

cmake_minimum_required(VERSION 3.16)
project(lkbench C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

file(GLOB_RECURSE LK_TEST_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/testcases/*.c")

add_executable(lkbench
    src/main.c
    src/measure.c
    src/registry.c
    ${LK_TEST_SOURCES})

target_include_directories(lkbench PRIVATE src)
target_compile_definitions(lkbench PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(lkbench PRIVATE -Wall -Wextra -Wpedantic)
