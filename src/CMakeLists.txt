add_library(hdlc STATIC
    hdlc_crc.c
    hdlc_station.c
    hdlc_in.c
    hdlc_out.c
)

target_include_directories(hdlc PUBLIC ${CMAKE_SOURCE_DIR}/inc)
target_compile_options(hdlc PRIVATE -Wall -Wextra -Wshadow -Wconversion)
