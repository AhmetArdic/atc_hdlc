/**
 * @file hdlc.h
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief Public API for the HDLC Library.
 * 
 * Provides function prototypes for initializing the library, packing/unpacking frames,
 * feeding received bytes into the parser, and handling packet transmission.
 */

#ifndef HDLC_H
#define HDLC_H

#include "hdlc_types.h"
#include <stddef.h>

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
 * @param ctx                Pointer to the @ref hdlc_context_t structure to initialize.
 * @param buffer             Pointer to the user-supplied Input buffer.
 * @param buffer_len         Length of the user-supplied Input buffer.
 * @param output_cb          Callback function for sending a byte to the hardware.
 * @param on_frame_cb        Callback function for receiving valid frames.
 * @param on_state_change_cb Callback function for connection state changes (Optional, can be NULL).
 * @param user_data          Optional user pointer to pass to the callbacks.
 */
void hdlc_init(hdlc_context_t *ctx, hdlc_u8 *buffer, hdlc_u32 buffer_len,
                      hdlc_output_byte_cb_t output_cb,
                      hdlc_on_frame_cb_t on_frame_cb,
                      hdlc_on_state_change_cb_t on_state_change_cb,
                      void *user_data);

/**
 * @brief Configure Station Addresses.
 *
 * Sets the logical address for this station and the expected peer address.
 *
 * @param ctx       Pointer to the initialized HDLC context.
 * @param my_addr   Address of this station (used for RX filtering).
 * @param peer_addr Address of the remote station (used for TX frames).
 */
void hdlc_configure_addresses(hdlc_context_t *ctx, hdlc_u8 my_addr, hdlc_u8 peer_addr);

/**
 * @brief Initiate a Logical Connection (SABM).
 *
 * Sends a Set Asynchronous Balanced Mode (SABM) frame to the peer
 * and transitions to the HDLC_PROTOCOL_STATE_CONNECTING state.
 *
 * @param ctx Pointer to the initialized HDLC context.
 * @return true if command sent successfully (does not mean connected yet).
 */
bool hdlc_connect(hdlc_context_t *ctx);

/**
 * @brief Terminate a Logical Connection (DISC).
 *
 * Sends a Disconnect (DISC) frame to the peer and transitions
 * to the HDLC_PROTOCOL_STATE_DISCONNECTING state.
 *
 * @param ctx Pointer to the initialized HDLC context.
 * @return true if command sent successfully.
 */
bool hdlc_disconnect(hdlc_context_t *ctx);

/**
 * @brief Check if Connected.
 *
 * @param ctx Pointer to the initialized HDLC context.
 * @return true if state is HDLC_PROTOCOL_STATE_CONNECTED, false otherwise.
 */
bool hdlc_is_connected(hdlc_context_t *ctx);

/**
 * @brief Send an Unnumbered Information (UI) frame.
 * 
 * Sends a UI frame with the provided data payload. UI frames are
 * unacknowledged and unsequenced.
 * 
 * @param ctx  Pointer to the initialized HDLC context.
 * @param data Pointer to the data payload.
 * @param len  Length of the data payload.
 * @return true if the frame was sent successfully, false otherwise.
 */
bool hdlc_send_ui(hdlc_context_t *ctx, const hdlc_u8 *data, hdlc_u32 len);

/**
 * @brief Input a received byte into the HDLC Parser.
 *
 * Checks for delimiters, handles byte-unstuffing, and buffers data.
 *
 * @warning **CRITICAL TIMING NOTE**: When the closing flag (0x7E) is received,
 * this function performs **O(N)** operations synchronously, including:
 * 1. CRC verification over the full frame.
 * 2. Execution of the user `on_frame_cb`.
 *
 * **DO NOT CALL FROM ISR** directly unless your baud rate is low, your frames
 * are short, and you understand the timing implications. For high-performance
 * applications, push bytes to a Ring Buffer from the ISR and call this function
 * from a lower-priority thread or main loop.
 *
 * @param ctx  Pointer to the initialized HDLC context.
 * @param byte The raw byte received from the physical medium.
 */
void hdlc_input_byte(hdlc_context_t *ctx, hdlc_u8 byte);

/**
 * @brief Input multiple received bytes into the HDLC Parser.
 *
 * Convenience wrapper that feeds an array of bytes into the parser
 * by calling @ref hdlc_input_byte for each element.
 *
 * @warning Same ISR safety considerations as @ref hdlc_input_byte apply.
 *
 * @param ctx  Pointer to the initialized HDLC context.
 * @param data Pointer to the byte array to process.
 * @param len  Number of bytes in the array.
 */
void hdlc_input_bytes(hdlc_context_t *ctx, const hdlc_u8 *data, hdlc_u32 len);

/**
 * @brief Output a complete HDLC Frame.
 * 
 * Constructs a raw HDLC stream from the provided `frame` structure,
 * automatically handling Flag generation, Byte Stuffing, and CRC calculation.
 * The output bytes are sent immediately via the `output_cb`.
 * 
 * @param ctx   Pointer to the initialized HDLC context.
 * @param frame Pointer to the frame structure to transmit.
 */
void hdlc_output_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame);


/**
 * @brief Pack (Serialize) a frame into a memory buffer.
 * 
 * Serializes the frame into the provided buffer. This is useful for users who
 * want to control transmission scheduling or use a different transport layer.
 * 
 * @param frame      Pointer to the frame to pack.
 * @param buffer     Destination buffer.
 * @param buffer_len Size of the destination buffer.
 * @param encoded_len Output pointer for the actual packed length.
 * @return true if successful, false if buffer is too small.
 */
bool hdlc_frame_pack(const hdlc_frame_t *frame, hdlc_u8 *buffer, hdlc_u32 buffer_len, hdlc_u32 *encoded_len);

/**
 * @brief Unpack (Deserialize) a raw HDLC frame from a buffer.
 *
 * Parses a raw byte buffer containing a full HDLC frame (with Flags and FCS),
 * validates the CRC, un-escapes the content, and populates the frame structure.
 *
 * @param buffer          Source buffer containing the raw HDLC frame (including 0x7E flags).
 * @param buffer_len      Length of the source buffer.
 * @param frame           Pointer to the frame structure to populate.
 * @param flat_buffer     Destination buffer to store the decoded (linearized) data (Addr, Ctrl, Info).
 * @param flat_buffer_len Length of the destination buffer.
 * @return true if frame is valid (CRC match, correct formatting), false otherwise.
 */
bool hdlc_frame_unpack(const hdlc_u8 *buffer, hdlc_u32 buffer_len, hdlc_frame_t *frame, hdlc_u8 *flat_buffer, hdlc_u32 flat_buffer_len);

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
 * @brief Start a Packet Output.
 *
 * Begins a new frame transmission by sending the Start Flag (`0x7E`)
 * and initializing the internal Output CRC engine.
 *
 * Use this API sequence for memory-constrained devices where constructing
 * a full `hdlc_frame_t` in RAM is not feasible.
 *
 * @param ctx Pointer to the initialized HDLC context.
 * @param address The address byte to send.
 * @param control The control byte to send.
 */
void hdlc_output_packet_start(hdlc_context_t *ctx, hdlc_u8 address, hdlc_u8 control);

/**
 * @brief Output a Information Byte.
 *
 * Sends a single byte of the frame content (Address, Control, or Data).
 * Automatically calculates CRC and performs Byte Stuffing (Escaping)
 * on the fly if the byte matches Flag/Escape characters.
 *
 * @param ctx Pointer to the initialized HDLC context.
 * @param information_byte The payload byte to send.
 */
void hdlc_output_packet_information_byte(hdlc_context_t *ctx, hdlc_u8 information_byte);

/**
 * @brief Output a Information Bytes Array.
 *
 * Sends a bytes array of the frame content (Address, Control, or Data).
 * Automatically calculates CRC and performs Byte Stuffing (Escaping)
 * on the fly if the byte matches Flag/Escape characters.
 *
 * @param ctx Pointer to the initialized HDLC context.
 * @param information_bytes The payload bytes array to send.
 * @param len The length of payload bytes array to send.
 */
void hdlc_output_packet_information_bytes(hdlc_context_t *ctx, const hdlc_u8* information_bytes, hdlc_u32 len);

/**
 * @brief Finalize Packet Output.
 *
 * Completes the current frame transmission by sending the computed
 * CRC-16 (FCS) and the End Flag (`0x7E`). Increments the TX frame counter.
 *
 * @param ctx Pointer to the initialized HDLC context.
 */
void hdlc_output_packet_end(hdlc_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif // HDLC_H
