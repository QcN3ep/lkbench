# SPDX-License-Identifier: MPL-2.0

cmake_minimum_required(VERSION 3.16)
project(lkbench VERSION 0.0.1 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

file(GLOB_RECURSE LK_TEST_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/testcases/*.c")

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(FILTER LK_TEST_SOURCES EXCLUDE REGEX "/testcases/linux/")
endif()

add_library(yyjson STATIC third/yyjson/yyjson.c)
target_include_directories(yyjson SYSTEM PUBLIC third/yyjson)
target_compile_options(yyjson PRIVATE -w)

add_library(xml STATIC third/xml.h/xml_impl.c)
target_include_directories(xml SYSTEM PUBLIC third/xml.h)
target_compile_options(xml PRIVATE -w)

add_library(ymlparser STATIC third/ymlparser/yml_impl.c)
target_include_directories(ymlparser SYSTEM PUBLIC third/ymlparser)
target_compile_options(ymlparser PRIVATE -w)

add_library(minicbor STATIC third/minicbor/minicbor_impl.c)
target_include_directories(minicbor SYSTEM PUBLIC third/minicbor)
target_compile_options(minicbor PRIVATE -w)

add_executable(lkbench
    src/main.c
    src/measure.c
    src/metrics.c
    src/registry.c
    ${LK_TEST_SOURCES})

target_include_directories(lkbench PRIVATE src)
target_link_libraries(lkbench PRIVATE yyjson m xml ymlparser minicbor)
target_compile_definitions(lkbench PRIVATE
    _GNU_SOURCE
    _POSIX_C_SOURCE=200809L
    LKBENCH_VERSION="${PROJECT_VERSION}")
target_compile_options(lkbench PRIVATE -Wall -Wextra -Wpedantic)
