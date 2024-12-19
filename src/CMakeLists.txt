# Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
# SPDX-License-Identifier: GPL-2.0-only OR Proprietary

# generate version info
add_custom_target(version_header
    ${CMAKE_COMMAND} -DWORKING_DIR=${CMAKE_CURRENT_LIST_DIR} -DSRC=${CMAKE_SOURCE_DIR}/include/goldilock/version.hpp.in -DDST=${CMAKE_BINARY_DIR}/generated_sources/goldilock/version.hpp -P ${CMAKE_SOURCE_DIR}/cmake/gen_version.cmake
)

# goldilock
add_executable(goldilock "${CMAKE_CURRENT_LIST_DIR}/goldilock.cpp" )
set_target_properties(goldilock PROPERTIES OUTPUT_NAME goldilock)
target_link_libraries(goldilock libgoldilock-utils cxxopts::cxxopts Boost::system Boost::filesystem Boost::regex Boost::lexical_cast Boost::process Boost::scope_exit Boost::asio Boost::uuid Boost::serialization Boost::interprocess)
target_include_directories(goldilock PRIVATE ${CMAKE_BINARY_DIR}/generated_sources)
add_dependencies(goldilock version_header)

install(TARGETS goldilock RUNTIME DESTINATION bin)

set(CPACK_PACKAGE_NAME "goldilock")
set(CPACK_PACKAGE_VENDOR "tipi.build")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}")
include(CPack)