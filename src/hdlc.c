#include "hdlc.h"
#include "hdlc_crc.h"
#include "hdlc_private.h"
#include <stdio.h>
#include <string.h>

#define HDLC_FLAG 0x7E
#define HDLC_ESCAPE 0x7D
#define HDLC_XOR_MASK 0x20

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

static void io_send_byte(hdlc_context_t *ctx, hdlc_u8 byte) {
  if (ctx->tx_cb) {
    ctx->tx_cb(ctx->user_data, byte);
  }
}

static void io_send_escaped(hdlc_context_t *ctx, hdlc_u8 byte, hdlc_u16 *crc) {
  *crc = hdlc_crc_ccitt_update(*crc, byte);

  if (byte == HDLC_FLAG || byte == HDLC_ESCAPE) {
    io_send_byte(ctx, HDLC_ESCAPE);
    io_send_byte(ctx, byte ^ HDLC_XOR_MASK);
  } else {
    io_send_byte(ctx, byte);
  }
}

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

  // FCS (CRC) - Stuffed!
  // CRC is sent little-endian or big-endian?
  // ISO/IEC 13239 says FCS is transmitted least significant bit first.
  // However, usually widespread implementations (like PPP/AX.25) might differ.
  // Classic HDLC often implies sending MSB first for 8-bit bytes,
  // but the CRC itself is often inverted.
  // Let's stick to standard practice: Send Low Byte, then High Byte?
  // Actually, normally X.25 CRC is sent MSB first? No, LSB first.
  // Let's assume Network Byte Order (Big Endian) for consistency unless
  // specified otherwise. Wait, ISO 3309 says "The 1s complement of the FCS is
  // transmitted... highest order term first". "Highest order term" generally
  // maps to MSB. Let's send MSB first (Big Endian) which is typical for network
  // protocols.

  // We do NOT update the CRC with itself.

  hdlc_u8 fcs_hi = (crc >> 8) & 0xFF;
  hdlc_u8 fcs_lo = crc & 0xFF;

  // We just write these bytes. They are subject to stuffing too.
  // Wait, we DO NOT recalculate CRC on the CRC bytes ourselves.
  // But we DO need to stuff them if they match FLAG/ESCAPE.
  // We use a local helper that assumes no CRC update for these two:

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

static void handle_i_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  // TODO: Implement I-Frame Logic
  // - Check Sequence Number N(S) against V(R)
  // - Check Piggybacked Ack N(R) against V(S)
  // - Deliver payload to user
  // - Send Ack (RR) if needed
  (void)ctx;
  (void)frame;
}

static void handle_s_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  // TODO: Implement S-Frame Logic
  // - Handle RR, RNR, REJ
  (void)ctx;
  (void)frame;
}

static void handle_u_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  // TODO: Implement U-Frame Logic
  // - SABM: Send UA, Reset State
  // - DISC: Send UA, Go to Disconnected
  // - UA: Connection Established
  (void)ctx;
  (void)frame;
}

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

void hdlc_input_byte(hdlc_context_t *ctx, hdlc_u8 byte) {
  if (!ctx)
    return;

  /* 1. Handle Frame Delimiters */
  if (byte == HDLC_FLAG) {
    if (ctx->rx_state != HDLC_RX_HUNT) {
      // End of frame or Resync
      if (ctx->rx_index > 0) {
        // We have some data, check if it's a valid frame
        // Minimum size: Addr(1) + Ctrl(1) + FCS(2) = 4 bytes

        // Verify CRC by Re-calculation (Robust method)
        // Check minimum size first (Addr+Ctrl+FCS = 4 bytes)
        if (ctx->rx_index >= 4) {
          // 1. Calculate CRC over Data (Addr + Ctrl + Payload)
          hdlc_u16 calced_crc = 0xFFFF;
          hdlc_u16 data_len = ctx->rx_index - 2; // Exclude FCS

          for (hdlc_u16 i = 0; i < data_len; i++) {
            calced_crc =
                hdlc_crc_ccitt_update(calced_crc, ctx->rx_frame.payload[i]);
          }

          // 2. Extract Received FCS
          hdlc_u8 fcs_hi = ctx->rx_frame.payload[ctx->rx_index - 2];
          hdlc_u8 fcs_lo = ctx->rx_frame.payload[ctx->rx_index - 1];
          hdlc_u16 rx_fcs = (fcs_hi << 8) | fcs_lo;

          if (calced_crc == rx_fcs) {
            // Valid Frame!

            // Extract Structure:
            ctx->rx_frame.address = ctx->rx_frame.payload[0];
            ctx->rx_frame.control.value = ctx->rx_frame.payload[1];

            // Move payload: (Total - Header(2) - FCS(2))
            ctx->rx_frame.payload_len = data_len - 2; // -Addr, -Ctrl

            memmove(ctx->rx_frame.payload, &ctx->rx_frame.payload[2],
                    ctx->rx_frame.payload_len);

            process_complete_frame(ctx);
          } else {
            printf("[LIB] CRC Fail: Calc=%04X Rx=%04X Len=%d\n", calced_crc,
                   rx_fcs, ctx->rx_index);
            ctx->stats_crc_errors++;
          }
        }
      }
    }

    // Reset for next frame
    ctx->rx_state = HDLC_RX_ADDRESS; // Expecting Address next
    ctx->rx_index = 0;
    ctx->rx_crc = 0xFFFF;
    // Don't process FLAG as data
    return;
  }

  /* 2. Handle State Machine */

  if (ctx->rx_state == HDLC_RX_HUNT) {
    return; // Ignoring noise
  }

  if (byte == HDLC_ESCAPE) {
    ctx->rx_state = HDLC_RX_ESCAPE;
    return;
  }

  if (ctx->rx_state == HDLC_RX_ESCAPE) {
    byte ^= HDLC_XOR_MASK;
    ctx->rx_state = HDLC_RX_DATA; // Return to normal state (conceptually)
  }

  /* 3. Validation & Buffering */

  if (ctx->rx_index >= HDLC_MAX_MTU + 4) {
    // Overflow
    ctx->rx_state = HDLC_RX_HUNT;
    return;
  }

  // Add to CRC
  ctx->rx_crc = hdlc_crc_ccitt_update(ctx->rx_crc, byte);

  // Store
  ctx->rx_frame.payload[ctx->rx_index++] = byte;
}

/*
 * --------------------------------------------------------------------------
 * STREAMING TRANSMIT ENGINE
 * --------------------------------------------------------------------------
 */

void hdlc_send_packet_start(hdlc_context_t *ctx) {
  if (!ctx)
    return;

  // Initialize CRC
  ctx->tx_crc = 0xFFFF;

  // Send Start Flag
  io_send_byte(ctx, HDLC_FLAG);
}

void hdlc_send_packet_byte(hdlc_context_t *ctx, hdlc_u8 byte) {
  if (!ctx)
    return;

  // Update CRC and Send Escaped
  io_send_escaped(ctx, byte, &ctx->tx_crc);
}

void hdlc_send_packet_end(hdlc_context_t *ctx) {
  if (!ctx)
    return;

  // Finalize CRC
  // Same logic as send_frame: Send MSB then LSB, inverted?
  // In send_frame we used:
  // hdlc_u8 fcs_hi = (crc >> 8) & 0xFF;
  // hdlc_u8 fcs_lo = crc & 0xFF;
  // And we did NOT update CRC with itself.
  // Also we stuffed them.

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
