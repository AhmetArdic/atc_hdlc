/*
 * Copyright (C) 2026 Ahmet Talha ARDIC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../inc/hdlc.h"
#include "hdlc_private.h"
#include <string.h>

atc_hdlc_error_t atc_hdlc_transmit_ui(atc_hdlc_context_t *ctx,
                                        atc_hdlc_u8         address,
                                        const atc_hdlc_u8  *data,
                                        atc_hdlc_u32        len) {
  if (!ctx) return ATC_HDLC_ERR_INVALID_PARAM;
  if (ctx->config && len > ctx->config->max_frame_size)
    return ATC_HDLC_ERR_FRAME_TOO_LARGE;

  frame_begin(ctx, address, U_CTRL(U_UI, 0));
  for (atc_hdlc_u32 i = 0; i < len; i++)
    emit(ctx, data[i]);
  frame_end(ctx);

  return ATC_HDLC_OK;
}

atc_hdlc_error_t atc_hdlc_transmit_test(atc_hdlc_context_t *ctx,
                                          atc_hdlc_u8         address,
                                          const atc_hdlc_u8  *data,
                                          atc_hdlc_u32        len) {
  if (!ctx) return ATC_HDLC_ERR_INVALID_PARAM;
  if (ctx->config && len > ctx->config->max_frame_size)
    return ATC_HDLC_ERR_FRAME_TOO_LARGE;

  frame_begin(ctx, address, U_CTRL(U_TEST, 1));
  for (atc_hdlc_u32 i = 0; i < len; i++)
    emit(ctx, data[i]);
  frame_end(ctx);

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
                             MOD8) % MOD8);
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

  LOG_DBG("tx: I-Frame V(S)=%u, Len=%lu", ctx->vs, (unsigned long)len);

  frame_begin(ctx, ctx->peer_address, I_CTRL(ctx->vs, ctx->vr, 0));
  for (atc_hdlc_u32 i = 0; i < len; i++)
    emit(ctx, data[i]);
  frame_end(ctx);

  ctx->vs = (atc_hdlc_u8)((ctx->vs + 1) % MOD8);
  t2_stop(ctx);

  if (outstanding == 0)
    t1_start(ctx);

  return ATC_HDLC_OK;
}

void atc_hdlc_transmit_start_ui(atc_hdlc_context_t *ctx, atc_hdlc_u8 address) {
  if (!ctx) return;
  frame_begin(ctx, address, U_CTRL(U_UI, 0));
}

void atc_hdlc_transmit_data(atc_hdlc_context_t *ctx,
                             const atc_hdlc_u8  *data,
                             atc_hdlc_u32        len) {
  if (!ctx || (!data && len > 0)) return;
  for (atc_hdlc_u32 i = 0; i < len; i++)
    emit(ctx, data[i]);
}

void atc_hdlc_transmit_end(atc_hdlc_context_t *ctx) {
  if (!ctx) return;
  frame_end(ctx);
}
