

add_library(test_lib INTERFACE)
add_library(goldilock::test_lib ALIAS test_lib)

target_include_directories(test_lib INTERFACE 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/>
)

target_link_libraries(test_lib INTERFACE     
  Boost::system 
  Boost::filesystem 
  Boost::uuid 
  Boost::included_unit_test_framework
  Boost::process 
  Boost::asio
  Threads::Threads
)

function(add_goldilock_test test_name source_files)
    message("Registering test ${test_name} (${source_files})")
    add_executable(${test_name} "${source_files}")
    set_target_properties(${test_name} PROPERTIES OUTPUT_NAME ${test_name})

    target_link_libraries(${test_name} goldilock::test_lib libgoldilock-utils::libgoldilock-utils)
    
    add_test(
        NAME ${test_name} 
        COMMAND "$<TARGET_FILE:${test_name}>"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}   # wd = project binary dir root so we can find goldilock exe    
    )

    # add to all tests
    set_property(TEST ${test_name} PROPERTY LABELS "ALL")

    if(NOT env_value STREQUAL "")
        set_property(TEST ${test_name} PROPERTY ENVIRONMENT "GOLDILOCK_TEST_ROOT_DIRECTORY=${CMAKE_CURRENT_LIST_DIR};BOOST_TEST_LOG_LEVEL=all")
    endif()

endfunction()

set(test_source_files
    "${CMAKE_CURRENT_LIST_DIR}/test_basics.cpp"
)

foreach(test_file IN LISTS test_source_files)
    get_filename_component(test_name ${test_file} NAME_WE) # without .cpp extension
    add_goldilock_test(${test_name} "${test_file}")    
endforeach()


set(test_support_apps_source_files
    "${CMAKE_CURRENT_LIST_DIR}/support_app_append_to_file.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/support_app_launcher.cpp"
)

foreach(source_file IN LISTS test_support_apps_source_files)
    get_filename_component(app_name ${source_file} NAME_WE) # without .cpp extension

    add_executable(${app_name} "${source_file}" )
    set_target_properties(${app_name} PROPERTIES OUTPUT_NAME ${app_name})
    target_link_libraries(${app_name} goldilock::test_lib cxxopts libgoldilock-utils::libgoldilock-utils)
endforeach()
