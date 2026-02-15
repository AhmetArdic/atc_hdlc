add_executable(hdlc_test hdlc_test.c)

target_link_libraries(hdlc_test PRIVATE hdlc)

add_test(NAME HDLC_Unit_Tests COMMAND hdlc_test)



add_executable(test_connection_state test_connection_state.c)
target_link_libraries(test_connection_state PRIVATE hdlc)
add_test(NAME ConnectionStateTest COMMAND test_connection_state)

add_executable(test_ui_frames test_ui_frames.c)
target_link_libraries(test_ui_frames PRIVATE hdlc)
add_test(NAME UIFrameTest COMMAND test_ui_frames)
