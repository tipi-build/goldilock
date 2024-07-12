# Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
# SPDX-License-Identifier: GPL-2.0-only OR Proprietary

# goldilock
add_executable(goldilock "${CMAKE_CURRENT_LIST_DIR}/goldilock.cpp" )
set_target_properties(goldilock PROPERTIES OUTPUT_NAME goldilock)
target_link_libraries(goldilock libgoldilock-utils)
install(TARGETS goldilock RUNTIME DESTINATION bin)