include_guard()

include(${CMAKE_CURRENT_LIST_DIR}/flags/cxx17.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/helpers/clang-libcxx.cmake)

set(expected_clang_version 14)

find_clang(
  VERSIONS ${expected_clang_version}
  RESULT_CLANG_C_COMPILER clang_C_COMPILER
  RESULT_CLANG_CXX_COMPILER clang_CXX_COMPILER
  RESULT_LLVM_CONFIG_EXECUTABLE llvm_config_executable
  FAIL_NOTFOUND
)

clang_major_version_check_equal(${llvm_config_executable} ${expected_clang_version})

set(CMAKE_C_COMPILER "${clang_C_COMPILER}" CACHE STRING "C compiler" FORCE)
set(CMAKE_CXX_COMPILER "${clang_CXX_COMPILER}" CACHE STRING "C++ compiler" FORCE)

# true full static build by specifying the paths to libc++ and libc++abi as we got them through discovery
# the combination of -static, -stdlib=libc++, -static-libstdc++ and the full path to the libc++(abi) archives
# yield the desired result
find_libcxx_static(${llvm_config_executable} RESULT_LIBCXX_PATH libcxx_path RESULT_LIBCXXABI_PATH libcxxabi_path FAIL_NOTFOUND)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld -static -stdlib=libc++ -static-libstdc++ ${libcxx_path} ${libcxxabi_path}")
set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS} -stdlib=libc++")

message(STATUS "Using libc++ at ${libcxx_path} for static build")
message(STATUS "Using libc++abi at ${libcxxabi_path} for static build")
