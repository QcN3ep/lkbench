cmake_minimum_required(VERSION 3.16)
project(lkbench C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(lkbench src/main.c)

target_compile_definitions(lkbench PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(lkbench PRIVATE -Wall -Wextra)
