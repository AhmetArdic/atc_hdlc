/**
 * @file hdlc_config.h
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief Configuration parameters for the HDLC Library.
 *
 * This file contains compile-time configuration macros to tailor the HDLC
 * library to specific embedded system constraints.
 */

#ifndef HDLC_CONFIG_H
#define HDLC_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * --------------------------------------------------------------------------
 * SYMBOL PREFIX CONFIGURATION
 * --------------------------------------------------------------------------
 */

/**
 * @brief Library Symbol Prefix
 *
 * Defines the prefix used for all public API functions and types.
 * Default is "atc_ / ATC_".
 */
#ifndef ATC_HDLC_PREFIX_LOWERCASE
#define ATC_HDLC_PREFIX_LOWERCASE atc_
#endif

#ifndef ATC_HDLC_PREFIX_UPPERCASE
#define ATC_HDLC_PREFIX_UPPERCASE ATC_
#endif

/* Helper macros for token concatenation */
#define _ATC_HDLC_CONCAT(a, b) a##b
#define ATC_HDLC_CONCAT(a, b) _ATC_HDLC_CONCAT(a, b)

/**
 * @brief Macro to generate prefixed symbol names.
 * Usage: ATC_HDLC_NAME(hdlc_init) -> atc_hdlc_init
 */
#define ATC_HDLC_NAME_LOWERCASE(name) ATC_HDLC_CONCAT(ATC_HDLC_PREFIX_LOWERCASE, name)
#define ATC_HDLC_NAME_UPPERCASE(name) ATC_HDLC_CONCAT(ATC_HDLC_PREFIX_UPPERCASE, name)

/*
 * --------------------------------------------------------------------------
 * PUBLIC SYMBOL MAPPINGS
 * --------------------------------------------------------------------------
 * These macros verify that every public symbol uses the configured prefix.
 * This allows the library to be namespaced (e.g. atc_hdlc_init).
 */

/* Functions */
#define hdlc_init                                                       ATC_HDLC_NAME_LOWERCASE(hdlc_init)
#define hdlc_input_byte                                                 ATC_HDLC_NAME_LOWERCASE(hdlc_input_byte)
#define hdlc_input_bytes                                                ATC_HDLC_NAME_LOWERCASE(hdlc_input_bytes)
#define hdlc_output_frame                                               ATC_HDLC_NAME_LOWERCASE(hdlc_output_frame)
#define hdlc_frame_pack                                                 ATC_HDLC_NAME_LOWERCASE(hdlc_frame_pack)
#define hdlc_frame_unpack                                               ATC_HDLC_NAME_LOWERCASE(hdlc_frame_unpack)
#define hdlc_create_i_ctrl                                              ATC_HDLC_NAME_LOWERCASE(hdlc_create_i_ctrl)
#define hdlc_create_s_ctrl                                              ATC_HDLC_NAME_LOWERCASE(hdlc_create_s_ctrl)
#define hdlc_create_u_ctrl                                              ATC_HDLC_NAME_LOWERCASE(hdlc_create_u_ctrl)
#define hdlc_output_packet_start                                        ATC_HDLC_NAME_LOWERCASE(hdlc_output_packet_start)
#define hdlc_output_packet_information_byte                             ATC_HDLC_NAME_LOWERCASE(hdlc_output_packet_information_byte)
#define hdlc_output_packet_information_bytes                            ATC_HDLC_NAME_LOWERCASE(hdlc_output_packet_information_bytes)
#define hdlc_output_packet_end                                          ATC_HDLC_NAME_LOWERCASE(hdlc_output_packet_end)
#define hdlc_send_ui                                                    ATC_HDLC_NAME_LOWERCASE(hdlc_send_ui)
#define hdlc_configure_addresses                                        ATC_HDLC_NAME_LOWERCASE(hdlc_configure_addresses)
#define hdlc_connect                                                    ATC_HDLC_NAME_LOWERCASE(hdlc_connect)
#define hdlc_disconnect                                                 ATC_HDLC_NAME_LOWERCASE(hdlc_disconnect)
#define hdlc_is_connected                                               ATC_HDLC_NAME_LOWERCASE(hdlc_is_connected)

/* Types */
#define hdlc_u8                                                         ATC_HDLC_NAME_LOWERCASE(hdlc_u8)
#define hdlc_u16                                                        ATC_HDLC_NAME_LOWERCASE(hdlc_u16)
#define hdlc_u32                                                        ATC_HDLC_NAME_LOWERCASE(hdlc_u32)
#define hdlc_bool                                                       ATC_HDLC_NAME_LOWERCASE(hdlc_bool)
#define hdlc_frame_type_t                                               ATC_HDLC_NAME_LOWERCASE(hdlc_frame_type_t)
#define hdlc_protocol_state_t                                           ATC_HDLC_NAME_LOWERCASE(hdlc_protocol_state_t)
#define hdlc_control_t                                                  ATC_HDLC_NAME_LOWERCASE(hdlc_control_t)
#define hdlc_frame_t                                                    ATC_HDLC_NAME_LOWERCASE(hdlc_frame_t)
#define hdlc_output_byte_cb_t                                           ATC_HDLC_NAME_LOWERCASE(hdlc_output_byte_cb_t)
#define hdlc_on_frame_cb_t                                              ATC_HDLC_NAME_LOWERCASE(hdlc_on_frame_cb_t)
#define hdlc_on_state_change_cb_t                                       ATC_HDLC_NAME_LOWERCASE(hdlc_on_state_change_cb_t)
#define hdlc_context_t                                                  ATC_HDLC_NAME_LOWERCASE(hdlc_context_t)

/* Frame Types */
#define HDLC_FRAME_I                                                    ATC_HDLC_NAME_UPPERCASE(HDLC_FRAME_I)
#define HDLC_FRAME_S                                                    ATC_HDLC_NAME_UPPERCASE(HDLC_FRAME_S)
#define HDLC_FRAME_U                                                    ATC_HDLC_NAME_UPPERCASE(HDLC_FRAME_U)
#define HDLC_FRAME_INVALID                                              ATC_HDLC_NAME_UPPERCASE(HDLC_FRAME_INVALID)

/* Protocol States */
#define HDLC_PROTOCOL_STATE_DISCONNECTED                                ATC_HDLC_NAME_UPPERCASE(HDLC_PROTOCOL_STATE_DISCONNECTED)
#define HDLC_PROTOCOL_STATE_CONNECTING                                  ATC_HDLC_NAME_UPPERCASE(HDLC_PROTOCOL_STATE_CONNECTING)
#define HDLC_PROTOCOL_STATE_CONNECTED                                   ATC_HDLC_NAME_UPPERCASE(HDLC_PROTOCOL_STATE_CONNECTED)
#define HDLC_PROTOCOL_STATE_DISCONNECTING                               ATC_HDLC_NAME_UPPERCASE(HDLC_PROTOCOL_STATE_DISCONNECTING)

#ifdef __cplusplus
}
#endif

#endif // HDLC_CONFIG_H
