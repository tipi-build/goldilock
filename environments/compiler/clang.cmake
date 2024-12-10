# Copyright (c) 2013, Ruslan Baratov
# All rights reserved.

if(DEFINED POLLY_COMPILER_CLANG_CMAKE)
  return()
else()
  set(POLLY_COMPILER_CLANG_CMAKE 1)
endif()

find_program(CMAKE_C_COMPILER clang)
find_program(CMAKE_CXX_COMPILER clang++)

if(NOT CMAKE_C_COMPILER)
  message(FATAL_ERROR "clang not found")
endif()

if(NOT CMAKE_CXX_COMPILER)
  message(FATAL_ERROR "clang++ not found")
endif()

set(
    CMAKE_C_COMPILER
    "${CMAKE_C_COMPILER}"
    CACHE
    STRING
    "C compiler"
    FORCE
)

set(
    CMAKE_CXX_COMPILER
    "${CMAKE_CXX_COMPILER}"
    CACHE
    STRING
    "C++ compiler"
    FORCE
)