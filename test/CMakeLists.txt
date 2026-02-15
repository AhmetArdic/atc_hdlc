add_executable(test_hdlc test_hdlc.c)
target_link_libraries(test_hdlc PRIVATE hdlc)
add_test(NAME HDLC_Unit_Tests COMMAND test_hdlc)

add_executable(test_connection_state test_connection_state.c)
target_link_libraries(test_connection_state PRIVATE hdlc)
add_test(NAME ConnectionStateTest COMMAND test_connection_state)
