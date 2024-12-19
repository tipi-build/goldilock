include_guard()

include(${CMAKE_CURRENT_LIST_DIR}/compiler/clang.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/flags/cxx17.cmake)

add_compile_options(
  $<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=address>)
add_link_options(
  $<$<COMPILE_LANGUAGE:C,CXX>:-fsanitize=address>
  $<$<COMPILE_LANGUAGE:C,CXX>:-static-libsan>)
add_compile_options(
  $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-uninitialized>)

add_compile_options(
  $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-string-concatenation>)
add_compile_options(
  $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-shift-overflow>)
add_compile_options(
  $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-misleading-indentation>)


set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
