/**
 * @file hdlc.c
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief Main Implementation of the HDLC Protocol Stack.
 *
 * Contains the logic for Frame Transmission (Buffered & Streaming),
 * Frame Reception (State Machine, Byte Stuffing removal), and
 * CRC Verification.
 */

#include "../inc/hdlc.h"
#include "hdlc_crc.h"
#include "hdlc_private.h"
#include <stdio.h>
#include <string.h>

/** @brief HDLC Flag Sequence (0x7E) used to delimit frames. */
#define HDLC_FLAG 0x7E
/** @brief HDLC Escape Octet (0x7D) used for transparency. */
#define HDLC_ESCAPE 0x7D
/** @brief Bit-mask (0x20) XORed with octets to be escaped. */
#define HDLC_XOR_MASK 0x20

/**
 * @brief Initialize the HDLC Context.
 * @see hdlc.h
 */
void hdlc_init(hdlc_context_t *ctx, hdlc_tx_byte_cb_t tx_cb,
               hdlc_on_frame_cb_t rx_cb, void *user_data) {
  if (ctx == NULL) {
    return;
  }

  // Clear context
  memset(ctx, 0, sizeof(hdlc_context_t));

  // Bind callbacks
  ctx->tx_cb = tx_cb;
  ctx->rx_cb = rx_cb;
  ctx->user_data = user_data;

  // Initialize State
  ctx->rx_state = HDLC_RX_HUNT;
}

/*
 * --------------------------------------------------------------------------
 * TRANSMIT ENGINE
 * --------------------------------------------------------------------------
 */

/*
 * --------------------------------------------------------------------------
 * SHARED ENCODING LOGIC
 * --------------------------------------------------------------------------
 */

typedef struct {
  hdlc_context_t *ctx;  // For callback-based TX
  hdlc_u8 *buffer;      // For buffer-based TX
  hdlc_u32 buffer_len;  // Max buffer length
  hdlc_u32 current_len; // Current bytes written to buffer
  hdlc_bool success;    // Used for buffer overflow check
} hdlc_encode_ctx_t;

typedef void (*hdlc_put_byte_fn)(hdlc_encode_ctx_t *enc_ctx, hdlc_u8 byte,
                                 hdlc_bool flush);

/**
 * @brief Helper to write byte to hardware callback.
 */
static void put_byte_direct(hdlc_encode_ctx_t *enc_ctx, hdlc_u8 byte,
                            hdlc_bool flush) {
  if (enc_ctx->ctx && enc_ctx->ctx->tx_cb) {
    enc_ctx->ctx->tx_cb(byte, flush, enc_ctx->ctx->user_data);
  }
}

/**
 * @brief Helper to write byte to memory buffer.
 */
static void put_byte_buffer(hdlc_encode_ctx_t *enc_ctx, hdlc_u8 byte,
                            hdlc_bool flush) {
  (void)flush;
  if (enc_ctx->current_len < enc_ctx->buffer_len) {
    enc_ctx->buffer[enc_ctx->current_len++] = byte;
  } else {
    enc_ctx->success = false;
  }
}

static inline void encode_byte(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn,
                               hdlc_u8 byte, hdlc_bool flush) {
  put_fn(ctx, byte, flush);
}

static void encode_escaped(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn,
                           hdlc_u8 byte) {
  if (byte == HDLC_FLAG || byte == HDLC_ESCAPE) {
    encode_byte(ctx, put_fn, HDLC_ESCAPE, false);
    encode_byte(ctx, put_fn, byte ^ HDLC_XOR_MASK, false);
  } else {
    encode_byte(ctx, put_fn, byte, false);
  }
}

static void encode_escaped_crc_update(hdlc_encode_ctx_t *ctx,
                                      hdlc_put_byte_fn put_fn, hdlc_u8 byte,
                                      hdlc_u16 *crc) {
  *crc = hdlc_crc_ccitt_update(*crc, byte);
  encode_escaped(ctx, put_fn, byte);
}

static hdlc_bool hdlc_encode_core(const hdlc_frame_t *frame,
                                  hdlc_put_byte_fn put_fn,
                                  hdlc_encode_ctx_t *enc_ctx) {
  hdlc_u16 crc = HDLC_FCS_INIT_VALUE;

  // Start Flag
  encode_byte(enc_ctx, put_fn, HDLC_FLAG, false);
  if (!enc_ctx->success)
    return false;

  // Address
  encode_escaped_crc_update(enc_ctx, put_fn, frame->address, &crc);
  if (!enc_ctx->success)
    return false;

  // Control
  encode_escaped_crc_update(enc_ctx, put_fn, frame->control.value, &crc);
  if (!enc_ctx->success)
    return false;

  // Payload
  for (hdlc_u16 i = 0; i < frame->information_len; i++) {
    encode_escaped_crc_update(enc_ctx, put_fn, frame->information[i], &crc);
    if (!enc_ctx->success)
      return false;
  }

  // FCS
  hdlc_u8 fcs_hi = (crc >> 8) & 0xFF;
  hdlc_u8 fcs_lo = crc & 0xFF;

  encode_escaped(enc_ctx, put_fn, fcs_hi);
  if (!enc_ctx->success)
    return false;

  encode_escaped(enc_ctx, put_fn, fcs_lo);
  if (!enc_ctx->success)
    return false;

  // End Flag
  encode_byte(enc_ctx, put_fn, HDLC_FLAG, true);

  return enc_ctx->success;
}

/**
 * @brief Send a complete HDLC Frame (Buffered).
 * @see hdlc.h
 */
void hdlc_send_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  if (ctx == NULL || frame == NULL) {
    return;
  }

  hdlc_encode_ctx_t enc_ctx = {.ctx = ctx,
                               .buffer = NULL,
                               .buffer_len = 0,
                               .current_len = 0,
                               .success = true};

  hdlc_encode_core(frame, put_byte_direct, &enc_ctx);
  ctx->stats_tx_frames++;
}

/**
 * @brief Encode a frame into a memory buffer.
 * @see hdlc.h
 */
bool hdlc_encode_frame(const hdlc_frame_t *frame, hdlc_u8 *buffer,
                       hdlc_u32 buffer_len, hdlc_u32 *encoded_len) {
  if (frame == NULL || buffer == NULL || encoded_len == NULL) {
    return false;
  }

  hdlc_encode_ctx_t enc_ctx = {.ctx = NULL,
                               .buffer = buffer,
                               .buffer_len = buffer_len,
                               .current_len = 0,
                               .success = true};

  if (hdlc_encode_core(frame, put_byte_buffer, &enc_ctx)) {
    *encoded_len = enc_ctx.current_len;
    return true;
  }

  *encoded_len = 0;
  return false;
}

/*
 * --------------------------------------------------------------------------
 * RECEIVE ENGINE
 * --------------------------------------------------------------------------
 */

/*
 * --------------------------------------------------------------------------
 * PROTOCOL LOGIC DISPATCHER
 * --------------------------------------------------------------------------
 */

/**
 * @brief Internal handler for Information (I) Frames.
 * @param ctx   HDLC Context.
 * @param frame Received frame.
 */
static void handle_i_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  // TODO: Implement I-Frame Logic
  // - Check Sequence Number N(S) against V(R)
  // - Check Piggybacked Ack N(R) against V(S)
  // - Deliver payload to user
  // - Send Ack (RR) if needed
  (void)ctx;
  (void)frame;
}

/**
 * @brief Internal handler for Supervisory (S) Frames.
 * @param ctx   HDLC Context.
 * @param frame Received frame.
 */
static void handle_s_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  // TODO: Implement S-Frame Logic
  // - Handle RR, RNR, REJ
  (void)ctx;
  (void)frame;
}

/**
 * @brief Internal handler for Unnumbered (U) Frames.
 * @param ctx   HDLC Context.
 * @param frame Received frame.
 */
static void handle_u_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  // TODO: Implement U-Frame Logic
  // - SABM: Send UA, Reset State
  // - DISC: Send UA, Go to Disconnected
  // - UA: Connection Established
  (void)ctx;
  (void)frame;
}

/**
 * @brief Process a completely received and validated frame.
 *
 * Called when the State Machine detects a valid closing Flag and CRC checks
 * out. Routes the frame to the appropriate handler (I/S/U) and invokes the user
 * callback.
 *
 * @param ctx HDLC Context.
 */
static void process_complete_frame(hdlc_context_t *ctx) {
  // 1. Identify Frame Type based on Control Field
  hdlc_u8 ctrl = ctx->rx_frame.control.value;

  if ((ctrl & 0x01) == 0) {
    ctx->rx_frame.type = HDLC_FRAME_I;
    handle_i_frame(ctx, &ctx->rx_frame);
  } else if ((ctrl & 0x03) == 0x01) {
    ctx->rx_frame.type = HDLC_FRAME_S;
    handle_s_frame(ctx, &ctx->rx_frame);
  } else if ((ctrl & 0x03) == 0x03) {
    ctx->rx_frame.type = HDLC_FRAME_U;
    handle_u_frame(ctx, &ctx->rx_frame);
  } else {
    ctx->rx_frame.type = HDLC_FRAME_INVALID;
  }

  // 2. Notify User (Pass-through inspection)
  if (ctx->rx_cb != NULL) {
    ctx->rx_cb(&ctx->rx_frame, ctx->user_data);
  }

  ctx->stats_rx_frames++;
}

/**
 * @brief Input a received byte into the HDLC Parser.
 * @note **ISR UNSAFE**: Performs heavy validation (CRC) on frame end. Checks
 * for delimiters, handles byte-unstuffing, and buffers data.
 * @see hdlc.h for detailed ISR usage warnings.
 */
void hdlc_input_byte(hdlc_context_t *ctx, hdlc_u8 byte) {
  if (ctx == NULL) {
    return;
  }

  /* 1. Handle Frame Delimiters */
  if (byte == HDLC_FLAG) {
    if (ctx->rx_state != HDLC_RX_HUNT) {
      // Minimum size: Addr(1) + Ctrl(1) + FCS(2) = 4 bytes
      if (ctx->rx_index >= 4) {
        // --- CRC Verification Strategy ---
        // 1. Re-calculate CRC over the "Data" portion (Addr..Payload).
        // 2. Compare calculated CRC with the received FCS bytes (last 2
        // bytes).

        hdlc_u16 calced_crc = HDLC_FCS_INIT_VALUE;
        hdlc_u16 data_len = ctx->rx_index - 2; // Exclude FCS bytes

        for (hdlc_u16 i = 0; i < data_len; i++) {
          calced_crc =
              hdlc_crc_ccitt_update(calced_crc, ctx->rx_frame.value[i]);
        }

        // Extract Received FCS (Assuming MSB first order on wire -> Buffered as
        // Hi, Lo)
        hdlc_fcs_t *fcs = (hdlc_fcs_t *)&ctx->rx_frame.value[ctx->rx_index - 2];
        hdlc_u16 rx_fcs = (fcs->fcs[0] << 8) | fcs->fcs[1];

        if (calced_crc == rx_fcs) {
          // Valid Frame!

          // Information is everything after Header(2) and before FCS(2)
          ctx->rx_frame.information_len = data_len - 2;

          process_complete_frame(ctx);
        } else {
          // CRC Error: Frame discarded silently (or logged)
          // printf("[LIB] CRC Fail: Calc=%04X Rx=%04X Len=%d\n", calced_crc,
          // rx_fcs, ctx->rx_index);
          ctx->stats_crc_errors++;
        }
      }
    }

    // Reset for next frame
    ctx->rx_state = HDLC_RX_ADDRESS; // Expecting Address next
    ctx->rx_index = 0;
    ctx->rx_crc = HDLC_FCS_INIT_VALUE;
    return;
  }

  /* 2. Handle State Machine */

  if (ctx->rx_state == HDLC_RX_HUNT) {
    return; // Ignoring noise awaiting next Flag
  }

  if (byte == HDLC_ESCAPE) {
    ctx->rx_state = HDLC_RX_ESCAPE;
    return;
  }

  if (ctx->rx_state == HDLC_RX_ESCAPE) {
    byte ^= HDLC_XOR_MASK;        // Un-escape
    ctx->rx_state = HDLC_RX_DATA; // Return to normal state
  }

  /* 3. Validation & Buffering */

  if (ctx->rx_index >= HDLC_MAX_FRAME_LEN) {
    // Overflow protection: Drop invalid large frame and hunt for next flag
    ctx->rx_state = HDLC_RX_HUNT;
    return;
  }

  // Note: We don't update running RX CRC here anymore because we use
  // the "Recalculate over buffer" method on Frame End for robustness.

  // Store byte in buffer
  ctx->rx_frame.value[ctx->rx_index++] = byte;
}

/**
 * @brief Input multiple received bytes into the HDLC Parser.
 * @see hdlc.h
 */
void hdlc_input_bytes(hdlc_context_t *ctx, const hdlc_u8 *data, hdlc_u32 len) {
  if (ctx == NULL || data == NULL) {
    return;
  }

  for (hdlc_u32 i = 0; i < len; ++i) {
    hdlc_input_byte(ctx, data[i]);
  }
}

/*
 * --------------------------------------------------------------------------
 * STREAMING TRANSMIT ENGINE
 * --------------------------------------------------------------------------
 */

/**
 * @brief Internal helper to send a single raw byte to hardware.
 * @param ctx  HDLC Context.
 * @param byte Raw byte to transmit.
 */
static inline void io_send_byte(hdlc_context_t *ctx, hdlc_u8 byte,
                                hdlc_bool flush) {
  if (ctx->tx_cb != NULL) {
    ctx->tx_cb(byte, flush, ctx->user_data);
  }
}

/**
 * @brief Internal helper to send a data byte with automatic escaping.
 *
 * @param ctx  HDLC Context.
 * @param byte Data byte to send.
 */
static void io_send_escaped(hdlc_context_t *ctx, hdlc_u8 byte) {
  if (byte == HDLC_FLAG || byte == HDLC_ESCAPE) {
    io_send_byte(ctx, HDLC_ESCAPE, false);
    io_send_byte(ctx, byte ^ HDLC_XOR_MASK, false);
  } else {
    io_send_byte(ctx, byte, false);
  }
}

/**
 * @brief Internal helper to send a data byte with automatic escaping.
 *
 * Updates the running CRC and checks if the byte needs escaping
 * (i.e. if it looks like a FLAG or ESCAPE).
 *
 * @param ctx  HDLC Context.
 * @param byte Data byte to send.
 * @param crc  Pointer to the running CRC to update.
 */
static void io_send_escaped_crc_update(hdlc_context_t *ctx, hdlc_u8 byte,
                                       hdlc_u16 *crc) {
  *crc = hdlc_crc_ccitt_update(*crc, byte);

  io_send_escaped(ctx, byte);
}

/**
 * @brief Start a Streaming Packet Transmission.
 * @see hdlc.h
 */
void hdlc_send_packet_start(hdlc_context_t *ctx, hdlc_u8 address,
                            hdlc_u8 control) {
  if (ctx == NULL) {
    return;
  }

  // Initialize CRC
  ctx->tx_crc = HDLC_FCS_INIT_VALUE;

  // Send Start Flag
  io_send_byte(ctx, HDLC_FLAG, false);

  // Send Address
  // Update CRC and Send Escaped
  io_send_escaped_crc_update(ctx, address, &ctx->tx_crc);

  // Send Control
  // Update CRC and Send Escaped
  io_send_escaped_crc_update(ctx, control, &ctx->tx_crc);
}

/**
 * @brief Send a Information Byte in Streaming Mode.
 * @see hdlc.h
 */
void hdlc_send_packet_information_byte(hdlc_context_t *ctx,
                                       hdlc_u8 information_byte) {
  if (ctx == NULL) {
    return;
  }

  // Update CRC and Send Escaped
  io_send_escaped_crc_update(ctx, information_byte, &ctx->tx_crc);
}

/**
 * @brief Send a Information Bytes Array in Streaming Mode.
 * @see hdlc.h
 */
void hdlc_send_packet_information_bytes_array(
    hdlc_context_t *ctx, const hdlc_u8 *information_bytes_array, hdlc_u32 len) {
  if (ctx == NULL) {
    return;
  }

  for (hdlc_u32 i = 0; i < len; ++i) {
    // Update CRC and Send Escaped
    io_send_escaped_crc_update(ctx, information_bytes_array[i], &ctx->tx_crc);
  }
}

/**
 * @brief Finalize Streaming Packet Transmission.
 * @see hdlc.h
 */
void hdlc_send_packet_end(hdlc_context_t *ctx) {
  if (ctx == NULL) {
    return;
  }

  // Finalize CRC
  hdlc_u16 crc = ctx->tx_crc;

  hdlc_u8 fcs_hi = (crc >> 8) & 0xFF;
  hdlc_u8 fcs_lo = crc & 0xFF;

  // Send FCS High
  io_send_escaped(ctx, fcs_hi);

  // Send FCS Low
  io_send_escaped(ctx, fcs_lo);

  // End Flag
  io_send_byte(ctx, HDLC_FLAG, true);

  ctx->stats_tx_frames++;
}

/*
 * --------------------------------------------------------------------------
 * CONTROL FIELD HELPERS
 * --------------------------------------------------------------------------
 */

hdlc_control_t hdlc_create_i_ctrl(hdlc_u8 ns, hdlc_u8 nr, hdlc_u8 pf) {
  hdlc_control_t ctrl = {0};
  ctrl.i_frame.frame_type_0 = 0;
  ctrl.i_frame.ns = ns;
  ctrl.i_frame.pf = pf;
  ctrl.i_frame.nr = nr;
  return ctrl;
}

hdlc_control_t hdlc_create_s_ctrl(hdlc_u8 s_bits, hdlc_u8 nr, hdlc_u8 pf) {
  hdlc_control_t ctrl = {0};
  ctrl.s_frame.frame_type_0 = 1;
  ctrl.s_frame.frame_type_1 = 0;
  ctrl.s_frame.s = s_bits;
  ctrl.s_frame.pf = pf;
  ctrl.s_frame.nr = nr;
  return ctrl;
}

hdlc_control_t hdlc_create_u_ctrl(hdlc_u8 m_lo, hdlc_u8 m_hi, hdlc_u8 pf) {
  hdlc_control_t ctrl = {0};
  ctrl.u_frame.frame_type_0 = 1;
  ctrl.u_frame.frame_type_1 = 1;
  ctrl.u_frame.m_lo = m_lo;
  ctrl.u_frame.pf = pf;
  ctrl.u_frame.m_hi = m_hi;
  return ctrl;
}
