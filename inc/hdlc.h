/**
 * @file hdlc.h
 * @brief Public API for the HDLC Library.
 * 
 * Provides function prototypes for initializing the library, sending frames,
 * feeding received bytes into the parser, and handling streaming transmission.
 */

#ifndef HDLC_H
#define HDLC_H

#include "hdlc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * --------------------------------------------------------------------------
 * CORE API
 * --------------------------------------------------------------------------
 */

/**
 * @brief Initialize the HDLC Context.
 * 
 * Sets up the HDLC instance, clears internal state, resets statistics,
 * and binds the user-provided callbacks.
 * 
 * @param ctx       Pointer to the @ref hdlc_context_t structure to initialize.
 * @param tx_cb     Callback function for sending a byte to the hardware.
 * @param rx_cb     Callback function for receiving valid frames.
 * @param user_data Optional user pointer to pass to the callbacks.
 */
void hdlc_init(hdlc_context_t *ctx, hdlc_tx_byte_cb_t tx_cb, 
               hdlc_on_frame_cb_t rx_cb, void *user_data);

/**
 * @brief Input a received byte into the HDLC Parser.
 * 
 * This function should be called from the hardware UART ISR or polling loop
 * for every single byte received. It handles Flag detection, Byte Unstuffing,
 * CRC verification, and eventually calls the `rx_cb` when a complete valid
 * frame is assembled.
 * 
 * @param ctx  Pointer to the initialized HDLC context.
 * @param byte The raw byte received from the physical medium.
 */
void hdlc_input_byte(hdlc_context_t *ctx, hdlc_u8 byte);

/**
 * @brief Send a complete HDLC Frame (Buffered).
 * 
 * Constructs a raw HDLC stream from the provided `frame` structure,
 * automatically handling Flag generation, Byte Stuffing, and CRC calculation.
 * The output bytes are sent immediately via the `tx_cb`.
 * 
 * @param ctx   Pointer to the initialized HDLC context.
 * @param frame Pointer to the frame structure to transmit.
 */
void hdlc_send_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame);

/* 
 * --------------------------------------------------------------------------
 * CONTROL FIELD HELPERS
 * --------------------------------------------------------------------------
 */

/**
 * @brief Create an I-Frame Control Field.
 * @param ns Send Sequence Number N(S) (3 bits).
 * @param nr Receive Sequence Number N(R) (3 bits).
 * @param pf Poll/Final Bit.
 * @return Constructed control field.
 */
hdlc_control_t hdlc_create_i_ctrl(hdlc_u8 ns, hdlc_u8 nr, hdlc_u8 pf);

/**
 * @brief Create an S-Frame Control Field.
 * @param s_bits Supervisory function bits (2 bits: RR=00, RNR=01, REJ=10).
 * @param nr     Receive Sequence Number N(R) (3 bits).
 * @param pf     Poll/Final Bit.
 * @return Constructed control field.
 */
hdlc_control_t hdlc_create_s_ctrl(hdlc_u8 s_bits, hdlc_u8 nr, hdlc_u8 pf);

/**
 * @brief Create a U-Frame Control Field.
 * @param m_lo Modifier function bits (low 2 bits).
 * @param m_hi Modifier function bits (high 3 bits).
 * @param pf   Poll/Final Bit.
 * @return Constructed control field.
 */
hdlc_control_t hdlc_create_u_ctrl(hdlc_u8 m_lo, hdlc_u8 m_hi, hdlc_u8 pf);

/* 
 * --------------------------------------------------------------------------
 * ZERO-COPY STREAMING API
 * --------------------------------------------------------------------------
 */

/**
 * @brief Start a Streaming Packet Transmission.
 * 
 * Begins a new frame transmission by sending the Start Flag (`0x7E`)
 * and initializing the internal TX CRC engine.
 * 
 * Use this API sequence for memory-constrained devices where constructing
 * a full `hdlc_frame_t` in RAM is not feasible.
 * 
 * @param ctx Pointer to the initialized HDLC context.
 */
void hdlc_send_packet_start(hdlc_context_t *ctx);

/**
 * @brief Send a Payload Byte in Streaming Mode.
 * 
 * Sends a single byte of the frame content (Address, Control, or Data).
 * Automatically calculates CRC and performs Byte Stuffing (Escaping)
 * on the fly if the byte matches Flag/Escape characters.
 * 
 * @param ctx  Pointer to the initialized HDLC context.
 * @param byte The payload byte to send.
 */
void hdlc_send_packet_byte(hdlc_context_t *ctx, hdlc_u8 byte);

/**
 * @brief Finalize Streaming Packet Transmission.
 * 
 * Sends the calculated CRC (FCS) (handling any necessary stuffing),
 * and transmits the End Flag (`0x7E`).
 * 
 * @param ctx Pointer to the initialized HDLC context.
 */
void hdlc_send_packet_end(hdlc_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif // HDLC_H
