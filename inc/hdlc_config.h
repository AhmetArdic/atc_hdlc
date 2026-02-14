/**
 * @file hdlc_config.h
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief Configuration parameters for the HDLC Library.
 *
 * This file contains compile-time configuration macros to tailor the HDLC
 * library to specific embedded system constraints, such as memory usage (MTU),
 * window sizes, and internal buffer limits.
 *
 * @note Adjust these values based on your target architecture and application
 * requirements.
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
 * Default is "atc_".
 */
#ifndef ATC_HDLC_PREFIX
#define ATC_HDLC_PREFIX atc_
#endif

/* Helper macros for token concatenation */
#define _ATC_HDLC_CONCAT(a, b) a##b
#define ATC_HDLC_CONCAT(a, b) _ATC_HDLC_CONCAT(a, b)

/**
 * @brief Macro to generate prefixed symbol names.
 * Usage: ATC_HDLC_NAME(hdlc_init) -> atc_hdlc_init
 */
#define ATC_HDLC_NAME(name) ATC_HDLC_CONCAT(ATC_HDLC_PREFIX, name)

/*
 * --------------------------------------------------------------------------
 * PUBLIC SYMBOL MAPPINGS
 * --------------------------------------------------------------------------
 * These macros verify that every public symbol uses the configured prefix.
 * This allows the library to be namespaced (e.g. atc_hdlc_init).
 */

/* Functions */
/* Functions */
#define hdlc_stream_init                                                ATC_HDLC_NAME(hdlc_stream_init)
#define hdlc_stream_input_byte                                          ATC_HDLC_NAME(hdlc_stream_input_byte)
#define hdlc_stream_input_bytes                                         ATC_HDLC_NAME(hdlc_stream_input_bytes)
#define hdlc_stream_output_frame                                        ATC_HDLC_NAME(hdlc_stream_output_frame)
#define hdlc_frame_pack                                                 ATC_HDLC_NAME(hdlc_frame_pack)
#define hdlc_frame_unpack                                               ATC_HDLC_NAME(hdlc_frame_unpack)
#define hdlc_create_i_ctrl                                              ATC_HDLC_NAME(hdlc_create_i_ctrl)
#define hdlc_create_s_ctrl                                              ATC_HDLC_NAME(hdlc_create_s_ctrl)
#define hdlc_create_u_ctrl                                              ATC_HDLC_NAME(hdlc_create_u_ctrl)
#define hdlc_stream_output_packet_start                                 ATC_HDLC_NAME(hdlc_stream_output_packet_start)
#define hdlc_stream_output_packet_information_byte                      ATC_HDLC_NAME(hdlc_stream_output_packet_information_byte)
#define hdlc_stream_output_packet_information_bytes                     ATC_HDLC_NAME(hdlc_stream_output_packet_information_bytes)
#define hdlc_stream_output_packet_end                                   ATC_HDLC_NAME(hdlc_stream_output_packet_end)

/* Types */
#define hdlc_u8                                                         ATC_HDLC_NAME(hdlc_u8)
#define hdlc_u16                                                        ATC_HDLC_NAME(hdlc_u16)
#define hdlc_u32                                                        ATC_HDLC_NAME(hdlc_u32)
#define hdlc_bool                                                       ATC_HDLC_NAME(hdlc_bool)
#define hdlc_frame_type_t                                               ATC_HDLC_NAME(hdlc_frame_type_t)
#define hdlc_control_t                                                  ATC_HDLC_NAME(hdlc_control_t)
#define hdlc_frame_t                                                    ATC_HDLC_NAME(hdlc_frame_t)
#define hdlc_output_byte_cb_t                                           ATC_HDLC_NAME(hdlc_output_byte_cb_t)
#define hdlc_on_frame_cb_t                                              ATC_HDLC_NAME(hdlc_on_frame_cb_t)
#define hdlc_context_t                                                  ATC_HDLC_NAME(hdlc_context_t)

#ifdef __cplusplus
}
#endif

#endif // HDLC_CONFIG_H
