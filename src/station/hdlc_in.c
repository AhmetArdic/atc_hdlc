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
 * @file hdlc_in.c
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief HDLC RX path — byte-by-byte receive parser.
 *
 * Handles frame flag detection, byte un-stuffing, CRC verification,
 * and dispatches complete frames to hdlc_frame_handlers.c.
 */

#include "../../inc/hdlc.h"
#include "../frame/hdlc_crc.h"
#include "../hdlc_private.h"
#include <string.h>

/**
 * @brief Feed a single received octet into the HDLC RX parser.
 * @note **ISR UNSAFE**: Performs CRC validation on frame end.
 * @see hdlc.h for detailed ISR usage warnings.
 */
void atc_hdlc_data_in(atc_hdlc_context_t *ctx, atc_hdlc_u8 byte) {
  if (ctx == NULL) {
    return;
  }

  /* 1. Handle Frame Delimiters */
  if (byte == HDLC_FLAG) {
    if (ctx->rx_state != HDLC_RX_STATE_HUNT) {
      /* Minimum size: Addr(1) + Ctrl(1) + FCS(2) = 4 bytes */
      if (ctx->rx_index >= HDLC_MIN_FRAME_LEN) {
        /* --- CRC Verification --- */
        atc_hdlc_u16 calced_crc = ATC_HDLC_FCS_INIT_VALUE;
        atc_hdlc_u32 data_len = ctx->rx_index - HDLC_FCS_LEN;

        for (atc_hdlc_u32 i = 0; i < data_len; i++) {
          calced_crc = atc_hdlc_crc_ccitt_update(calced_crc, ctx->rx_buf->buffer[i]);
        }

        /* Extract received FCS (MSB first on wire → buffered as Hi, Lo) */
        atc_hdlc_u16 rx_fcs = ((atc_hdlc_u16)ctx->rx_buf->buffer[data_len] << 8) |
                                ctx->rx_buf->buffer[data_len + 1];

        if (calced_crc == rx_fcs) {
          /* Valid Frame — build descriptor and dispatch */
          ATC_HDLC_LOG_DEBUG("rx: Valid frame (Addr: 0x%02X, Ctrl: 0x%02X, Len: %lu)",
                         ctx->rx_buf->buffer[0], ctx->rx_buf->buffer[1], data_len);

          ctx->rx_frame.address = ctx->rx_buf->buffer[0];
          ctx->rx_frame.control = ctx->rx_buf->buffer[1];

          if (data_len > HDLC_ADDRESS_LEN + HDLC_CONTROL_LEN) {
             ctx->rx_frame.information = &ctx->rx_buf->buffer[HDLC_ADDRESS_LEN + HDLC_CONTROL_LEN];
             ctx->rx_frame.information_len = (atc_hdlc_u16)(data_len - (HDLC_ADDRESS_LEN + HDLC_CONTROL_LEN));
          } else {
             ctx->rx_frame.information = NULL;
             ctx->rx_frame.information_len = 0;
          }

          hdlc_process_complete_frame(ctx);
        } else {
          ATC_HDLC_LOG_WARN("rx: CRC Error! Calc: 0x%04X, RX: 0x%04X", calced_crc, rx_fcs);
          ctx->stats.fcs_errors++;
        }
      }
    }

    /* Reset for next frame (ADDRESS state allows back-to-back frames). */
    ctx->rx_state = HDLC_RX_STATE_ADDRESS;
    ctx->rx_index = 0;
    return;
  }

  /* 2. Handle State Machine */

  if (ctx->rx_state == HDLC_RX_STATE_HUNT) {
    return; /* Ignoring noise, awaiting next flag */
  }

  if (byte == HDLC_ESCAPE) {
    ctx->rx_state = HDLC_RX_STATE_ESCAPE;
    return;
  }

  if (ctx->rx_state == HDLC_RX_STATE_ESCAPE) {
    byte ^= HDLC_XOR_MASK;
    ctx->rx_state = HDLC_RX_STATE_DATA;
  }

  /* 3. Validation & Buffering */

  if (ctx->rx_index >= ctx->rx_buf->capacity) {
    ATC_HDLC_LOG_WARN("rx: Buffer overflow! Max %lu bytes. Discarding.",
                      (unsigned long)ctx->rx_buf->capacity);
    ctx->rx_state = HDLC_RX_STATE_HUNT;
    return;
  }

  /* Store byte */
  ctx->rx_buf->buffer[ctx->rx_index++] = byte;

  /* Early abort if Address byte is invalid */
  if (ctx->rx_index == 1) {
    if (byte != ctx->my_address && byte != ctx->peer_address &&
        byte != ATC_HDLC_BROADCAST_ADDRESS) {
      ATC_HDLC_LOG_WARN("rx: Invalid Address 0x%02X. Frame discarded, returning to HUNT.", byte);
      ctx->rx_state = HDLC_RX_STATE_HUNT;
      ctx->rx_index = 0;
      return;
    }
    ctx->rx_state = HDLC_RX_STATE_DATA;
  }
}

/**
 * @brief Feed multiple received octets into the HDLC RX parser.
 * @see hdlc.h
 */
void atc_hdlc_data_in_bytes(atc_hdlc_context_t *ctx, const atc_hdlc_u8 *data, atc_hdlc_u32 len) {
  if (ctx == NULL || data == NULL) {
    return;
  }

  for (atc_hdlc_u32 i = 0; i < len; ++i) {
    atc_hdlc_data_in(ctx, data[i]);
  }
}
