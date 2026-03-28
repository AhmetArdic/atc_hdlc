find_package(Threads REQUIRED)
set(HELPERS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../helpers)

add_executable(virtual_com virtual_com.c ${HELPERS_DIR}/common.c ${HELPERS_DIR}/virtual_pipe.c)
target_link_libraries(virtual_com PRIVATE hdlc Threads::Threads)
target_compile_definitions(virtual_com PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

foreach(w 1 2 3 4 5 6 7)
    add_test(NAME virtual_com.file_transfer_w${w} COMMAND virtual_com file_transfer_w${w})
    set_tests_properties(virtual_com.file_transfer_w${w} PROPERTIES LABELS "integration")
endforeach()

foreach(w 4 5 6 7)
    add_test(NAME virtual_com.file_transfer_error_w${w} COMMAND virtual_com file_transfer_error_w${w})
    set_tests_properties(virtual_com.file_transfer_error_w${w} PROPERTIES LABELS "integration")

    add_test(NAME virtual_com.timeout_w${w} COMMAND virtual_com timeout_w${w})
    set_tests_properties(virtual_com.timeout_w${w} PROPERTIES LABELS "integration")

    add_test(NAME virtual_com.go_back_n_w${w} COMMAND virtual_com go_back_n_w${w})
    set_tests_properties(virtual_com.go_back_n_w${w} PROPERTIES LABELS "integration")
endforeach()

add_executable(physical_target physical_target.c ${HELPERS_DIR}/common.c)
target_link_libraries(physical_target PRIVATE hdlc Threads::Threads)
target_compile_definitions(physical_target PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
# physical_target requires a real MCU connected via serial port — not registered as a ctest entry.
# Run manually: physical_target [--port <dev>] [--baud <rate>] [w<1-7>]
