/**
 * @file hdlc_input.c
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief HDLC Input Parsing (Receive State Machine).
 *
 * Contains the byte-by-byte receive parser that handles frame delimiters,
 * byte un-stuffing, CRC verification, and triggers frame processing.
 */

#include "../inc/hdlc.h"
#include "hdlc_crc.h"
#include "hdlc_private.h"

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
    if (ctx->input_state != HDLC_INPUT_STATE_HUNT) {
      // Minimum size: Addr(1) + Ctrl(1) + FCS(2) = 4 bytes
      if (ctx->input_index >= HDLC_MIN_FRAME_LEN) {
        // --- CRC Verification Strategy ---
        // 1. Re-calculate CRC over the "Data" portion (Addr..Payload).
        // 2. Compare calculated CRC with the received FCS bytes (last 2
        // bytes).

        hdlc_u16 calced_crc = HDLC_FCS_INIT_VALUE;
        hdlc_u32 data_len = ctx->input_index - HDLC_FCS_LEN; // Exclude FCS bytes

        for (hdlc_u32 i = 0; i < data_len; i++) {
          calced_crc =
              hdlc_crc_ccitt_update(calced_crc, ctx->input_buffer[i]);
        }

        // Extract Received FCS (Assuming MSB first order on wire -> Buffered as
        // Hi, Lo)
        hdlc_fcs_t *fcs = (hdlc_fcs_t *)&ctx->input_buffer[ctx->input_index - HDLC_FCS_LEN];
        hdlc_u16 rx_fcs = (fcs->fcs[0] << 8) | fcs->fcs[1];

        if (calced_crc == rx_fcs) {
          // Valid Frame!

          /* Construct the temporary frame descriptor (Zero-Copy) */
          ctx->input_frame_buffer.address = ctx->input_buffer[0];
          ctx->input_frame_buffer.control.value = ctx->input_buffer[1];
          
          // Information starts after Header (Addr+Ctrl), length is Total - (Header+FCS) = Total - 4
          // But only if total >= 4 (checked above)
          // Header Len = Address(1) + Control(1) = 2
          if (data_len > HDLC_ADDRESS_LEN + HDLC_CONTROL_LEN) {
             ctx->input_frame_buffer.information = &ctx->input_buffer[HDLC_ADDRESS_LEN + HDLC_CONTROL_LEN];
             ctx->input_frame_buffer.information_len = (hdlc_u16)(data_len - (HDLC_ADDRESS_LEN + HDLC_CONTROL_LEN));
          } else {
             ctx->input_frame_buffer.information = NULL;
             ctx->input_frame_buffer.information_len = 0;
          }

          process_complete_frame(ctx);
        } else {
          // CRC Error: Frame discarded silently (or logged)
          ctx->stats_crc_errors++;
        }
      }
    }

    // Reset for next frame
    ctx->input_state = HDLC_INPUT_STATE_ADDRESS; // Expecting Address next
    ctx->input_index = 0;
    ctx->input_crc = HDLC_FCS_INIT_VALUE;
    return;
  }

  /* 2. Handle State Machine */

  if (ctx->input_state == HDLC_INPUT_STATE_HUNT) {
    return; // Ignoring noise awaiting next Flag
  }

  if (byte == HDLC_ESCAPE) {
    ctx->input_state = HDLC_INPUT_STATE_ESCAPE;
    return;
  }

  if (ctx->input_state == HDLC_INPUT_STATE_ESCAPE) {
    byte ^= HDLC_XOR_MASK;        // Un-escape
    ctx->input_state = HDLC_INPUT_STATE_DATA; // Return to normal state
  }

  /* 3. Validation & Buffering */

  if (ctx->input_index >= ctx->input_buffer_len) {
    // Overflow protection: Drop invalid large frame and hunt for next flag
    ctx->input_state = HDLC_INPUT_STATE_HUNT;
    return;
  }

  // Note: We don't update running RX CRC here anymore because we use
  // the "Recalculate over buffer" method on Frame End for robustness.

  // Store byte in buffer
  ctx->input_buffer[ctx->input_index++] = byte;
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
