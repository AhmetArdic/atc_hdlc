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
 * @param ctx    Context (user allocates).
 * @param params Init parameters.
 * @return ATC_HDLC_OK or error code.
 */
atc_hdlc_error_t atc_hdlc_init(atc_hdlc_context_t* ctx, atc_hdlc_params_t params);

/**
 * @brief T1 timer expired (retransmission timeout).
 *
 * Call from your timer callback.
 *
 * @param ctx Context.
 */
void atc_hdlc_t1_expired(atc_hdlc_context_t* ctx);

/**
 * @brief T2 timer expired (delayed ACK).
 *
 * Call from your timer callback. Sends RR automatically.
 *
 * @param ctx Context.
 */
void atc_hdlc_t2_expired(atc_hdlc_context_t* ctx);

/**
 * @brief Connect to peer (sends SABM).
 *
 * @param ctx       Context.
 * @param peer_addr Peer address.
 * @return ATC_HDLC_OK or error code.
 */
atc_hdlc_error_t atc_hdlc_link_setup(atc_hdlc_context_t* ctx, atc_hdlc_u8 peer_addr);

/**
 * @brief Disconnect (sends DISC).
 *
 * @param ctx Context.
 * @return ATC_HDLC_OK or error code.
 */
atc_hdlc_error_t atc_hdlc_disconnect(atc_hdlc_context_t* ctx);

/**
 * @brief Reset and reconnect.
 *
 * Use after FRMR_ERROR or for recovery.
 *
 * @param ctx Context.
 * @return ATC_HDLC_OK always.
 */
atc_hdlc_error_t atc_hdlc_link_reset(atc_hdlc_context_t* ctx);

/**
 * @brief Unconditional abort.
 *
 * Call when UART hardware detects a line break or framing error.
 * Sends two flag bytes (0x7E 0x7E) to force the peer into HUNT state,
 * stops all timers, resets connection state, and moves to DISCONNECTED
 * without firing an event.
 *
 * @param ctx Context.
 */
void atc_hdlc_abort(atc_hdlc_context_t* ctx);

/**
 * @brief Set local busy condition.
 *
 * @param ctx  Context.
 * @param busy true = busy (tell peer to wait), false = ready.
 * @return ATC_HDLC_OK or error code.
 */
atc_hdlc_error_t atc_hdlc_set_local_busy(atc_hdlc_context_t* ctx, bool busy);

/**
 * @brief Send reliable I-frame.
 *
 * @param ctx  Context.
 * @param data Payload.
 * @param len  Payload length.
 * @return ATC_HDLC_OK or error code.
 */
atc_hdlc_error_t atc_hdlc_transmit_i(atc_hdlc_context_t* ctx, const atc_hdlc_u8* data,
                                     atc_hdlc_u32 len);

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
atc_hdlc_error_t atc_hdlc_transmit_ui(atc_hdlc_context_t* ctx, atc_hdlc_u8 address,
                                      const atc_hdlc_u8* data, atc_hdlc_u32 len);

/**
 * @brief Send TEST frame and wait for echo.
 *
 * @param ctx     Context.
 * @param address Destination.
 * @param data    Test pattern (can be NULL).
 * @param len     Length.
 * @return ATC_HDLC_OK or error code.
 */
atc_hdlc_error_t atc_hdlc_transmit_test(atc_hdlc_context_t* ctx, atc_hdlc_u8 address,
                                        const atc_hdlc_u8* data, atc_hdlc_u32 len);

/**
 * @brief Feed multiple bytes.
 *
 * @warning Not ISR-safe
 *
 * @param ctx  Context.
 * @param data Byte array.
 * @param len  Count.
 */
void atc_hdlc_data_in(atc_hdlc_context_t* ctx, const atc_hdlc_u8* data, atc_hdlc_u32 len);

/**
 * @brief Start streaming UI frame TX.
 *
 * @param ctx     Context.
 * @param address Address.
 */
void atc_hdlc_transmit_ui_start(atc_hdlc_context_t* ctx, atc_hdlc_u8 address);

/**
 * @brief Add bytes to streaming UI frame.
 *
 * @param ctx  Context.
 * @param data Data.
 * @param len  Length.
 */
void atc_hdlc_transmit_ui_data(atc_hdlc_context_t* ctx, const atc_hdlc_u8* data, atc_hdlc_u32 len);

/**
 * @brief Finish streaming UI frame TX.
 *
 * Sends FCS and closing flag.
 *
 * @param ctx Context.
 */
void atc_hdlc_transmit_ui_end(atc_hdlc_context_t* ctx);

/**
 * @brief Get current state.
 *
 * @param ctx Context.
 * @return State value.
 */
atc_hdlc_state_t atc_hdlc_get_state(const atc_hdlc_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif /* ATC_HDLC_H */
