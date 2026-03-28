set(HELPERS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../helpers)

add_executable(hdlc_unit_test hdlc.c ${HELPERS_DIR}/common.c)
target_link_libraries(hdlc_unit_test PRIVATE hdlc)
add_test(NAME HDLC_Unit_Tests COMMAND hdlc_unit_test)
set_tests_properties(HDLC_Unit_Tests PROPERTIES LABELS "unit")

add_executable(connection_management connection_management.c ${HELPERS_DIR}/common.c)
target_link_libraries(connection_management PRIVATE hdlc)
add_test(NAME ConnectionManagementTest COMMAND connection_management)
set_tests_properties(ConnectionManagementTest PROPERTIES LABELS "unit")

add_executable(reliable_transmission reliable_transmission.c ${HELPERS_DIR}/common.c)
target_link_libraries(reliable_transmission PRIVATE hdlc)
add_test(NAME ReliableTransmissionTest COMMAND reliable_transmission)
set_tests_properties(ReliableTransmissionTest PROPERTIES LABELS "unit")

add_executable(init_validation init_validation.c ${HELPERS_DIR}/common.c)
target_link_libraries(init_validation PRIVATE hdlc)
add_test(NAME InitValidationTest COMMAND init_validation)
set_tests_properties(InitValidationTest PROPERTIES LABELS "unit")

add_executable(error_codes error_codes.c ${HELPERS_DIR}/common.c)
target_link_libraries(error_codes PRIVATE hdlc)
add_test(NAME ErrorCodeTest COMMAND error_codes)
set_tests_properties(ErrorCodeTest PROPERTIES LABELS "unit")
