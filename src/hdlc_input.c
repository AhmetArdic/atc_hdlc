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
#include <string.h>
/**
 * @brief Input a received byte into the HDLC Parser.
 * @note **ISR UNSAFE**: Performs heavy validation (CRC) on frame end. Checks
 * for delimiters, handles byte-unstuffing, and buffers data.
 * @see hdlc.h for detailed ISR usage warnings.
 */
void atc_hdlc_input_byte(atc_hdlc_context_t *ctx, atc_hdlc_u8 byte) {
  if (ctx == NULL) {
    return;
  }

  /* 1. Handle Frame Delimiters */
  if (byte == HDLC_FLAG) {
    if (ctx->input_state != HDLC_INPUT_STATE_HUNT) {
      // Minimum size: Addr(1) + Ctrl(1) + FCS(2) = 4 bytes
      if (ctx->input_index >= ATC_HDLC_MIN_FRAME_LEN) {
        // --- CRC Verification ---
        // 1. Calculate CRC over the "Data" portion (Addr..Payload).
        // 2. Compare calculated CRC with the received FCS bytes (last 2
        // bytes).

        atc_hdlc_u16 calced_crc = ATC_HDLC_FCS_INIT_VALUE;
        atc_hdlc_u32 data_len = ctx->input_index - ATC_HDLC_FCS_LEN; // Exclude FCS bytes

        for (atc_hdlc_u32 i = 0; i < data_len; i++) {
          calced_crc =
              atc_hdlc_crc_ccitt_update(calced_crc, ctx->input_buffer[i]);
        }

        // Extract Received FCS (Assuming MSB first order on wire -> Buffered as
        // Hi, Lo)
        atc_hdlc_u16 rx_fcs = ((atc_hdlc_u16)ctx->input_buffer[data_len] << 8) | ctx->input_buffer[data_len + 1];

        if (calced_crc == rx_fcs) {
          // Valid Frame!
          ATC_HDLC_LOG_DEBUG("rx: Valid frame (Addr: 0x%02X, Ctrl: 0x%02X, Len: %lu)",
                         ctx->input_buffer[0], ctx->input_buffer[1], data_len);

          /* Construct the temporary frame descriptor (Zero-Copy) */
          ctx->input_frame_buffer.address = ctx->input_buffer[0];
          ctx->input_frame_buffer.control.value = ctx->input_buffer[1];
          
          // Information starts after Header (Addr+Ctrl), length is Total - (Header+FCS) = Total - 4
          // But only if total >= 4 (checked above)
          // Header Len = Address(1) + Control(1) = 2
          if (data_len > ATC_HDLC_ADDRESS_LEN + ATC_HDLC_CONTROL_LEN) {
             ctx->input_frame_buffer.information = &ctx->input_buffer[ATC_HDLC_ADDRESS_LEN + ATC_HDLC_CONTROL_LEN];
             ctx->input_frame_buffer.information_len = (atc_hdlc_u16)(data_len - (ATC_HDLC_ADDRESS_LEN + ATC_HDLC_CONTROL_LEN));
          } else {
             ctx->input_frame_buffer.information = NULL;
             ctx->input_frame_buffer.information_len = 0;
          }

          process_complete_frame(ctx);
        } else {
          // CRC Error: Frame discarded silently (or logged)
          ATC_HDLC_LOG_WARN("rx: CRC Error! Calc: 0x%04X, RX: 0x%04X", calced_crc, rx_fcs);
          ctx->stats_crc_errors++;
        }
      }
    }

    // Reset for next frame. We set it to ADDRESS expecting the next frame right away (Back-to-Back).
    // If the next byte is noise instead of a valid address, the Early Abort logic below will
    // immediately catch it and drop the state to HUNT.
    ctx->input_state = HDLC_INPUT_STATE_ADDRESS;
    ctx->input_index = 0;
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
    ATC_HDLC_LOG_WARN("rx: Buffer overflow! Max %lu bytes. Discarding.", (unsigned long)ctx->input_buffer_len);
    ctx->input_state = HDLC_INPUT_STATE_HUNT;
    return;
  }



  // Store byte in buffer
  ctx->input_buffer[ctx->input_index++] = byte;

  // Early abort if Address byte is invalid
  if (ctx->input_index == 1) {
    if (byte != ctx->my_address && byte != ctx->peer_address && byte != ATC_HDLC_BROADCAST_ADDRESS) {
      ATC_HDLC_LOG_WARN("rx: Invalid Address 0x%02X. Frame discarded, returning to HUNT.", byte);
      ctx->input_state = HDLC_INPUT_STATE_HUNT;
      ctx->input_index = 0;
      return;
    }
    ctx->input_state = HDLC_INPUT_STATE_DATA; // Address validated, transition to DATA state
  }
}

/**
 * @brief Input multiple received bytes into the HDLC Parser.
 * @see hdlc.h
 */
void atc_hdlc_input_bytes(atc_hdlc_context_t *ctx, const atc_hdlc_u8 *data, atc_hdlc_u32 len) {
  if (ctx == NULL || data == NULL) {
    return;
  }

  for (atc_hdlc_u32 i = 0; i < len; ++i) {
    atc_hdlc_input_byte(ctx, data[i]);
  }
}
