add_executable(test_hdlc test_hdlc.c test_common.c)
target_link_libraries(test_hdlc PRIVATE hdlc)
add_test(NAME HDLC_Unit_Tests COMMAND test_hdlc)

add_executable(test_connection_management test_connection_management.c test_common.c)
target_link_libraries(test_connection_management PRIVATE hdlc)
add_test(NAME ConnectionManagementTest COMMAND test_connection_management)

add_executable(test_reliable_transmission test_reliable_transmission.c test_common.c)
target_link_libraries(test_reliable_transmission PRIVATE hdlc)
add_test(NAME ReliableTransmissionTest COMMAND test_reliable_transmission)

add_executable(test_init_validation test_init_validation.c test_common.c)
target_link_libraries(test_init_validation PRIVATE hdlc)
add_test(NAME InitValidationTest COMMAND test_init_validation)

add_executable(test_error_codes test_error_codes.c test_common.c)
target_link_libraries(test_error_codes PRIVATE hdlc)
add_test(NAME ErrorCodeTest COMMAND test_error_codes)

find_package(Threads REQUIRED)
add_executable(test_virtual_com test_virtual_com.c test_common.c test_virtual_pipe.c)
target_link_libraries(test_virtual_com PRIVATE hdlc Threads::Threads)
target_compile_definitions(test_virtual_com PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME VirtualComTest COMMAND test_virtual_com)
set_tests_properties(VirtualComTest PROPERTIES LABELS "slow")

add_executable(test_physical_target test_physical_target.c test_common.c)
target_link_libraries(test_physical_target PRIVATE hdlc Threads::Threads)
target_compile_definitions(test_physical_target PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME PhysicalTargetTest COMMAND test_physical_target)
set_tests_properties(PhysicalTargetTest PROPERTIES LABELS "slow")
