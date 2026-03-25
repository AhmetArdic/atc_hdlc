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

static void fire_event(atc_hdlc_context_t *ctx, atc_hdlc_event_t event);

atc_hdlc_error_t atc_hdlc_init(atc_hdlc_context_t        *ctx,
                                  const atc_hdlc_config_t   *config,
                                  const atc_hdlc_platform_t *platform,
                                  atc_hdlc_tx_window_t      *tx_window,
                                  atc_hdlc_rx_buffer_t      *rx_buf) {
  if (!ctx || !config || !platform || !rx_buf)
    return ATC_HDLC_ERR_INVALID_PARAM;
  if (!platform->on_send)
    return ATC_HDLC_ERR_INVALID_PARAM;
  if (!rx_buf->buffer)
    return ATC_HDLC_ERR_INVALID_PARAM;

  if (config->mode != ATC_HDLC_MODE_ABM)
    return ATC_HDLC_ERR_UNSUPPORTED_MODE;
  if (config->use_extended)
    return ATC_HDLC_ERR_UNSUPPORTED_MODE;

  if (config->window_size < 1 || config->window_size > 7)
    return ATC_HDLC_ERR_INVALID_PARAM;

  atc_hdlc_u32 min_cap = config->max_frame_size + ADDR_LEN + CTRL_LEN + FCS_LEN;
  if (rx_buf->capacity < min_cap || rx_buf->capacity < MIN_FRAME_LEN)
    return ATC_HDLC_ERR_INCONSISTENT_BUFFER;

  if (tx_window) {
    if (!tx_window->slots || !tx_window->slot_lens || !tx_window->seq_to_slot)
      return ATC_HDLC_ERR_INCONSISTENT_BUFFER;
    if (tx_window->slot_count != config->window_size)
      return ATC_HDLC_ERR_INCONSISTENT_BUFFER;
    if (tx_window->slot_capacity < config->max_frame_size)
      return ATC_HDLC_ERR_INCONSISTENT_BUFFER;
  }

  memset(ctx, 0, sizeof(atc_hdlc_context_t));

  ctx->config    = config;
  ctx->platform  = platform;
  ctx->tx_window = tx_window;
  ctx->rx_buf    = rx_buf;

  ctx->my_address  = config->address;
  ctx->window_size = config->window_size;
  ctx->role        = ATC_HDLC_ROLE_COMBINED;
  ctx->rx_state    = RX_HUNT;
  ctx->rx_crc      = ATC_HDLC_FCS_INIT_VALUE;

  return ATC_HDLC_OK;
}

atc_hdlc_error_t atc_hdlc_link_setup(atc_hdlc_context_t *ctx, atc_hdlc_u8 peer_addr) {
  if (!ctx) return ATC_HDLC_ERR_INVALID_PARAM;
  if (ctx->current_state != ATC_HDLC_STATE_DISCONNECTED)
    return ATC_HDLC_ERR_INVALID_STATE;

  ctx->peer_address = peer_addr;

  LOG_INFO("tx: Sending SABM to peer 0x%02X", ctx->peer_address);
  send_u(ctx, ctx->peer_address, U_CTRL(U_SABM, 1));
  t1_start(ctx);
  set_state(ctx, ATC_HDLC_STATE_CONNECTING, ATC_HDLC_EVENT_LINK_SETUP_REQUEST);
  return ATC_HDLC_OK;
}

atc_hdlc_error_t atc_hdlc_disconnect(atc_hdlc_context_t *ctx) {
  if (!ctx) return ATC_HDLC_ERR_INVALID_PARAM;
  if (ctx->current_state != ATC_HDLC_STATE_CONNECTED &&
      ctx->current_state != ATC_HDLC_STATE_FRMR_ERROR)
    return ATC_HDLC_ERR_INVALID_STATE;

  LOG_INFO("tx: Sending DISC to peer 0x%02X", ctx->peer_address);
  send_u(ctx, ctx->peer_address, U_CTRL(U_DISC, 1));
  t1_start(ctx);
  set_state(ctx, ATC_HDLC_STATE_DISCONNECTING, ATC_HDLC_EVENT_DISCONNECT_REQUEST);
  return ATC_HDLC_OK;
}

atc_hdlc_error_t atc_hdlc_link_reset(atc_hdlc_context_t *ctx) {
  if (!ctx) return ATC_HDLC_ERR_INVALID_PARAM;

  LOG_INFO("state: Link reset initiated");
  reset_state(ctx);
  send_u(ctx, ctx->peer_address, U_CTRL(U_SABM, 1));
  t1_start(ctx);
  set_state(ctx, ATC_HDLC_STATE_CONNECTING, ATC_HDLC_EVENT_RESET);
  return ATC_HDLC_OK;
}


atc_hdlc_error_t atc_hdlc_set_local_busy(atc_hdlc_context_t *ctx, atc_hdlc_bool busy) {
  if (!ctx) return ATC_HDLC_ERR_INVALID_PARAM;
  if (ctx->current_state != ATC_HDLC_STATE_CONNECTED)
    return ATC_HDLC_ERR_INVALID_STATE;

  if (busy && !ctx->local_busy) {
    ctx->local_busy = true;
    LOG_INFO("flow: Local busy asserted");
  } else if (!busy && ctx->local_busy) {
    ctx->local_busy = false;
    send_rr(ctx, 0);
    LOG_INFO("flow: Local busy cleared, RR sent");
  }

  return ATC_HDLC_OK;
}

void atc_hdlc_t1_expired(atc_hdlc_context_t *ctx) {
  if (!ctx) return;

  ctx->t1_active = false;

  const atc_hdlc_u8 max_retries = ctx->config ? ctx->config->max_retries : 0;
  ctx->retry_count++;

  if (max_retries > 0 && ctx->retry_count > max_retries) {
    LOG_ERR("tx: Link failure — N2 exceeded (state %d)", ctx->current_state);
    set_state(ctx, ATC_HDLC_STATE_DISCONNECTED, ATC_HDLC_EVENT_LINK_FAILURE);
    reset_state(ctx);
    return;
  }

  switch (ctx->current_state) {
    case ATC_HDLC_STATE_CONNECTING:
      LOG_WRN("tx: T1 expired in CONNECTING, retry SABM (%u/%u)",
                        ctx->retry_count, max_retries);
      send_u(ctx, ctx->peer_address, U_CTRL(U_SABM, 1));
      t1_start(ctx);
      break;

    case ATC_HDLC_STATE_DISCONNECTING:
      LOG_WRN("tx: T1 expired in DISCONNECTING, retry DISC (%u/%u)",
                        ctx->retry_count, max_retries);
      send_u(ctx, ctx->peer_address, U_CTRL(U_DISC, 1));
      t1_start(ctx);
      break;

    case ATC_HDLC_STATE_CONNECTED:
      if (ctx->va != ctx->vs) {
        LOG_WRN("tx: T1 expired, enquiry RR(P=1) (%u/%u)",
                          ctx->retry_count, max_retries);
        send_rr(ctx, 1);
        t1_start(ctx);
      }
      break;

    case ATC_HDLC_STATE_FRMR_ERROR:
      LOG_WRN("tx: T1 expired in FRMR_ERROR, retransmit FRMR (%u/%u)",
                        ctx->retry_count, max_retries);
      retransmit_frmr(ctx);
      t1_start(ctx);
      break;

    default:
      break;
  }
}

void atc_hdlc_t2_expired(atc_hdlc_context_t *ctx) {
  if (!ctx) return;
  ctx->t2_active = false;
  send_rr(ctx, 0);
}

atc_hdlc_state_t atc_hdlc_get_state(const atc_hdlc_context_t *ctx) {
  if (!ctx) return ATC_HDLC_STATE_DISCONNECTED;
  return ctx->current_state;
}

atc_hdlc_u8 atc_hdlc_get_window_available(const atc_hdlc_context_t *ctx) {
  if (!ctx) return 0;
  atc_hdlc_u8 outstanding = (atc_hdlc_u8)((ctx->vs - ctx->va +
                             MOD8) % MOD8);
  if (outstanding >= ctx->window_size) return 0;
  return (atc_hdlc_u8)(ctx->window_size - outstanding);
}


void atc_hdlc_abort(atc_hdlc_context_t *ctx) {
  if (!ctx) return;

  if (ctx->platform && ctx->platform->on_send) {
    ctx->platform->on_send(FLAG, false, ctx->platform->user_ctx);
    ctx->platform->on_send(FLAG, true,  ctx->platform->user_ctx);
  }

  reset_state(ctx);
  ctx->rx_state      = RX_HUNT;
  ctx->current_state = ATC_HDLC_STATE_DISCONNECTED;
}

static void fire_event(atc_hdlc_context_t *ctx, atc_hdlc_event_t event) {
  if (ctx->platform->on_event)
    ctx->platform->on_event(event, ctx->platform->user_ctx);
}

void set_state(atc_hdlc_context_t *ctx,
               atc_hdlc_state_t    new_state,
               atc_hdlc_event_t    event) {
  atc_hdlc_bool state_changed = (ctx->current_state != new_state);

  /* INCOMING_CONNECT always fires even if state did not change: SABM in CONNECTED
   * resets the link (vs/vr zeroed) but stays CONNECTED, so state_changed is false
   * yet the application must be notified that a new session has started. */
  if (state_changed || event == ATC_HDLC_EVENT_INCOMING_CONNECT) {
    LOG_INFO("state: %d -> %d (event: %d)",
                       ctx->current_state, new_state, event);
    ctx->current_state = new_state;
    fire_event(ctx, event);
  }
}
