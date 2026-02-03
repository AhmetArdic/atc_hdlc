#ifndef HDLC_H
#define HDLC_H

#include "hdlc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the HDLC Context
 * 
 * @param ctx Pointer to allocated context structure
 * @param tx_cb Callback to send a byte to UART
 * @param rx_cb Callback when a valid frame is received
 * @param user_data User pointer passed to callbacks
 */
void hdlc_init(hdlc_context_t *ctx, 
               hdlc_tx_byte_cb_t tx_cb, 
               hdlc_on_frame_cb_t rx_cb,
               void *user_data);

/**
 * @brief Feed a received byte into the HDLC stack
 * 
 * Call this function from your UART RX interrupt or polling loop.
 * 
 * @param ctx Context pointer
 * @param data Received byte
 */
void hdlc_input_byte(hdlc_context_t *ctx, hdlc_u8 data);

/**
 * @brief Send a raw frame (Buffer Mode)
 * 
 * Constructs a frame with the given type and payload, 
 * calculates CRC, performs byte stuffing, and sends it via tx_cb.
 * 
 * @param ctx Context pointer
 * @param frame Pointer to frame structure to send
 */
void hdlc_send_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame);

/* 
 * Streaming API (Zero-Copy TX) - For Future Use or Phase 3 
 */
void hdlc_send_packet_start(hdlc_context_t *ctx);
void hdlc_send_packet_byte(hdlc_context_t *ctx, hdlc_u8 byte);
void hdlc_send_packet_end(hdlc_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif // HDLC_H
