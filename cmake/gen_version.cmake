find_package(Git)

if(GIT_EXECUTABLE)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${WORKING_DIR}
        OUTPUT_VARIABLE GOLDILOCK_GIT_REVISION
        RESULT_VARIABLE ERROR_CODE
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

if(GOLDILOCK_GIT_REVISION STREQUAL "")
    set(GOLDILOCK_GIT_REVISION 0.0.0-unknown)
    message(WARNING "Failed to determine version from Git tags. Using default version \"${GOLDILOCK_GIT_REVISION}\".")
endif()

message(STATUS "Generated version tag: ${GOLDILOCK_GIT_REVISION}")

set(temp_file "${DST}.tmp")

configure_file(${SRC} ${temp_file} @ONLY)
file(COPY_FILE ${temp_file} ${DST} ONLY_IF_DIFFERENT) # avoid rebuilds because we touched an indentical file

if(EXISTS ${temp_file})
    file(REMOVE ${temp_file})
endif()