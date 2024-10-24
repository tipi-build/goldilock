# Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
# SPDX-License-Identifier: GPL-2.0-only OR Proprietary

cmake_minimum_required(VERSION 3.27.6)
set(FETCHCONTENT_QUIET OFF CACHE BOOL "" FORCE)

project(goldilock 
  VERSION "0.0.1"
  LANGUAGES CXX
)
enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


include(FetchContent)

FetchContent_Declare(
  cxxopts
  GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
  GIT_TAG        3bf268481da8208d171d8908e6491459de3651d7 # tag v3.2.0
)

FetchContent_MakeAvailable(cxxopts)

find_package(Threads)

add_library(libgoldilock-utils INTERFACE )
add_library(libgoldilock-utils::libgoldilock-utils ALIAS libgoldilock-utils)

target_include_directories(libgoldilock-utils INTERFACE 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include> 
)

add_subdirectory(src)