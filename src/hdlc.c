/**
 * @file hdlc.c
 * @brief Main Implementation of the HDLC Protocol Stack.
 *
 * Contains the logic for Frame Transmission (Buffered & Streaming),
 * Frame Reception (State Machine, Byte Stuffing removal), and
 * CRC Verification.
 */

#include "hdlc.h"
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
  if (!ctx)
    return;

  // Clear context
  memset(ctx, 0, sizeof(hdlc_context_t));

  // Bind callbacks
  ctx->tx_cb = tx_cb;
  ctx->rx_cb = rx_cb;
  ctx->user_data = user_data;

  // Initialize State
  ctx->rx_state = HDLC_RX_HUNT;
  ctx->state = HDLC_STATE_DISCONNECTED;
}

/*
 * --------------------------------------------------------------------------
 * TRANSMIT ENGINE
 * --------------------------------------------------------------------------
 */

/**
 * @brief Internal helper to send a single raw byte to hardware.
 * @param ctx  HDLC Context.
 * @param byte Raw byte to transmit.
 */
static void io_send_byte(hdlc_context_t *ctx, hdlc_u8 byte) {
  if (ctx->tx_cb) {
    ctx->tx_cb(ctx->user_data, byte);
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
static void io_send_escaped(hdlc_context_t *ctx, hdlc_u8 byte, hdlc_u16 *crc) {
  *crc = hdlc_crc_ccitt_update(*crc, byte);

  if (byte == HDLC_FLAG || byte == HDLC_ESCAPE) {
    io_send_byte(ctx, HDLC_ESCAPE);
    io_send_byte(ctx, byte ^ HDLC_XOR_MASK);
  } else {
    io_send_byte(ctx, byte);
  }
}

/**
 * @brief Send a complete HDLC Frame (Buffered).
 * @see hdlc.h
 */
void hdlc_send_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  if (!ctx || !frame)
    return;

  hdlc_u16 crc = 0xFFFF;

  // Start Flag
  io_send_byte(ctx, HDLC_FLAG);

  // Address (Directly from frame, usually broadcast or specific)
  io_send_escaped(ctx, frame->address, &crc);

  // Control Field
  io_send_escaped(ctx, frame->control.value, &crc);

  // Payload
  for (hdlc_u16 i = 0; i < frame->payload_len; i++) {
    io_send_escaped(ctx, frame->payload[i], &crc);
  }

  // FCS (CRC) - Transmit MSB first (Network Byte Order)
  // Note: FCS bytes themselves are NOT added to the CRC calculation,
  // but they ARE subject to byte stuffing.
  hdlc_u8 fcs_hi = (crc >> 8) & 0xFF;
  hdlc_u8 fcs_lo = crc & 0xFF;

  if (fcs_hi == HDLC_FLAG || fcs_hi == HDLC_ESCAPE) {
    io_send_byte(ctx, HDLC_ESCAPE);
    io_send_byte(ctx, fcs_hi ^ HDLC_XOR_MASK);
  } else {
    io_send_byte(ctx, fcs_hi);
  }

  if (fcs_lo == HDLC_FLAG || fcs_lo == HDLC_ESCAPE) {
    io_send_byte(ctx, HDLC_ESCAPE);
    io_send_byte(ctx, fcs_lo ^ HDLC_XOR_MASK);
  } else {
    io_send_byte(ctx, fcs_lo);
  }

  // End Flag
  io_send_byte(ctx, HDLC_FLAG);

  ctx->stats_tx_frames++;
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
  if (ctx->rx_cb) {
    ctx->rx_cb(ctx->user_data, &ctx->rx_frame);
  }

  ctx->stats_rx_frames++;
}

/**
 * @brief Input a received byte into the HDLC Parser.
 * @see hdlc.h
 */
void hdlc_input_byte(hdlc_context_t *ctx, hdlc_u8 byte) {
  if (!ctx)
    return;

  /* 1. Handle Frame Delimiters */
  if (byte == HDLC_FLAG) {
    if (ctx->rx_state != HDLC_RX_HUNT) {
      // Flag received while receiving data -> End of Frame
      if (ctx->rx_index > 0) {
        // Minimum size: Addr(1) + Ctrl(1) + FCS(2) = 4 bytes
        if (ctx->rx_index >= 4) {

          // --- CRC Verification Strategy ---
          // 1. Re-calculate CRC over the "Data" portion (Addr..Payload).
          // 2. Compare calculated CRC with the received FCS bytes (last 2
          // bytes).

          hdlc_u16 calced_crc = 0xFFFF;
          hdlc_u16 data_len = ctx->rx_index - 2; // Exclude FCS bytes

          for (hdlc_u16 i = 0; i < data_len; i++) {
            calced_crc =
                hdlc_crc_ccitt_update(calced_crc, ctx->rx_frame.payload[i]);
          }

          // Extract Received FCS (Assuming MSB first order on wire -> Buffered
          // as Hi, Lo)
          hdlc_u8 fcs_hi = ctx->rx_frame.payload[ctx->rx_index - 2];
          hdlc_u8 fcs_lo = ctx->rx_frame.payload[ctx->rx_index - 1];
          hdlc_u16 rx_fcs = (fcs_hi << 8) | fcs_lo;

          if (calced_crc == rx_fcs) {
            // Valid Frame!

            // Populate Structure:
            ctx->rx_frame.address = ctx->rx_frame.payload[0];
            ctx->rx_frame.control.value = ctx->rx_frame.payload[1];

            // Payload is everything after Header(2) and before FCS(2)
            ctx->rx_frame.payload_len = data_len - 2;

            // Shift payload to the beginning of the buffer (removing header)
            memmove(ctx->rx_frame.payload, &ctx->rx_frame.payload[2],
                    ctx->rx_frame.payload_len);

            process_complete_frame(ctx);
          } else {
            // CRC Error: Frame discarded silently (or logged)
            // printf("[LIB] CRC Fail: Calc=%04X Rx=%04X Len=%d\n", calced_crc,
            // rx_fcs, ctx->rx_index);
            ctx->stats_crc_errors++;
          }
        }
      }
    }

    // Reset for next frame
    ctx->rx_state = HDLC_RX_ADDRESS; // Expecting Address next
    ctx->rx_index = 0;
    ctx->rx_crc = 0xFFFF;
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

  if (ctx->rx_index >= HDLC_MAX_MTU + 4) {
    // Overflow protection: Drop invalid large frame and hunt for next flag
    ctx->rx_state = HDLC_RX_HUNT;
    return;
  }

  // Note: We don't update running RX CRC here anymore because we use
  // the "Recalculate over buffer" method on Frame End for robustness.

  // Store byte in buffer
  ctx->rx_frame.payload[ctx->rx_index++] = byte;
}

/*
 * --------------------------------------------------------------------------
 * STREAMING TRANSMIT ENGINE
 * --------------------------------------------------------------------------
 */

/**
 * @brief Start a Streaming Packet Transmission.
 * @see hdlc.h
 */
void hdlc_send_packet_start(hdlc_context_t *ctx) {
  if (!ctx)
    return;

  // Initialize CRC
  ctx->tx_crc = 0xFFFF;

  // Send Start Flag
  io_send_byte(ctx, HDLC_FLAG);
}

/**
 * @brief Send a Payload Byte in Streaming Mode.
 * @see hdlc.h
 */
void hdlc_send_packet_byte(hdlc_context_t *ctx, hdlc_u8 byte) {
  if (!ctx)
    return;

  // Update CRC and Send Escaped
  io_send_escaped(ctx, byte, &ctx->tx_crc);
}

/**
 * @brief Finalize Streaming Packet Transmission.
 * @see hdlc.h
 */
void hdlc_send_packet_end(hdlc_context_t *ctx) {
  if (!ctx)
    return;

  // Finalize CRC
  hdlc_u16 crc = ctx->tx_crc;

  hdlc_u8 fcs_hi = (crc >> 8) & 0xFF;
  hdlc_u8 fcs_lo = crc & 0xFF;

  // Send FCS High
  if (fcs_hi == HDLC_FLAG || fcs_hi == HDLC_ESCAPE) {
    io_send_byte(ctx, HDLC_ESCAPE);
    io_send_byte(ctx, fcs_hi ^ HDLC_XOR_MASK);
  } else {
    io_send_byte(ctx, fcs_hi);
  }

  // Send FCS Low
  if (fcs_lo == HDLC_FLAG || fcs_lo == HDLC_ESCAPE) {
    io_send_byte(ctx, HDLC_ESCAPE);
    io_send_byte(ctx, fcs_lo ^ HDLC_XOR_MASK);
  } else {
    io_send_byte(ctx, fcs_lo);
  }

  // End Flag
  io_send_byte(ctx, HDLC_FLAG);

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
