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

atc_hdlc_error_t atc_hdlc_transmit_ui(atc_hdlc_context_t *ctx,
                                         atc_hdlc_u8         address,
                                         const atc_hdlc_u8  *data,
                                         atc_hdlc_u32        len) {
    if (ctx == NULL) return ATC_HDLC_ERR_INVALID_PARAM;
    if (ctx->config && len > ctx->config->max_frame_size) {
        return ATC_HDLC_ERR_FRAME_TOO_LARGE;
    }

    atc_hdlc_frame_t frame = {
        .address = address,
        .control = HDLC_U_CTRL(HDLC_U_UI, 0),
        .information = data,
        .information_len = len
    };

    hdlc_transmit_frame(ctx, &frame);

    return ATC_HDLC_OK;
}

atc_hdlc_error_t atc_hdlc_transmit_test(atc_hdlc_context_t *ctx,
                                           atc_hdlc_u8         address,
                                           const atc_hdlc_u8  *data,
                                           atc_hdlc_u32        len) {
    if (ctx == NULL) return ATC_HDLC_ERR_INVALID_PARAM;
    if (ctx->test_pending) return ATC_HDLC_ERR_TEST_PENDING;
    if (ctx->config && len > ctx->config->max_frame_size) {
        return ATC_HDLC_ERR_FRAME_TOO_LARGE;
    }

    ctx->test_pattern     = data;
    ctx->test_pattern_len = (atc_hdlc_u16)len;
    ctx->test_pending     = true;
    ctx->stats.test_sent++;

    atc_hdlc_frame_t frame = {
        .address = address,
        .control = HDLC_U_CTRL(HDLC_U_TEST, 1),
        .information = data,
        .information_len = len
    };

    hdlc_transmit_frame(ctx, &frame);

    hdlc_t1_start(ctx);

    return ATC_HDLC_OK;
}

atc_hdlc_error_t atc_hdlc_transmit_i(atc_hdlc_context_t *ctx,
                                       const atc_hdlc_u8  *data,
                                       atc_hdlc_u32        len) {
    if (ctx == NULL) return ATC_HDLC_ERR_INVALID_PARAM;
    if (ctx->current_state != ATC_HDLC_STATE_CONNECTED) {
        return ATC_HDLC_ERR_INVALID_STATE;
    }
    if (ctx->remote_busy) return ATC_HDLC_ERR_REMOTE_BUSY;
    if (ctx->tx_window == NULL) return ATC_HDLC_ERR_NO_BUFFER;
    if (ctx->config && len > ctx->config->max_frame_size) {
        return ATC_HDLC_ERR_FRAME_TOO_LARGE;
    }

    atc_hdlc_u8 outstanding = (atc_hdlc_u8)((ctx->vs - ctx->va +
                               HDLC_SEQUENCE_MODULUS) % HDLC_SEQUENCE_MODULUS);
    if (outstanding >= ctx->window_size) {
        return ATC_HDLC_ERR_WINDOW_FULL;
    }

    atc_hdlc_u8 slot = ctx->next_tx_slot;
    ctx->tx_window->seq_to_slot[ctx->vs] = slot;
    ctx->next_tx_slot = (atc_hdlc_u8)((ctx->next_tx_slot + 1) % ctx->window_size);

    if (len > 0 && data != NULL) {
        if (len > ctx->tx_window->slot_capacity) {
            return ATC_HDLC_ERR_FRAME_TOO_LARGE;
        }
        memcpy(ctx->tx_window->slots + (slot * ctx->tx_window->slot_capacity),
               data, len);
    }
    ctx->tx_window->slot_lens[slot] = len;

    ATC_HDLC_LOG_DEBUG("tx: I-Frame V(S)=%u, Len=%lu", ctx->vs, (unsigned long)len);
    atc_hdlc_frame_t frame = {
        .address = ctx->peer_address,
        .control = HDLC_I_CTRL(ctx->vs, ctx->vr, 0),
        .information = data,
        .information_len = len
    };

    hdlc_transmit_frame(ctx, &frame);

    ctx->vs = (atc_hdlc_u8)((ctx->vs + 1) % HDLC_SEQUENCE_MODULUS);
    hdlc_t2_stop(ctx);

    if (outstanding == 0) {
        hdlc_t1_start(ctx);
    }

    HDLC_STAT_INC(ctx, tx_i_frames);
    HDLC_STAT_ADD(ctx, tx_bytes, len);

    return ATC_HDLC_OK;
}

void atc_hdlc_transmit_start_ui(atc_hdlc_context_t *ctx, atc_hdlc_u8 address) {
  if (ctx == NULL) {
    return;
  }
  atc_hdlc_u8 ctrl = HDLC_U_CTRL(HDLC_U_UI, 0);
  atc_hdlc_transmit_start(ctx, address, ctrl);
}

void atc_hdlc_transmit_data(atc_hdlc_context_t *ctx,
                                   const atc_hdlc_u8  *data,
                                   atc_hdlc_u32        len) {
  if (ctx == NULL || (data == NULL && len > 0)) {
    return;
  }

  hdlc_encode_ctx_t enc = {.ctx = ctx, .success = true};
  for (atc_hdlc_u32 i = 0; i < len; ++i) {
    hdlc_pack_escaped_crc(&enc, hdlc_write_byte, data[i], &ctx->tx_crc);
  }
}

void atc_hdlc_transmit_end(atc_hdlc_context_t *ctx) {
  if (ctx == NULL) {
    return;
  }

  hdlc_encode_ctx_t enc = {.ctx = ctx, .success = true};

  atc_hdlc_u16 crc = ctx->tx_crc;
  atc_hdlc_u8 fcs_hi = (crc >> 8) & 0xFF;
  atc_hdlc_u8 fcs_lo = crc & 0xFF;

  hdlc_pack_escaped(&enc, hdlc_write_byte, fcs_hi);
  hdlc_pack_escaped(&enc, hdlc_write_byte, fcs_lo);

  hdlc_write_byte(&enc, HDLC_FLAG, true);
}
