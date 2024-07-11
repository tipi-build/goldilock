# Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
# SPDX-License-Identifier: GPL-2.0-only OR Proprietary

cmake_minimum_required(VERSION 3.27.6)
set(FETCHCONTENT_QUIET OFF CACHE BOOL "" FORCE)

project(goldilock VERSION "0.0.1")
enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads)

add_library(libgoldilock-utils INTERFACE )
add_library(libgoldilock-utils::libgoldilock-utils ALIAS libgoldilock-utils)

target_include_directories(libgoldilock-utils INTERFACE 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include> 
)

add_subdirectory(src)