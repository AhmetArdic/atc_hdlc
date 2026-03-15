/*
 * Copyright (C) 2026 Ahmet Talha ARDIC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file hdlc.h
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief Public API for the HDLC library.
 *
 * Single include for all application-facing functionality:
 *   - Station lifecycle (init, tick, connect, disconnect, reset)
 *   - Data transfer (I-frame, UI, TEST)
 *   - Streaming frame transmit (start / data / end)
 *   - Stateless frame serialisation (pack / unpack)
 *   - Status queries
 *
 * Naming convention:
 *   - atc_hdlc_data_in*      : Feed received bytes into the RX parser.
 *   - atc_hdlc_transmit_*    : Build and send frames to the peer.
 *   - atc_hdlc_transmit_start/data/end : Streaming TX (zero-copy path).
 */

#ifndef ATC_HDLC_H
#define ATC_HDLC_H

#include "hdlc_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * --------------------------------------------------------------------------
 * STATION LIFECYCLE
 * --------------------------------------------------------------------------
 */

/**
 * @brief Initialise an HDLC station context.
 *
 * Clears the context, validates all parameters, stores the injected
 * references, and sets the state machine to DISCONNECTED.
 *
 * @param ctx       Station context to initialise (user-allocated).
 * @param config    Protocol configuration. Must remain valid for the lifetime
 *                  of @p ctx.
 * @param platform  Platform integration callbacks. Must remain valid for the
 *                  lifetime of @p ctx. @c on_send is mandatory; @c on_data and
 *                  @c on_event are optional (may be NULL).
 * @param tx_window TX retransmit window descriptor (may be NULL to disable
 *                  reliable I-frame transmission).
 * @param rx_buf    RX buffer descriptor (mandatory).
 *
 * @return @ref ATC_HDLC_OK on success.
 * @return @ref ATC_HDLC_ERR_INVALID_PARAM if any mandatory pointer is NULL or
 *         a parameter is out of range.
 * @return @ref ATC_HDLC_ERR_UNSUPPORTED_MODE if @c config->mode != ABM or
 *         @c config->use_extended is true.
 * @return @ref ATC_HDLC_ERR_INCONSISTENT_BUFFER if buffer geometry constraints
 *         are violated (see §4 of the architecture document).
 */
atc_hdlc_error_t atc_hdlc_init(atc_hdlc_context_t        *ctx,
                                 const atc_hdlc_config_t   *config,
                                 const atc_hdlc_platform_t *platform,
                                 atc_hdlc_tx_window_t      *tx_window,
                                 atc_hdlc_rx_buffer_t      *rx_buf);

/**
 * @brief Drive all internal timers by one tick.
 *
 * Must be called periodically at a fixed rate. The tick period (in ms) is
 * implied by the call frequency and must be consistent with @c config->t1_ms,
 * @c config->t2_ms, and @c config->t3_ms.
 *
 * Handles T1 retransmission, T2 delayed-ACK, T3 keep-alive, and SABM/DISC
 * retry logic for CONNECTING and DISCONNECTING states.
 *
 * @param ctx Initialised station context.
 */
void atc_hdlc_tick(atc_hdlc_context_t *ctx);

/*
 * --------------------------------------------------------------------------
 * CONNECTION MANAGEMENT
 * --------------------------------------------------------------------------
 */

/**
 * @brief Initiate a connection with the peer (sends SABM).
 *
 * Transitions from DISCONNECTED to CONNECTING and starts T1.
 *
 * @param ctx       Initialised station context.
 * @param peer_addr Remote station address.
 *
 * @return @ref ATC_HDLC_OK on success.
 * @return @ref ATC_HDLC_ERR_INVALID_STATE if not in DISCONNECTED state.
 * @return @ref ATC_HDLC_ERR_UNSUPPORTED_MODE if mode is not ABM.
 */
atc_hdlc_error_t atc_hdlc_link_setup(atc_hdlc_context_t *ctx,
                                       atc_hdlc_u8 peer_addr);

/**
 * @brief Terminate the logical connection (sends DISC).
 *
 * Transitions to DISCONNECTING and starts T1.
 *
 * @param ctx Initialised station context.
 *
 * @return @ref ATC_HDLC_OK on success.
 * @return @ref ATC_HDLC_ERR_INVALID_STATE if not in CONNECTED or FRMR_ERROR.
 */
atc_hdlc_error_t atc_hdlc_disconnect(atc_hdlc_context_t *ctx);

/**
 * @brief Reset the link and re-establish the connection.
 *
 * Performs an internal state reset followed by a new SABM. This is the
 * primary recovery path after @ref ATC_HDLC_STATE_FRMR_ERROR. Fires
 * @ref ATC_HDLC_EVENT_RESET. Valid in any state.
 *
 * @param ctx Initialised station context.
 *
 * @return @ref ATC_HDLC_OK always.
 */
atc_hdlc_error_t atc_hdlc_link_reset(atc_hdlc_context_t *ctx);

/**
 * @brief Return true if the station has an active logical connection.
 *
 * Returns true when @c current_state == @ref ATC_HDLC_STATE_CONNECTED.
 * Sub-conditions (remote_busy, local_busy, rej_exception) do not affect
 * this result — the connection is still active.
 *
 * @param ctx Initialised station context.
 * @return @c true if connected, @c false otherwise.
 */
atc_hdlc_bool atc_hdlc_is_connected(const atc_hdlc_context_t *ctx);

/**
 * @brief Assert or clear the local busy condition.
 *
 * When @p busy is true, the station sets the @c local_busy flag and responds
 * to incoming I-frames with RNR instead of RR. When @p busy is false, the
 * flag is cleared and an RR is sent to resume peer transmission. The station
 * remains in CONNECTED throughout.
 *
 * @param ctx  Initialised station context.
 * @param busy @c true to assert busy, @c false to clear it.
 *
 * @return @ref ATC_HDLC_OK on success.
 * @return @ref ATC_HDLC_ERR_INVALID_STATE if not in CONNECTED state.
 */
atc_hdlc_error_t atc_hdlc_set_local_busy(atc_hdlc_context_t *ctx,
                                           atc_hdlc_bool busy);

/*
 * --------------------------------------------------------------------------
 * DATA TRANSFER (TX PATH)
 * --------------------------------------------------------------------------
 */

/**
 * @brief Transmit a reliable Information (I) frame.
 *
 * Copies @p data into the TX window, assigns V(S), and sends the frame.
 * The frame is buffered for automatic retransmission until acknowledged.
 *
 * @param ctx  Initialised station context.
 * @param data Payload to transmit.
 * @param len  Payload length in octets.
 *
 * @return @ref ATC_HDLC_OK on success.
 * @return @ref ATC_HDLC_ERR_INVALID_STATE if not in CONNECTED state.
 * @return @ref ATC_HDLC_ERR_REMOTE_BUSY if peer sent RNR.
 * @return @ref ATC_HDLC_ERR_WINDOW_FULL if all TX slots are occupied.
 * @return @ref ATC_HDLC_ERR_FRAME_TOO_LARGE if @p len > @c max_frame_size.
 * @return @ref ATC_HDLC_ERR_NO_BUFFER if no TX window was provided at init.
 */
atc_hdlc_error_t atc_hdlc_transmit_i(atc_hdlc_context_t *ctx,
                                       const atc_hdlc_u8  *data,
                                       atc_hdlc_u32        len);

/**
 * @brief Transmit an Unnumbered Information (UI) frame.
 *
 * Connectionless, unacknowledged delivery. Connection state is irrelevant.
 *
 * @param ctx     Initialised station context.
 * @param address Destination address (@ref ATC_HDLC_BROADCAST_ADDRESS for broadcast).
 * @param data    Payload (may be NULL if @p len is 0).
 * @param len     Payload length in octets.
 *
 * @return @ref ATC_HDLC_OK on success.
 * @return @ref ATC_HDLC_ERR_FRAME_TOO_LARGE if @p len > @c max_frame_size.
 */
atc_hdlc_error_t atc_hdlc_transmit_ui(atc_hdlc_context_t *ctx,
                                        atc_hdlc_u8         address,
                                        const atc_hdlc_u8  *data,
                                        atc_hdlc_u32        len);

/**
 * @brief Transmit a TEST frame and wait for the echo response.
 *
 * Sends TEST(P=1), stores @p data as the expected echo pattern, and starts
 * T1. The result is reported via @ref ATC_HDLC_EVENT_TEST_RESULT. Only one
 * TEST may be outstanding at a time. Connection state is irrelevant.
 *
 * @param ctx     Initialised station context.
 * @param address Destination address.
 * @param data    Test pattern (may be NULL for an empty pattern).
 * @param len     Pattern length in octets.
 *
 * @return @ref ATC_HDLC_OK on success.
 * @return @ref ATC_HDLC_ERR_TEST_PENDING if a TEST is already in flight.
 * @return @ref ATC_HDLC_ERR_FRAME_TOO_LARGE if @p len > @c max_frame_size.
 */
atc_hdlc_error_t atc_hdlc_transmit_test(atc_hdlc_context_t *ctx,
                                          atc_hdlc_u8         address,
                                          const atc_hdlc_u8  *data,
                                          atc_hdlc_u32        len);

/*
 * --------------------------------------------------------------------------
 * RX PATH — Feed received bytes into the parser
 * --------------------------------------------------------------------------
 */

/**
 * @brief Feed a single received octet into the RX parser.
 *
 * Handles flag detection, byte un-stuffing, CRC verification, and frame
 * dispatch. When the closing flag (0x7E) is received, this function
 * performs CRC verification and all frame-processing synchronously.
 *
 * @warning Do not call directly from an ISR. Use an intermediate ring buffer
 *          and process from the main context. See §9 of the architecture doc.
 *
 * @param ctx  Initialised station context.
 * @param byte Raw octet from the physical medium.
 */
void atc_hdlc_data_in(atc_hdlc_context_t *ctx, atc_hdlc_u8 byte);

/**
 * @brief Feed multiple received octets into the RX parser.
 *
 * Convenience wrapper around @ref atc_hdlc_data_in.
 *
 * @param ctx  Initialised station context.
 * @param data Pointer to the octet array.
 * @param len  Number of octets.
 */
void atc_hdlc_data_in_bytes(atc_hdlc_context_t *ctx,
                              const atc_hdlc_u8  *data,
                              atc_hdlc_u32        len);

/*
 * --------------------------------------------------------------------------
 * STREAMING FRAME TRANSMIT (FRAME LAYER — STATELESS)
 * --------------------------------------------------------------------------
 * Use this sequence for memory-constrained devices where constructing a
 * complete atc_hdlc_frame_t in RAM is not feasible:
 *   atc_hdlc_transmit_start()
 *   atc_hdlc_transmit_data_byte() / atc_hdlc_transmit_data_bytes()  [zero or more]
 *   atc_hdlc_transmit_end()
 */

/**
 * @brief Begin streaming a frame — emit the opening flag, address, and control.
 *
 * Resets the FCS accumulator and sends the opening 0x7E flag followed by the
 * address and control octets (escaped and FCS-updated).
 *
 * @param ctx     Initialised station context.
 * @param address Address octet.
 * @param control Control octet.
 */
void atc_hdlc_transmit_start(atc_hdlc_context_t *ctx,
                               atc_hdlc_u8 address,
                               atc_hdlc_u8 control);

/** @brief Convenience starter for a UI frame (address + control pre-set). */
void atc_hdlc_transmit_start_ui(atc_hdlc_context_t *ctx,
                                  atc_hdlc_u8 address);

/** @brief Convenience starter for a TEST frame (address + control pre-set). */
void atc_hdlc_transmit_start_test(atc_hdlc_context_t *ctx,
                                    atc_hdlc_u8 address);

/**
 * @brief Append a single data octet to the frame being streamed.
 *
 * Updates the FCS and applies byte stuffing if necessary.
 *
 * @param ctx  Initialised station context.
 * @param byte Octet to append.
 */
void atc_hdlc_transmit_data_byte(atc_hdlc_context_t *ctx,
                                   atc_hdlc_u8 byte);

/**
 * @brief Append an array of data octets to the frame being streamed.
 *
 * @param ctx  Initialised station context.
 * @param data Pointer to the octet array.
 * @param len  Number of octets.
 */
void atc_hdlc_transmit_data_bytes(atc_hdlc_context_t *ctx,
                                    const atc_hdlc_u8  *data,
                                    atc_hdlc_u32        len);

/**
 * @brief Finalise the streamed frame — emit FCS and closing flag.
 *
 * Sends the two FCS octets (escaped) and the closing 0x7E flag. Increments
 * the TX frame counter.
 *
 * @param ctx Initialised station context.
 */
void atc_hdlc_transmit_end(atc_hdlc_context_t *ctx);

/*
 * --------------------------------------------------------------------------
 * STATELESS FRAME SERIALISATION (FRAME LAYER)
 * --------------------------------------------------------------------------
 */

/**
 * @brief Serialise an @ref atc_hdlc_frame_t into a byte buffer.
 *
 * Produces a complete wire-format HDLC frame (flags, escaped content, FCS).
 * Does not require a station context — usable standalone.
 *
 * @param frame       Frame to serialise.
 * @param buffer      Destination buffer.
 * @param buffer_len  Destination buffer size in octets.
 * @param encoded_len Output: number of octets written.
 *
 * @return @c true on success, @c false if the buffer is too small.
 */
atc_hdlc_bool atc_hdlc_frame_pack(const atc_hdlc_frame_t *frame,
                                    atc_hdlc_u8            *buffer,
                                    atc_hdlc_u32            buffer_len,
                                    atc_hdlc_u32           *encoded_len);

/**
 * @brief Deserialise a raw HDLC frame from a byte buffer.
 *
 * Performs de-stuffing, FCS verification, and field extraction. Does not
 * require a station context — usable standalone.
 *
 * @param buffer          Source buffer (wire-format frame, including 0x7E flags).
 * @param buffer_len      Source buffer length in octets.
 * @param frame           Output frame structure.
 * @param flat_buffer     Scratch buffer for de-stuffed content (Addr + Ctrl + Info).
 * @param flat_buffer_len Scratch buffer size in octets.
 *
 * @return @c true if the frame is valid (CRC match, minimum length met).
 * @return @c false otherwise.
 */
atc_hdlc_bool atc_hdlc_frame_unpack(const atc_hdlc_u8 *buffer,
                                      atc_hdlc_u32       buffer_len,
                                      atc_hdlc_frame_t  *frame,
                                      atc_hdlc_u8       *flat_buffer,
                                      atc_hdlc_u32       flat_buffer_len);

/*
 * --------------------------------------------------------------------------
 * CONTROL FIELD HELPERS (FRAME LAYER — STATELESS)
 * --------------------------------------------------------------------------
 */

/**
 * @brief Decode the S-frame sub-type from a control field byte.
 *
 * @param control Control field byte.
 * @return S-frame sub-type, or @ref ATC_HDLC_S_FRAME_TYPE_UNKNOWN.
 */
atc_hdlc_s_frame_sub_type_t atc_hdlc_get_s_frame_sub_type(atc_hdlc_u8 control);

/**
 * @brief Decode the U-frame sub-type from a control field byte.
 *
 * @param control Control field byte.
 * @return U-frame sub-type, or @ref ATC_HDLC_U_FRAME_TYPE_UNKNOWN.
 */
atc_hdlc_u_frame_sub_type_t atc_hdlc_get_u_frame_sub_type(atc_hdlc_u8 control);

/*
 * --------------------------------------------------------------------------
 * STATUS QUERIES
 * --------------------------------------------------------------------------
 */

/**
 * @brief Return the current state machine state.
 *
 * @param ctx Initialised station context.
 * @return Current @ref atc_hdlc_state_t value.
 */
atc_hdlc_state_t atc_hdlc_get_state(const atc_hdlc_context_t *ctx);

/**
 * @brief Return the number of free TX window slots.
 *
 * A value of 0 means the window is full and no more I-frames can be sent
 * until the peer acknowledges outstanding frames.
 *
 * @param ctx Initialised station context.
 * @return Free slot count (0 – window_size).
 */
atc_hdlc_u8 atc_hdlc_get_window_available(const atc_hdlc_context_t *ctx);

/**
 * @brief Return true if a received I-frame is pending acknowledgement.
 *
 * Indicates that T2 is running and a piggybacked or standalone RR has not
 * yet been sent for the most recently accepted in-sequence I-frame.
 *
 * @param ctx Initialised station context.
 * @return @c true if a pending ACK exists.
 */
atc_hdlc_bool atc_hdlc_has_pending_ack(const atc_hdlc_context_t *ctx);

/**
 * @brief Copy the current statistics snapshot.
 *
 * Thread-safe only when called from the same execution context as
 * @ref atc_hdlc_tick and @ref atc_hdlc_data_in.
 *
 * @param ctx Initialised station context.
 * @param out Destination for the statistics snapshot.
 */
void atc_hdlc_get_stats(const atc_hdlc_context_t *ctx, atc_hdlc_stats_t *out);

/**
 * @brief Return the time in ticks until the nearest timer expiry.
 *
 * Allows tickless/low-power schedulers to sleep for the exact duration
 * before calling @ref atc_hdlc_tick. Returns @c UINT32_MAX when no timers
 * are active.
 *
 * @param ctx Initialised station context.
 * @return Ticks until next expiry, or @c UINT32_MAX if none active.
 */
atc_hdlc_u32 atc_hdlc_get_next_timeout_ticks(const atc_hdlc_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ATC_HDLC_H */
