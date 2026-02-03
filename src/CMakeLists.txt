add_library(hdlc STATIC
    hdlc.c
    hdlc_crc.c
)

target_include_directories(hdlc PUBLIC ${CMAKE_SOURCE_DIR}/inc)
