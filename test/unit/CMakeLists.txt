set(HELPERS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../helpers)

add_executable(hdlc_unit_test hdlc.c ${HELPERS_DIR}/common.c)
target_link_libraries(hdlc_unit_test PRIVATE hdlc)

foreach(t
    test_basic_frame
    test_empty_information
    test_byte_stuffing_heavy
    test_garbage_noise
    test_consecutive_flags
    test_min_size_rejection
    test_aborted_frame
    test_crc_error_injection
    test_input_buffer_overflow
    test_streaming_large_payload
    test_control_field_i
    test_control_field_s
    test_ui_frame_transmission
    test_ui_frame_reception
    test_test_frame
)
    add_test(NAME hdlc.${t} COMMAND hdlc_unit_test ${t})
    set_tests_properties(hdlc.${t} PROPERTIES LABELS "unit")
endforeach()

add_executable(connection_management connection_management.c ${HELPERS_DIR}/common.c)
target_link_libraries(connection_management PRIVATE hdlc)

foreach(t
    test_init_state
    test_connect_sends_sabm
    test_connect_complete_on_ua
    test_disconnect_flow
    test_passive_open
    test_frmr_reception
    test_extended_mode_rejection
    test_contention_resolution_winner
    test_link_reset
    test_peer_disconnect
    test_event_callbacks
    test_t1_timer_callbacks
    test_frmr_send_invalid_nr
    test_frmr_error_lockdown
    test_dm_on_connecting
    test_duplicate_rej_guard
)
    add_test(NAME connection_management.${t} COMMAND connection_management ${t})
    set_tests_properties(connection_management.${t} PROPERTIES LABELS "unit")
endforeach()

add_executable(reliable_transmission reliable_transmission.c ${HELPERS_DIR}/common.c)
target_link_libraries(reliable_transmission PRIVATE hdlc)

foreach(t
    test_reliable_transmission
    test_sequence_rollover
    test_duplicate_ack_ignored
    test_piggyback_ack
    test_window_size_2_basic
    test_gobackn_retransmit
    test_window7_mid_rej
    test_throughput_benchmark
    test_nr_modulo_validation
    test_nr_edge_cases
    test_state_initialization
    test_public_query_api
    test_set_local_busy
    test_local_busy_rnr_response
    test_rnr_reception
    test_t2_timer_callbacks
    test_n2_retry_connected
)
    add_test(NAME reliable_transmission.${t} COMMAND reliable_transmission ${t})
    set_tests_properties(reliable_transmission.${t} PROPERTIES LABELS "unit")
endforeach()

add_executable(init_validation init_validation.c ${HELPERS_DIR}/common.c)
target_link_libraries(init_validation PRIVATE hdlc)

foreach(t
    test_init_null_params
    test_init_unsupported_mode
    test_init_invalid_slot_count
    test_init_inconsistent_rx_buffer
    test_init_inconsistent_tx_window
    test_init_success_sets_state
)
    add_test(NAME init_validation.${t} COMMAND init_validation ${t})
    set_tests_properties(init_validation.${t} PROPERTIES LABELS "unit")
endforeach()

add_executable(error_codes error_codes.c ${HELPERS_DIR}/common.c)
target_link_libraries(error_codes PRIVATE hdlc)

foreach(t
    test_err_invalid_state
    test_err_invalid_param
    test_err_no_buffer
    test_err_window_full
    test_err_frame_too_large
    test_err_remote_busy
    test_err_max_retry
)
    add_test(NAME error_codes.${t} COMMAND error_codes ${t})
    set_tests_properties(error_codes.${t} PROPERTIES LABELS "unit")
endforeach()
