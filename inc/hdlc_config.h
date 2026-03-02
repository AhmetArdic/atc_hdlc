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
#define hdlc_configure_addresses                                        ATC_HDLC_NAME_LOWERCASE(hdlc_configure_addresses)
#define hdlc_connect                                                    ATC_HDLC_NAME_LOWERCASE(hdlc_connect)
#define hdlc_disconnect                                                 ATC_HDLC_NAME_LOWERCASE(hdlc_disconnect)
#define hdlc_is_connected                                               ATC_HDLC_NAME_LOWERCASE(hdlc_is_connected)
#define hdlc_tick                                                       ATC_HDLC_NAME_LOWERCASE(hdlc_tick)
#define hdlc_input_byte                                                 ATC_HDLC_NAME_LOWERCASE(hdlc_input_byte)
#define hdlc_input_bytes                                                ATC_HDLC_NAME_LOWERCASE(hdlc_input_bytes)
#define hdlc_output_frame                                               ATC_HDLC_NAME_LOWERCASE(hdlc_output_frame)
#define hdlc_frame_pack                                                 ATC_HDLC_NAME_LOWERCASE(hdlc_frame_pack)
#define hdlc_frame_unpack                                               ATC_HDLC_NAME_LOWERCASE(hdlc_frame_unpack)
#define hdlc_output_frame_ui                                            ATC_HDLC_NAME_LOWERCASE(hdlc_output_frame_ui)
#define hdlc_output_frame_test                                          ATC_HDLC_NAME_LOWERCASE(hdlc_output_frame_test)
#define hdlc_output_frame_i                                             ATC_HDLC_NAME_LOWERCASE(hdlc_output_frame_i)
#define hdlc_output_frame_start                                         ATC_HDLC_NAME_LOWERCASE(hdlc_output_frame_start)
#define hdlc_output_frame_information_byte                              ATC_HDLC_NAME_LOWERCASE(hdlc_output_frame_information_byte)
#define hdlc_output_frame_information_bytes                             ATC_HDLC_NAME_LOWERCASE(hdlc_output_frame_information_bytes)
#define hdlc_output_frame_end                                           ATC_HDLC_NAME_LOWERCASE(hdlc_output_frame_end)
#define hdlc_output_frame_start_ui                                      ATC_HDLC_NAME_LOWERCASE(hdlc_output_frame_start_ui)
#define hdlc_output_frame_start_test                                    ATC_HDLC_NAME_LOWERCASE(hdlc_output_frame_start_test)
#define hdlc_output_frame_start_i                                       ATC_HDLC_NAME_LOWERCASE(hdlc_output_frame_start_i)
#define hdlc_create_i_ctrl                                              ATC_HDLC_NAME_LOWERCASE(hdlc_create_i_ctrl)
#define hdlc_create_s_ctrl                                              ATC_HDLC_NAME_LOWERCASE(hdlc_create_s_ctrl)
#define hdlc_create_u_ctrl                                              ATC_HDLC_NAME_LOWERCASE(hdlc_create_u_ctrl)
#define hdlc_get_s_frame_sub_type                                       ATC_HDLC_NAME_LOWERCASE(hdlc_get_s_frame_sub_type)
#define hdlc_get_u_frame_sub_type                                       ATC_HDLC_NAME_LOWERCASE(hdlc_get_u_frame_sub_type)

/* Types */
#define hdlc_u8                                                         ATC_HDLC_NAME_LOWERCASE(hdlc_u8)
#define hdlc_u16                                                        ATC_HDLC_NAME_LOWERCASE(hdlc_u16)
#define hdlc_u32                                                        ATC_HDLC_NAME_LOWERCASE(hdlc_u32)
#define hdlc_bool                                                       ATC_HDLC_NAME_LOWERCASE(hdlc_bool)
#define hdlc_frame_type_t                                               ATC_HDLC_NAME_LOWERCASE(hdlc_frame_type_t)
#define hdlc_s_frame_sub_type_t                                         ATC_HDLC_NAME_LOWERCASE(hdlc_s_frame_sub_type_t)
#define hdlc_u_frame_sub_type_t                                         ATC_HDLC_NAME_LOWERCASE(hdlc_u_frame_sub_type_t)
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

/* S-Frame Sub-Types */
#define HDLC_S_FRAME_TYPE_RR                                            ATC_HDLC_NAME_UPPERCASE(HDLC_S_FRAME_TYPE_RR)
#define HDLC_S_FRAME_TYPE_RNR                                           ATC_HDLC_NAME_UPPERCASE(HDLC_S_FRAME_TYPE_RNR)
#define HDLC_S_FRAME_TYPE_REJ                                           ATC_HDLC_NAME_UPPERCASE(HDLC_S_FRAME_TYPE_REJ)
#define HDLC_S_FRAME_TYPE_UNKNOWN                                       ATC_HDLC_NAME_UPPERCASE(HDLC_S_FRAME_TYPE_UNKNOWN)

/* U-Frame Sub-Types */
#define HDLC_U_FRAME_TYPE_SABM                                          ATC_HDLC_NAME_UPPERCASE(HDLC_U_FRAME_TYPE_SABM)
#define HDLC_U_FRAME_TYPE_SNRM                                          ATC_HDLC_NAME_UPPERCASE(HDLC_U_FRAME_TYPE_SNRM)
#define HDLC_U_FRAME_TYPE_SARM                                          ATC_HDLC_NAME_UPPERCASE(HDLC_U_FRAME_TYPE_SARM)
#define HDLC_U_FRAME_TYPE_SABME                                         ATC_HDLC_NAME_UPPERCASE(HDLC_U_FRAME_TYPE_SABME)
#define HDLC_U_FRAME_TYPE_SNRME                                         ATC_HDLC_NAME_UPPERCASE(HDLC_U_FRAME_TYPE_SNRME)
#define HDLC_U_FRAME_TYPE_SARME                                         ATC_HDLC_NAME_UPPERCASE(HDLC_U_FRAME_TYPE_SARME)
#define HDLC_U_FRAME_TYPE_DISC                                          ATC_HDLC_NAME_UPPERCASE(HDLC_U_FRAME_TYPE_DISC)
#define HDLC_U_FRAME_TYPE_UA                                            ATC_HDLC_NAME_UPPERCASE(HDLC_U_FRAME_TYPE_UA)
#define HDLC_U_FRAME_TYPE_DM                                            ATC_HDLC_NAME_UPPERCASE(HDLC_U_FRAME_TYPE_DM)
#define HDLC_U_FRAME_TYPE_FRMR                                          ATC_HDLC_NAME_UPPERCASE(HDLC_U_FRAME_TYPE_FRMR)
#define HDLC_U_FRAME_TYPE_UI                                            ATC_HDLC_NAME_UPPERCASE(HDLC_U_FRAME_TYPE_UI)
#define HDLC_U_FRAME_TYPE_TEST                                          ATC_HDLC_NAME_UPPERCASE(HDLC_U_FRAME_TYPE_TEST)
#define HDLC_U_FRAME_TYPE_UNKNOWN                                       ATC_HDLC_NAME_UPPERCASE(HDLC_U_FRAME_TYPE_UNKNOWN)

/* Protocol States */
#define HDLC_PROTOCOL_STATE_DISCONNECTED                                ATC_HDLC_NAME_UPPERCASE(HDLC_PROTOCOL_STATE_DISCONNECTED)
#define HDLC_PROTOCOL_STATE_CONNECTING                                  ATC_HDLC_NAME_UPPERCASE(HDLC_PROTOCOL_STATE_CONNECTING)
#define HDLC_PROTOCOL_STATE_CONNECTED                                   ATC_HDLC_NAME_UPPERCASE(HDLC_PROTOCOL_STATE_CONNECTED)
#define HDLC_PROTOCOL_STATE_DISCONNECTING                               ATC_HDLC_NAME_UPPERCASE(HDLC_PROTOCOL_STATE_DISCONNECTING)

/*
 * --------------------------------------------------------------------------
 * TIMER DEFAULTS
 * --------------------------------------------------------------------------
 */

/**
 * @brief Default retransmission (T1) timeout in ticks.
 *
 * Used as the default value for the retransmit_timeout parameter
 * in hdlc_init(). Can be overridden at init time.
 * The actual time depends on how frequently hdlc_tick() is called.
 */
#ifndef HDLC_DEFAULT_RETRANSMIT_TIMEOUT
#define HDLC_DEFAULT_RETRANSMIT_TIMEOUT  1000
#endif

/**
 * @brief Default ACK delay (T2) timeout in ticks.
 *
 * Used as the default value for the ack_delay_timeout parameter
 * in hdlc_init(). Prevents immediate RR transmission, allowing
 * piggybacking on outgoing I-frames.
 */
#ifndef HDLC_DEFAULT_ACK_DELAY_TIMEOUT
#define HDLC_DEFAULT_ACK_DELAY_TIMEOUT  10
#endif

/**
 * @brief Default Contention delay timeout in ticks.
 *
 * Used for backoff when two peers send SABM concurrently to prevent deadlocks.
 */
#ifndef HDLC_DEFAULT_CONTENTION_DELAY_TIMEOUT
#define HDLC_DEFAULT_CONTENTION_DELAY_TIMEOUT  100
#endif

/*
 * --------------------------------------------------------------------------
 * RETRY COUNT DEFAULTS
 * --------------------------------------------------------------------------
 */

/**
 * @brief Default maximum retry count (N2) before link failure.
 *
 * Used as the default value for the max_retry_count parameter
 * in hdlc_init(). Can be overridden at init time.
 */
#ifndef HDLC_DEFAULT_MAX_RETRY_COUNT
#define HDLC_DEFAULT_MAX_RETRY_COUNT  3
#endif

/*
 * --------------------------------------------------------------------------
 * WINDOW SIZE DEFAULTS
 * --------------------------------------------------------------------------
 */

/**
 * @brief Default transmit window size for Go-Back-N.
 *
 * Valid range: 1..7. Window=1 is equivalent to Stop-and-Wait.
 * Can be overridden at compile time or at init time.
 */
#ifndef HDLC_DEFAULT_WINDOW_SIZE
#define HDLC_DEFAULT_WINDOW_SIZE  1
#endif

/*
 * --------------------------------------------------------------------------
 * DEBUG LOGGING
 * --------------------------------------------------------------------------
 */

/**
 * @brief Enable debug logging macros
 * 
 * Set to 1 to enable HDLC debug printouts. Requires <stdio.h>.
 */
#ifndef HDLC_ENABLE_DEBUG_LOGS
#define HDLC_ENABLE_DEBUG_LOGS 0
#endif

#if HDLC_ENABLE_DEBUG_LOGS
#include <stdio.h>
#define HDLC_LOG_DEBUG(fmt, ...) printf("[HDLC DEBUG] " fmt "\n", ##__VA_ARGS__)
#define HDLC_LOG_WARN(fmt, ...)  printf("[HDLC WARN]  " fmt "\n", ##__VA_ARGS__)
#define HDLC_LOG_ERROR(fmt, ...) printf("[HDLC ERROR] " fmt "\n", ##__VA_ARGS__)
#else
#define HDLC_LOG_DEBUG(fmt, ...)
#define HDLC_LOG_WARN(fmt, ...)
#define HDLC_LOG_ERROR(fmt, ...)
#endif

#ifdef __cplusplus
}
#endif

#endif // HDLC_CONFIG_H
