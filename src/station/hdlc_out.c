/*
 * Copyright (C) 2026 Ahmet Talha ARDIC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../../inc/hdlc.h"
#include "../hdlc_private.h"
#include <string.h>

atc_hdlc_error_t atc_hdlc_transmit_ui(atc_hdlc_context_t *ctx,
                                        atc_hdlc_u8         address,
                                        const atc_hdlc_u8  *data,
                                        atc_hdlc_u32        len) {
  if (!ctx) return ATC_HDLC_ERR_INVALID_PARAM;
  if (ctx->config && len > ctx->config->max_frame_size)
    return ATC_HDLC_ERR_FRAME_TOO_LARGE;

  hdlc_transmit_start(ctx, address, HDLC_U_CTRL(HDLC_U_UI, 0));
  for (atc_hdlc_u32 i = 0; i < len; i++)
    hdlc_put_escaped_crc(ctx, data[i]);
  hdlc_finish_frame(ctx);

  return ATC_HDLC_OK;
}

atc_hdlc_error_t atc_hdlc_transmit_test(atc_hdlc_context_t *ctx,
                                          atc_hdlc_u8         address,
                                          const atc_hdlc_u8  *data,
                                          atc_hdlc_u32        len) {
  if (!ctx) return ATC_HDLC_ERR_INVALID_PARAM;
  if (ctx->test_pending) return ATC_HDLC_ERR_TEST_PENDING;
  if (ctx->config && len > ctx->config->max_frame_size)
    return ATC_HDLC_ERR_FRAME_TOO_LARGE;

  ctx->test_pattern     = data;
  ctx->test_pattern_len = (atc_hdlc_u16)len;
  ctx->test_pending     = true;

  hdlc_transmit_start(ctx, address, HDLC_U_CTRL(HDLC_U_TEST, 1));
  for (atc_hdlc_u32 i = 0; i < len; i++)
    hdlc_put_escaped_crc(ctx, data[i]);
  hdlc_finish_frame(ctx);

  hdlc_t1_start(ctx);
  return ATC_HDLC_OK;
}

atc_hdlc_error_t atc_hdlc_transmit_i(atc_hdlc_context_t *ctx,
                                       const atc_hdlc_u8  *data,
                                       atc_hdlc_u32        len) {
  if (!ctx) return ATC_HDLC_ERR_INVALID_PARAM;
  if (ctx->current_state != ATC_HDLC_STATE_CONNECTED)
    return ATC_HDLC_ERR_INVALID_STATE;
  if (ctx->remote_busy) return ATC_HDLC_ERR_REMOTE_BUSY;
  if (!ctx->tx_window)  return ATC_HDLC_ERR_NO_BUFFER;
  if (ctx->config && len > ctx->config->max_frame_size)
    return ATC_HDLC_ERR_FRAME_TOO_LARGE;

  atc_hdlc_u8 outstanding = (atc_hdlc_u8)((ctx->vs - ctx->va +
                             HDLC_SEQUENCE_MODULUS) % HDLC_SEQUENCE_MODULUS);
  if (outstanding >= ctx->window_size)
    return ATC_HDLC_ERR_WINDOW_FULL;

  atc_hdlc_u8 slot = ctx->next_tx_slot;
  ctx->tx_window->seq_to_slot[ctx->vs] = slot;
  ctx->next_tx_slot = (atc_hdlc_u8)((ctx->next_tx_slot + 1) % ctx->window_size);

  if (len > 0 && data) {
    if (len > ctx->tx_window->slot_capacity)
      return ATC_HDLC_ERR_FRAME_TOO_LARGE;
    memcpy(ctx->tx_window->slots + (slot * ctx->tx_window->slot_capacity), data, len);
  }
  ctx->tx_window->slot_lens[slot] = len;

  ATC_HDLC_LOG_DEBUG("tx: I-Frame V(S)=%u, Len=%lu", ctx->vs, (unsigned long)len);

  hdlc_transmit_start(ctx, ctx->peer_address, HDLC_I_CTRL(ctx->vs, ctx->vr, 0));
  for (atc_hdlc_u32 i = 0; i < len; i++)
    hdlc_put_escaped_crc(ctx, data[i]);
  hdlc_finish_frame(ctx);

  ctx->vs = (atc_hdlc_u8)((ctx->vs + 1) % HDLC_SEQUENCE_MODULUS);
  hdlc_t2_stop(ctx);

  if (outstanding == 0)
    hdlc_t1_start(ctx);

  return ATC_HDLC_OK;
}

void atc_hdlc_transmit_start_ui(atc_hdlc_context_t *ctx, atc_hdlc_u8 address) {
  if (!ctx) return;
  hdlc_transmit_start(ctx, address, HDLC_U_CTRL(HDLC_U_UI, 0));
}

void atc_hdlc_transmit_data(atc_hdlc_context_t *ctx,
                             const atc_hdlc_u8  *data,
                             atc_hdlc_u32        len) {
  if (!ctx || (!data && len > 0)) return;
  for (atc_hdlc_u32 i = 0; i < len; i++)
    hdlc_put_escaped_crc(ctx, data[i]);
}

void atc_hdlc_transmit_end(atc_hdlc_context_t *ctx) {
  if (!ctx) return;
  hdlc_finish_frame(ctx);
}
