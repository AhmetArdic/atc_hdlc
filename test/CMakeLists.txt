add_executable(test_hdlc test_hdlc.c test_common.c)
target_link_libraries(test_hdlc PRIVATE hdlc)
add_test(NAME HDLC_Unit_Tests COMMAND test_hdlc)

add_executable(test_connection_management test_connection_management.c test_common.c)
target_link_libraries(test_connection_management PRIVATE hdlc)
add_test(NAME ConnectionManagementTest COMMAND test_connection_management)

add_executable(test_reliable_transmission test_reliable_transmission.c test_common.c)
target_link_libraries(test_reliable_transmission PRIVATE hdlc)
add_test(NAME ReliableTransmissionTest COMMAND test_reliable_transmission)

find_package(Threads REQUIRED)
add_executable(test_virtual_com test_virtual_com.c test_common.c)
target_link_libraries(test_virtual_com PRIVATE hdlc Threads::Threads)
target_compile_definitions(test_virtual_com PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME VirtualComTest COMMAND test_virtual_com)
