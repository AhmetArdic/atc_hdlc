add_library(hdlc STATIC
    hdlc.c
    hdlc_input.c
    hdlc_output.c
    hdlc_frame.c
    hdlc_frame_handlers.c
    hdlc_crc.c
)

target_include_directories(hdlc PUBLIC ${CMAKE_SOURCE_DIR}/inc)
