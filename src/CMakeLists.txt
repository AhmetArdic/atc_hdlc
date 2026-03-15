add_library(hdlc STATIC
    frame/hdlc_frame.c
    frame/hdlc_crc.c
    station/hdlc_station.c
    station/hdlc_input.c
    station/hdlc_output.c
    station/hdlc_frame_handlers.c
)

target_include_directories(hdlc PUBLIC ${CMAKE_SOURCE_DIR}/inc)
