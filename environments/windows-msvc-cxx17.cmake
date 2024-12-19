include_guard()

include(${CMAKE_CURRENT_LIST_DIR}/flags/vs-cxx17.cmake)
add_compile_definitions(
    # Prevents Windows.h from adding unnecessary includes    
    WIN32_LEAN_AND_MEAN  
    # Prevents Windows.h from defining min/max as macros 
    NOMINMAX
)
