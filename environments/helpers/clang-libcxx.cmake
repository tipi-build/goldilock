
# run the passed llvm-config to retrieve information about that installation
macro(llvm_config_get llvm_config_binary var flag)
  set(result_code)
  execute_process(
    COMMAND ${llvm_config_binary} --link-static --${flag}
    RESULT_VARIABLE result_code
    OUTPUT_VARIABLE LLVM_${var}
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  if(result_code)
    message(FATAL_ERROR "Failed to execute llvm-config ('${llvm_config_binary}', result code: '${result_code})'")
  else()
    if(${ARGV3})
      file(TO_CMAKE_PATH "${LLVM_${var}}" LLVM_${var})
    endif()
  endif()
endmacro()

function(llvm_get_version llvm_config_executable OUT_result)
  llvm_config_get(${llvm_config_executable} VERSION_STRING version)

  # The LLVM version string _may_ contain a git/svn suffix, so match only the x.y.z part
  string(REGEX MATCH "^[0-9]+[.][0-9]+[.][0-9]+" LLVM_VERSION_BASE_STRING "${LLVM_VERSION_STRING}")
  set(${OUT_result} ${LLVM_VERSION_BASE_STRING} PARENT_SCOPE)
endfunction()

function(clang_major_version_check_equal llvm_config_executable expected_version)
  llvm_get_version(${llvm_config_executable} version)
  string(REGEX REPLACE "([0-9]+).*" "\\1" major_version "${version}" )

  if (NOT "${major_version}" VERSION_EQUAL "${expected_version}")
    message(FATAL_ERROR "Unsupported LLVM version ${LLVM_VERSION_STRING} found (${llvm_config_executable}). Major version equal to ${expected_version} is required.")
  endif()
endfunction()

function(clang_check_minimum_version llvm_config_executable expected_version)
  llvm_get_version(${llvm_config_executable} version)

  if (NOT "${version}" VERSION_GREATER_EQUAL "${expected_version}")
    message(FATAL_ERROR "Unsupported LLVM version ${LLVM_VERSION_STRING} found (${llvm_config_executable}). At least version ${expected_version} is required.")
  endif()
endfunction()


function(llvm_get_include_dir llvm_config_executable OUT_result)
  llvm_config_get(${llvm_config_executable} INCLUDE_DIRS includedir true)
  set(${OUT_result} ${LLVM_INCLUDE_DIRS} PARENT_SCOPE)
endfunction()

function(llvm_get_root_dir llvm_config_executable OUT_result)
  llvm_config_get(${llvm_config_executable} ROOT_DIR prefix true)
  set(${OUT_result} ${LLVM_ROOT_DIR} PARENT_SCOPE)
endfunction()

function(llvm_get_library_dir llvm_config_executable OUT_result)
  llvm_config_get(${llvm_config_executable} LIBRARY_DIRS libdir true)
  set(${OUT_result} ${LLVM_LIBRARY_DIRS} PARENT_SCOPE)
endfunction()

function(llvm_get_cmake_dir llvm_config_executable OUT_result)
  llvm_config_get(${llvm_config_executable} CMAKEDIR cmakedir true)
  set(${OUT_result} ${LLVM_CMAKEDIR} PARENT_SCOPE)
endfunction()

# find the paths to the static libc++ and libc++abi of the discovered llvm/clang installation
# Usages:
#
# find_libcxx_static(<llvm_config_binary> RESULT_LIBCXX_PATH <variable name> RESULT_LIBCXXABI_PATH <variable_name> [FAIL_NOTFOUND])
function(find_libcxx_static llvm_config_binary)

  set(options_params FAIL_NOTFOUND)
  set(one_value_args RESULT_LIBCXX_PATH RESULT_LIBCXXABI_PATH)
  set(multi_value_params )

  cmake_parse_arguments(FN_ARG "${options_params}" "${one_value_args}" "${multi_value_params}" ${ARGN})

  # interogate llvm-config
  llvm_get_library_dir(${llvm_config_binary} LLVM_LIBRARY_DIRS)

  set(libcxx_path "${LLVM_LIBRARY_DIRS}/libc++.a")
  set(libcxxabi_path "${LLVM_LIBRARY_DIRS}/libc++abi.a")

  if(EXISTS "${libcxx_path}" AND EXISTS "${libcxxabi_path}")
    set(${FN_ARG_RESULT_LIBCXX_PATH} "${libcxx_path}" PARENT_SCOPE)
    set(${FN_ARG_RESULT_LIBCXXABI_PATH} "${libcxxabi_path}" PARENT_SCOPE)
  elseif(FN_ARG_FAIL_NOTFOUND)
    message(FATAL_ERROR "Did not find a matching libc++ to the llvm/clang installation described by ${llvm_config_binary}")
  else()
    set(${FN_ARG_RESULT_LIBCXX_PATH} NOTFOUND PARENT_SCOPE)
    set(${FN_ARG_RESULT_LIBCXXABI_PATH} NOTFOUND PARENT_SCOPE)
  endif()
endfunction()


# Find a llvm/clang installtion on the system:
# Usage:
#
# find_clang(
#   [VERSIONS <list>]
#   [HINT_PATHS <list>]
#   [RESULT_CLANG_C_COMPILER <variable name>]
#   [RESULT_CLANG_CXX_COMPILER <variable name>]
#   [RESULT_LLVM_CONFIG_EXECUTABLE <variable name>]
#   [FAIL_NOTFOUND]
# )
function(find_clang)
  set(options_params
    FAIL_NOTFOUND
  )

  set(one_value_args
    VERSIONS
    HINT_PATHS
    RESULT_CLANG_C_COMPILER
    RESULT_CLANG_CXX_COMPILER
    RESULT_LLVM_CONFIG_EXECUTABLE
  )

  set(multi_value_params )

  cmake_parse_arguments(FN_ARG "${options_params}" "${one_value_args}" "${multi_value_params}" ${ARGN})

  set(clang_names "")
  set(clangpp_names "")
  set(llvm_config_names "")

  if(NOT FN_ARG_VERSIONS)
    list(APPEND clang_names "clang")
    list(APPEND clangpp_names "clang++")
    list(APPEND llvm_config_names "llvm-config")
  else()
    foreach(version IN LISTS FN_ARG_VERSIONS)
      if(version STREQUAL "")
        list(APPEND clang_names "clang")
        list(APPEND clangpp_names "clang++")
        list(APPEND llvm_config_names "llvm-config")
      else()
        list(APPEND clang_names "clang-${version}")
        list(APPEND clangpp_names "clang++-${version}")
        list(APPEND llvm_config_names "llvm-config-${version}")
      endif()
    endforeach()
  endif()

  list(REMOVE_DUPLICATES clang_names)
  list(REMOVE_DUPLICATES clangpp_names)
  list(REMOVE_DUPLICATES llvm_config_names)

  find_program(LLVM_CONFIG_EXECUTABLE
    NAMES ${llvm_config_names}
    PATHS ${FN_ARG_HINT_PATHS}
    ENV LLVM_PATH
    NO_CACHE
  )

  if(LLVM_CONFIG_EXECUTABLE)
    message(STATUS "Found llvm-config: ${LLVM_CONFIG_EXECUTABLE}")
  elseif(FN_ARG_FAIL_NOTFOUND)
    message(FATAL_ERROR "Can't find program: llvm-config (searched for ${llvm_config_names})")
  endif()

  find_program(CLANG_C_COMPILER_EXECUTABLE
    NAMES ${clang_names}
    PATHS ${FN_ARG_HINT_PATHS}
    ENV LLVM_PATH
    NO_CACHE
  )

  if(CLANG_C_COMPILER_EXECUTABLE)
    message(STATUS "Found clang: ${CLANG_C_COMPILER_EXECUTABLE}")
  elseif(FN_ARG_FAIL_NOTFOUND)
    message(FATAL_ERROR "Can't fing program: clang (searched for ${clang_names})")
  endif()

  find_program(CLANG_CXX_COMPILER_EXECUTABLE
    NAMES ${clangpp_names}
    PATHS ${FN_ARG_HINT_PATHS}
    ENV LLVM_PATH
    NO_CACHE
  )

  if(CLANG_CXX_COMPILER_EXECUTABLE)
    message(STATUS "Found clang++: ${CLANG_CXX_COMPILER_EXECUTABLE}")
  elseif(FN_ARG_FAIL_NOTFOUND)
    message(FATAL_ERROR "Can't fing program: clang++ (searched for ${clangpp_names})")
  endif()

  if(FN_ARG_RESULT_CLANG_C_COMPILER)
    set(${FN_ARG_RESULT_CLANG_C_COMPILER} ${CLANG_C_COMPILER_EXECUTABLE} PARENT_SCOPE)
  endif()

  if(FN_ARG_RESULT_CLANG_CXX_COMPILER)
    set(${FN_ARG_RESULT_CLANG_CXX_COMPILER} ${CLANG_CXX_COMPILER_EXECUTABLE} PARENT_SCOPE)
  endif()

  if(FN_ARG_RESULT_LLVM_CONFIG_EXECUTABLE)
    set(${FN_ARG_RESULT_LLVM_CONFIG_EXECUTABLE} ${LLVM_CONFIG_EXECUTABLE} PARENT_SCOPE)
  endif()

endfunction()
