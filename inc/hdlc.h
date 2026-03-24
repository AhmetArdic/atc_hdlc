/*
 * Copyright (C) 2026 Ahmet Talha ARDIC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef ATC_HDLC_H
#define ATC_HDLC_H

#include "hdlc_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize HDLC station.
 *
 * @param ctx       Context (user allocates).
 * @param config    Protocol settings (must stay valid).
 * @param platform  Callbacks (on_send required, on_data/on_event optional).
 * @param tx_window TX buffer for reliable transmission (NULL = disable).
 * @param rx_buf    RX buffer (required).
 * @return ATC_HDLC_OK or error code.
 */
atc_hdlc_error_t atc_hdlc_init(atc_hdlc_context_t        *ctx,
                                 const atc_hdlc_config_t   *config,
                                 const atc_hdlc_platform_t *platform,
                                 atc_hdlc_tx_window_t      *tx_window,
                                 atc_hdlc_rx_buffer_t      *rx_buf);

/**
 * @brief T1 timer expired (retransmission timeout).
 *
 * Call from your timer callback.
 *
 * @param ctx Context.
 */
void atc_hdlc_t1_expired(atc_hdlc_context_t *ctx);

/**
 * @brief T2 timer expired (delayed ACK).
 *
 * Call from your timer callback. Sends RR automatically.
 *
 * @param ctx Context.
 */
void atc_hdlc_t2_expired(atc_hdlc_context_t *ctx);

/**
 * @brief Connect to peer (sends SABM).
 *
 * @param ctx       Context.
 * @param peer_addr Peer address.
 * @return ATC_HDLC_OK or error code.
 */
atc_hdlc_error_t atc_hdlc_link_setup(atc_hdlc_context_t *ctx,
                                       atc_hdlc_u8 peer_addr);

/**
 * @brief Disconnect (sends DISC).
 *
 * @param ctx Context.
 * @return ATC_HDLC_OK or error code.
 */
atc_hdlc_error_t atc_hdlc_disconnect(atc_hdlc_context_t *ctx);

/**
 * @brief Reset and reconnect.
 *
 * Use after FRMR_ERROR or for recovery.
 *
 * @param ctx Context.
 * @return ATC_HDLC_OK always.
 */
atc_hdlc_error_t atc_hdlc_link_reset(atc_hdlc_context_t *ctx);

/**
 * @brief Check if connected.
 *
 * @param ctx Context.
 * @return true if CONNECTED.
 */
atc_hdlc_bool atc_hdlc_is_connected(const atc_hdlc_context_t *ctx);

/**
 * @brief Set local busy condition.
 *
 * @param ctx  Context.
 * @param busy true = busy (tell peer to wait), false = ready.
 * @return ATC_HDLC_OK or error code.
 */
atc_hdlc_error_t atc_hdlc_set_local_busy(atc_hdlc_context_t *ctx,
                                           atc_hdlc_bool busy);

/**
 * @brief Send reliable I-frame.
 *
 * @param ctx  Context.
 * @param data Payload.
 * @param len  Payload length.
 * @return ATC_HDLC_OK or error code.
 */
atc_hdlc_error_t atc_hdlc_transmit_i(atc_hdlc_context_t *ctx,
                                       const atc_hdlc_u8  *data,
                                       atc_hdlc_u32        len);

/**
 * @brief Send UI-frame (connectionless).
 *
 * Works in any state.
 *
 * @param ctx     Context.
 * @param address Destination.
 * @param data    Payload (can be NULL).
 * @param len     Length.
 * @return ATC_HDLC_OK or error code.
 */
atc_hdlc_error_t atc_hdlc_transmit_ui(atc_hdlc_context_t *ctx,
                                        atc_hdlc_u8         address,
                                        const atc_hdlc_u8  *data,
                                        atc_hdlc_u32        len);

/**
 * @brief Send TEST frame and wait for echo.
 *
 * @param ctx     Context.
 * @param address Destination.
 * @param data    Test pattern (can be NULL).
 * @param len     Length.
 * @return ATC_HDLC_OK or error code.
 */
atc_hdlc_error_t atc_hdlc_transmit_test(atc_hdlc_context_t *ctx,
                                          atc_hdlc_u8         address,
                                          const atc_hdlc_u8  *data,
                                          atc_hdlc_u32        len);

/**
 * @brief Feed multiple bytes.
 *
 * @warning Not ISR-safe
 *
 * @param ctx  Context.
 * @param data Byte array.
 * @param len  Count.
 */
void atc_hdlc_data_in(atc_hdlc_context_t *ctx,
                      const atc_hdlc_u8  *data,
                      atc_hdlc_u32        len);

/**
 * @brief Start UI frame TX.
 *
 * @param ctx     Context.
 * @param address Address.
 */
void atc_hdlc_transmit_start_ui(atc_hdlc_context_t *ctx,
                                 atc_hdlc_u8 address);

/**
 * @brief Add bytes to TX stream.
 *
 * @param ctx  Context.
 * @param data Data.
 * @param len  Length.
 */
void atc_hdlc_transmit_data(atc_hdlc_context_t *ctx,
                            const atc_hdlc_u8  *data,
                            atc_hdlc_u32        len);

/**
 * @brief Finish streaming TX.
 *
 * Sends FCS and closing flag.
 *
 * @param ctx Context.
 */
void atc_hdlc_transmit_end(atc_hdlc_context_t *ctx);

/**
 * @brief Pack frame to buffer.
 *
 * Standalone (no context needed).
 *
 * @param frame       Frame to encode.
 * @param buffer      Output buffer.
 * @param buffer_len  Buffer size.
 * @param encoded_len Bytes written.
 * @return true on success.
 */
atc_hdlc_bool atc_hdlc_frame_pack(const atc_hdlc_frame_t *frame,
                                   atc_hdlc_u8            *buffer,
                                   atc_hdlc_u32            buffer_len,
                                   atc_hdlc_u32           *encoded_len);

/**
 * @brief Unpack frame from buffer.
 *
 * Standalone (no context needed).
 *
 * @param buffer          Input data.
 * @param buffer_len      Length.
 * @param frame           Output frame.
 * @param flat_buffer     Work buffer.
 * @param flat_buffer_len Work buffer size.
 * @return true if valid.
 */
atc_hdlc_bool atc_hdlc_frame_unpack(const atc_hdlc_u8 *buffer,
                                      atc_hdlc_u32       buffer_len,
                                      atc_hdlc_frame_t  *frame,
                                      atc_hdlc_u8       *flat_buffer,
                                      atc_hdlc_u32       flat_buffer_len);

/**
 * @brief Decode S-frame type.
 *
 * @param control Control byte.
 * @return S-frame type.
 */
atc_hdlc_s_frame_sub_type_t atc_hdlc_get_s_frame_sub_type(atc_hdlc_u8 control);

/**
 * @brief Decode U-frame type.
 *
 * @param control Control byte.
 * @return U-frame type.
 */
atc_hdlc_u_frame_sub_type_t atc_hdlc_get_u_frame_sub_type(atc_hdlc_u8 control);

/**
 * @brief Get current state.
 *
 * @param ctx Context.
 * @return State value.
 */
atc_hdlc_state_t atc_hdlc_get_state(const atc_hdlc_context_t *ctx);

/**
 * @brief Get free TX window slots.
 *
 * @param ctx Context.
 * @return Available slots (0 = full).
 */
atc_hdlc_u8 atc_hdlc_get_window_available(const atc_hdlc_context_t *ctx);

/**
 * @brief Check if ACK pending.
 *
 * @param ctx Context.
 * @return true if T2 running.
 */
atc_hdlc_bool atc_hdlc_has_pending_ack(const atc_hdlc_context_t *ctx);

/**
 * @brief Get statistics.
 *
 * @param ctx Context.
 * @param out Output struct.
 */
void atc_hdlc_get_stats(const atc_hdlc_context_t *ctx, atc_hdlc_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ATC_HDLC_H */
