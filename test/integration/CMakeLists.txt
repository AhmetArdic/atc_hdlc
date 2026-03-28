find_package(Threads REQUIRED)
set(HELPERS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../helpers)

add_executable(virtual_com virtual_com.c ${HELPERS_DIR}/common.c ${HELPERS_DIR}/virtual_pipe.c)
target_link_libraries(virtual_com PRIVATE hdlc Threads::Threads)
target_compile_definitions(virtual_com PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME VirtualComTest COMMAND virtual_com)
set_tests_properties(VirtualComTest PROPERTIES LABELS "integration")

add_executable(physical_target physical_target.c ${HELPERS_DIR}/common.c)
target_link_libraries(physical_target PRIVATE hdlc Threads::Threads)
target_compile_definitions(physical_target PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME PhysicalTargetTest COMMAND physical_target)
set_tests_properties(PhysicalTargetTest PROPERTIES LABELS "integration;slow")
