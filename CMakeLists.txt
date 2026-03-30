cmake_minimum_required(VERSION 3.10)
project(atc_hdlc C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Include directories
include_directories(inc)

enable_testing()

# Add subdirectories
add_subdirectory(src)
add_subdirectory(test)
add_subdirectory(example)
