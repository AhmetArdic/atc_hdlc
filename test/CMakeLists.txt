add_executable(hdlc_test hdlc_test.c)

target_link_libraries(hdlc_test PRIVATE hdlc)

add_test(NAME HDLC_Unit_Tests COMMAND hdlc_test)


