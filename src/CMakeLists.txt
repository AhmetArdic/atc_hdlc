add_library(hdlc STATIC
    frame/hdlc_frame.c
    frame/hdlc_crc.c
    station/hdlc_station.c
    station/hdlc_in.c
    station/hdlc_out.c
)

target_include_directories(hdlc PUBLIC ${CMAKE_SOURCE_DIR}/inc)
