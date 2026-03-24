/*
 * Copyright (C) 2026 Ahmet Talha ARDIC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../../inc/hdlc.h"
#include "../frame/hdlc_crc.h"
#include "../hdlc_private.h"
#include <string.h>

void hdlc_data_in(atc_hdlc_context_t *ctx, atc_hdlc_u8 byte) {
  if (byte == HDLC_FLAG) {
    if (ctx->rx_state != HDLC_RX_STATE_HUNT) {
      if (ctx->rx_index >= HDLC_MIN_FRAME_LEN) {
        atc_hdlc_u16 calced_crc = ATC_HDLC_FCS_INIT_VALUE;
        atc_hdlc_u32 data_len = ctx->rx_index - HDLC_FCS_LEN;

        for (atc_hdlc_u32 i = 0; i < data_len; i++) {
          calced_crc = atc_hdlc_crc_ccitt_update(calced_crc, ctx->rx_buf->buffer[i]);
        }

        atc_hdlc_u16 rx_fcs = ((atc_hdlc_u16)ctx->rx_buf->buffer[data_len] << 8) |
                                ctx->rx_buf->buffer[data_len + 1];

        if (calced_crc == rx_fcs) {
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
          HDLC_STAT_INC(ctx, fcs_errors);
        }
      }
    }

    ctx->rx_state = HDLC_RX_STATE_ADDRESS;
    ctx->rx_index = 0;
    return;
  }

  if (ctx->rx_state == HDLC_RX_STATE_HUNT) {
    return;
  }

  if (byte == HDLC_ESCAPE) {
    ctx->rx_state = HDLC_RX_STATE_ESCAPE;
    return;
  }

  if (ctx->rx_state == HDLC_RX_STATE_ESCAPE) {
    byte ^= HDLC_XOR_MASK;
    ctx->rx_state = HDLC_RX_STATE_DATA;
  }

  if (ctx->rx_index >= ctx->rx_buf->capacity) {
    ATC_HDLC_LOG_WARN("rx: Buffer overflow! Max %lu bytes. Discarding.",
                      (unsigned long)ctx->rx_buf->capacity);
    ctx->rx_state = HDLC_RX_STATE_HUNT;
    return;
  }

  ctx->rx_buf->buffer[ctx->rx_index++] = byte;

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

void atc_hdlc_data_in(atc_hdlc_context_t *ctx, const atc_hdlc_u8 *data, atc_hdlc_u32 len) {
  if (ctx == NULL || data == NULL) {
    return;
  }

  for (atc_hdlc_u32 i = 0; i < len; ++i) {
    hdlc_data_in(ctx, data[i]);
  }
}
