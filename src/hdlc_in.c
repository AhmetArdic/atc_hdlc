/*
 * Copyright (C) 2026 Ahmet Talha ARDIC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../inc/hdlc.h"
#include "hdlc_crc.h"
#include "hdlc_private.h"
#include <string.h>

void reset_state(atc_hdlc_context_t *ctx) {
  ctx->vs = 0;
  ctx->vr = 0;
  ctx->va = 0;
  if (ctx->tx_window && ctx->tx_window->slot_lens)
    memset(ctx->tx_window->slot_lens, 0,
           ctx->tx_window->slot_count * sizeof(ctx->tx_window->slot_lens[0]));
  ctx->next_tx_slot  = 0;
  ctx->rej_exception = false;
  ctx->remote_busy   = false;
  ctx->local_busy    = false;
  ctx->retry_count   = 0;
  t1_stop(ctx);
  t2_stop(ctx);
}

static bool validate_nr(atc_hdlc_context_t *ctx, atc_hdlc_u8 nr) {
  atc_hdlc_u8 diff_nr = (atc_hdlc_u8)((nr      - ctx->va) & (MOD8 - 1));
  atc_hdlc_u8 diff_vs = (atc_hdlc_u8)((ctx->vs - ctx->va) & (MOD8 - 1));
  return diff_nr <= diff_vs;
}

static void frames_acked(atc_hdlc_context_t *ctx, atc_hdlc_u8 nr) {
  if (nr == ctx->va) return;

  atc_hdlc_u8 prev_out = (atc_hdlc_u8)((ctx->vs - ctx->va) & (MOD8 - 1));
  ctx->va          = nr;
  ctx->retry_count = 0;

  if (prev_out >= ctx->window_size)
    if (ctx->platform && ctx->platform->on_event)
      ctx->platform->on_event(ATC_HDLC_EVENT_WINDOW_OPEN, ctx->platform->user_ctx);

  LOG_DBG("rx: N(R)=%u acknowledged, V(A) -> %u", nr, ctx->va);
}

static void check_ack(atc_hdlc_context_t *ctx, atc_hdlc_u8 nr) {
  if (ctx->vs == nr) {
    frames_acked(ctx, nr);
    t1_stop(ctx);
  } else if (ctx->va != nr) {
    frames_acked(ctx, nr);
    t1_start(ctx);
  }
}

static void maybe_respond(atc_hdlc_context_t *ctx, int cmd, atc_hdlc_u8 pf) {
  if (cmd && pf) {
    send_rr_resp(ctx, 1);
    t2_stop(ctx);
  }
}

static void go_back_n(atc_hdlc_context_t *ctx, atc_hdlc_u8 from_seq) {
  if (!ctx->tx_window || ctx->vs == from_seq) return;

  atc_hdlc_u8 old_vs = ctx->vs;
  LOG_WRN("tx: Go-Back-N V(S) %u -> %u", old_vs, from_seq);

  ctx->vs = from_seq;
  if (ctx->tx_window->slots)
    ctx->next_tx_slot = ctx->tx_window->seq_to_slot[ctx->vs];

  while (ctx->vs != old_vs) {
    atc_hdlc_u8 slot      = ctx->tx_window->seq_to_slot[ctx->vs];
    const atc_hdlc_u8 *sd = ctx->tx_window->slots +
                             (slot * ctx->tx_window->slot_capacity);
    atc_hdlc_u32 slen     = ctx->tx_window->slot_lens[slot];

    frame_begin(ctx, ctx->peer_address, I_CTRL(ctx->vs, ctx->vr, 0));
    for (atc_hdlc_u32 i = 0; i < slen; i++)
      emit(ctx, sd[i]);
    frame_end(ctx);

    ctx->vs = (atc_hdlc_u8)((ctx->vs + 1) % MOD8);

    if (ctx->tx_window->slots)
      ctx->next_tx_slot = (atc_hdlc_u8)((ctx->next_tx_slot + 1) % ctx->window_size);
  }
  t1_start(ctx);
}

static void state_disconnected(atc_hdlc_context_t *ctx,
                                atc_hdlc_u8 address, atc_hdlc_u8 ctrl,
                                const atc_hdlc_u8 *info, atc_hdlc_u16 info_len) {
  (void)address;
  if (!is_uframe(ctrl)) return;

  switch (ctrl & ~PF_BIT) {
    case U_SABM:
      LOG_INFO("S0 RX SABM -> S3 TX UA");
      reset_state(ctx);
      send_ua(ctx, CTRL_PF(ctrl));
      set_state(ctx, ATC_HDLC_STATE_CONNECTED, ATC_HDLC_EVENT_INCOMING_CONNECT);
      break;

    case U_DISC:
      LOG_INFO("S0 RX DISC -> TX UA");
      send_ua(ctx, CTRL_PF(ctrl));
      break;

    case U_UI:
      if (ctx->platform && ctx->platform->on_data)
        ctx->platform->on_data(info, info_len, ctx->platform->user_ctx);
      break;

    case U_SNRM:
    case U_SABME:
    case U_SNRME:
    case U_SARME:
      LOG_INFO("S0 RX unsupported mode -> TX DM");
      send_dm(ctx, CTRL_PF(ctrl));
      break;

    default:
      break;
  }
}

static void state_connecting(atc_hdlc_context_t *ctx,
                              atc_hdlc_u8 address, atc_hdlc_u8 ctrl,
                              const atc_hdlc_u8 *info, atc_hdlc_u16 info_len) {
  (void)address; (void)info; (void)info_len;
  if (!is_uframe(ctrl)) return;

  switch (ctrl & ~PF_BIT) {
    case U_SABM:
      LOG_WRN("S1 SABM collision: TX UA, stay in S1");
      send_ua(ctx, CTRL_PF(ctrl));
      break;

    case U_UA:
      if (CTRL_PF(ctrl)) {
        LOG_INFO("S1 RX UA(F=1) -> S3 CONNECTED");
        t1_stop(ctx);
        reset_state(ctx);
        set_state(ctx, ATC_HDLC_STATE_CONNECTED, ATC_HDLC_EVENT_CONNECT_ACCEPTED);
      }
      break;

    case U_DM:
      if (CTRL_PF(ctrl)) {
        LOG_INFO("S1 RX DM(F=1) -> S0 DISCONNECTED");
        t1_stop(ctx);
        set_state(ctx, ATC_HDLC_STATE_DISCONNECTED, ATC_HDLC_EVENT_PEER_REJECT);
      }
      break;

    case U_DISC:
      LOG_INFO("S1 RX DISC -> TX DM");
      send_dm(ctx, CTRL_PF(ctrl));
      break;

    default:
      break;
  }
}

static bool nr_valid(atc_hdlc_context_t *ctx, atc_hdlc_u8 ctrl, atc_hdlc_u8 nr) {
  if (validate_nr(ctx, nr)) return true;
  LOG_WRN("rx: Invalid N(R)=%u (V(A)=%u, V(S)=%u) -> FRMR Z", nr, ctx->va, ctx->vs);
  send_frmr(ctx, ctrl, false, false, false, true);
  set_state(ctx, ATC_HDLC_STATE_FRMR_ERROR, ATC_HDLC_EVENT_PROTOCOL_ERROR);
  return false;
}

static void state_connected(atc_hdlc_context_t *ctx,
                             atc_hdlc_u8 address, atc_hdlc_u8 ctrl,
                             const atc_hdlc_u8 *info, atc_hdlc_u16 info_len) {
  atc_hdlc_u8 pf = CTRL_PF(ctrl);

  if (is_iframe(ctrl)) {
    atc_hdlc_u8 ns = CTRL_NS(ctrl);
    atc_hdlc_u8 nr = CTRL_NR(ctrl);

    LOG_DBG("S3 RX I N(S)=%u N(R)=%u P=%u", ns, nr, pf);

    if (!nr_valid(ctx, ctrl, nr)) return;
    check_ack(ctx, nr);

    if (ns == ctx->vr) {
      ctx->vr = (atc_hdlc_u8)((ctx->vr + 1) % MOD8);
      ctx->rej_exception = false;

      if (ctx->platform && ctx->platform->on_data)
        ctx->platform->on_data(info, info_len, ctx->platform->user_ctx);

      if (pf) {
        if (ctx->local_busy)
          send_rnr(ctx, 1);
        else
          send_rr_resp(ctx, 1);
        t2_stop(ctx);
      } else {
        if (!ctx->t2_active) {
          if (ctx->local_busy)
            send_rnr(ctx, 0);
          else
            t2_start(ctx);
        }
      }
    } else {
      if (ctx->rej_exception) {
        if (pf)
          send_rr_resp(ctx, 1);
      } else {
        LOG_WRN("S3 OOS I N(S)=%u exp=%u -> REJ", ns, ctx->vr);
        ctx->rej_exception = true;
        send_rej(ctx, pf);
        t2_stop(ctx);
      }
    }

  } else if (is_sframe(ctrl)) {
    atc_hdlc_u8 s  = CTRL_S(ctrl);
    atc_hdlc_u8 nr = CTRL_NR(ctrl);
    int cmd        = is_cmd(ctx, address);

    LOG_DBG("S3 RX S s=%u N(R)=%u P/F=%u", s, nr, pf);

    if (s == S_RR) {
      bool was_busy = ctx->remote_busy;
      ctx->remote_busy = false;
      if (was_busy) {
        LOG_DBG("flow: remote_busy cleared by RR");
        if (ctx->platform && ctx->platform->on_event)
          ctx->platform->on_event(ATC_HDLC_EVENT_REMOTE_BUSY_OFF, ctx->platform->user_ctx);
      }
    } else if (s == S_RNR) {
      if (!ctx->remote_busy) {
        ctx->remote_busy = true;
        LOG_DBG("flow: remote_busy set by RNR");
        if (ctx->platform && ctx->platform->on_event)
          ctx->platform->on_event(ATC_HDLC_EVENT_REMOTE_BUSY_ON, ctx->platform->user_ctx);
      }
    } else if (s == S_REJ) {
      ctx->remote_busy = false;
    }

    maybe_respond(ctx, cmd, pf);
    if (!nr_valid(ctx, ctrl, nr)) return;

    if (s == S_REJ) {
      frames_acked(ctx, nr);
      t1_stop(ctx);
      ctx->retry_count = 0;
      if (ctx->va != ctx->vs)
        go_back_n(ctx, nr);
    } else {
      check_ack(ctx, nr);
    }

    if (!cmd && pf) {
      LOG_DBG("S3 F=1 response — check retransmit");
      if (ctx->va != ctx->vs)
        go_back_n(ctx, ctx->va);
    }

  } else if (is_uframe(ctrl)) {
    switch (ctrl & ~PF_BIT) {
      case U_SABM:
        LOG_INFO("S3 RX SABM -> reset + UA");
        reset_state(ctx);
        send_ua(ctx, pf);
        break;

      case U_DISC:
        LOG_INFO("S3 RX DISC -> S0");
        reset_state(ctx);
        send_ua(ctx, pf);
        set_state(ctx, ATC_HDLC_STATE_DISCONNECTED, ATC_HDLC_EVENT_PEER_DISCONNECT);
        break;

      case U_DM:
        LOG_INFO("S3 RX DM -> S0");
        reset_state(ctx);
        set_state(ctx, ATC_HDLC_STATE_DISCONNECTED, ATC_HDLC_EVENT_PEER_REJECT);
        break;

      case U_FRMR:
        LOG_ERR("S3 RX FRMR -> S4 + re-establish");
        reset_state(ctx);
        set_state(ctx, ATC_HDLC_STATE_FRMR_ERROR, ATC_HDLC_EVENT_PROTOCOL_ERROR);
        break;

      case U_UI:
        if (ctx->platform && ctx->platform->on_data)
          ctx->platform->on_data(info, info_len, ctx->platform->user_ctx);
        break;

      case U_TEST:
        if (is_cmd(ctx, address)) {
          frame_begin(ctx, ctx->my_address, U_CTRL(U_TEST, pf));
          for (atc_hdlc_u16 i = 0; i < info_len; i++)
            emit(ctx, info[i]);
          frame_end(ctx);
        } else {
          if (ctx->platform && ctx->platform->on_data && info)
            ctx->platform->on_data(info, info_len, ctx->platform->user_ctx);
        }
        break;

      case U_SNRM:
      case U_SABME:
      case U_SNRME:
      case U_SARME:
        send_dm(ctx, pf);
        break;

      default:
        LOG_WRN("S3 RX unknown U-frame -> FRMR W");
        send_frmr(ctx, ctrl, true, false, false, false);
        break;
    }
  }
}

static void state_disconnecting(atc_hdlc_context_t *ctx,
                                 atc_hdlc_u8 address, atc_hdlc_u8 ctrl,
                                 const atc_hdlc_u8 *info, atc_hdlc_u16 info_len) {
  (void)address; (void)info; (void)info_len;

  if (!is_uframe(ctrl)) {
    if (CTRL_PF(ctrl))
      send_dm(ctx, 1);
    return;
  }

  switch (ctrl & ~PF_BIT) {
    case U_SABM:
    case U_SABME:
      LOG_INFO("S2 RX SABM/SABME -> TX DM");
      send_dm(ctx, CTRL_PF(ctrl));
      break;

    case U_DISC:
      LOG_INFO("S2 RX DISC -> TX UA");
      send_ua(ctx, CTRL_PF(ctrl));
      break;

    case U_UA:
      if (CTRL_PF(ctrl)) {
        LOG_INFO("S2 RX UA(F=1) -> S0 DISCONNECTED");
        t1_stop(ctx);
        set_state(ctx, ATC_HDLC_STATE_DISCONNECTED, ATC_HDLC_EVENT_DISCONNECT_COMPLETE);
      }
      break;

    case U_DM:
      if (CTRL_PF(ctrl)) {
        LOG_INFO("S2 RX DM(F=1) -> S0 DISCONNECTED");
        t1_stop(ctx);
        set_state(ctx, ATC_HDLC_STATE_DISCONNECTED, ATC_HDLC_EVENT_PEER_REJECT);
      }
      break;

    default:
      break;
  }
}

static void state_frmr_error(atc_hdlc_context_t *ctx,
                              atc_hdlc_u8 address, atc_hdlc_u8 ctrl,
                              const atc_hdlc_u8 *info, atc_hdlc_u16 info_len) {
  (void)address; (void)info; (void)info_len;
  if (!is_uframe(ctrl)) return;

  if ((ctrl & ~PF_BIT) == U_SABM) {
    LOG_INFO("S4 RX SABM -> S3 TX UA");
    reset_state(ctx);
    send_ua(ctx, CTRL_PF(ctrl));
    set_state(ctx, ATC_HDLC_STATE_CONNECTED, ATC_HDLC_EVENT_INCOMING_CONNECT);
  }
}

void dispatch_frame(atc_hdlc_context_t *ctx,
                    atc_hdlc_u8 address, atc_hdlc_u8 ctrl,
                    const atc_hdlc_u8 *info, atc_hdlc_u16 info_len) {
  switch (ctx->current_state) {
    case ATC_HDLC_STATE_DISCONNECTED:
      state_disconnected(ctx, address, ctrl, info, info_len);
      break;
    case ATC_HDLC_STATE_CONNECTING:
      state_connecting(ctx, address, ctrl, info, info_len);
      break;
    case ATC_HDLC_STATE_CONNECTED:
      state_connected(ctx, address, ctrl, info, info_len);
      break;
    case ATC_HDLC_STATE_DISCONNECTING:
      state_disconnecting(ctx, address, ctrl, info, info_len);
      break;
    case ATC_HDLC_STATE_FRMR_ERROR:
      state_frmr_error(ctx, address, ctrl, info, info_len);
      break;
    default:
      break;
  }
}

static void handle_flag(atc_hdlc_context_t *ctx) {
  if (ctx->rx_state != RX_HUNT && ctx->rx_index >= MIN_FRAME_LEN) {
    atc_hdlc_u32 dlen   = ctx->rx_index - FCS_LEN;
    atc_hdlc_u16 crc    = ATC_HDLC_FCS_INIT_VALUE;

    for (atc_hdlc_u32 i = 0; i < dlen; i++)
      crc = atc_hdlc_crc_ccitt_update(crc, ctx->rx_buf->buffer[i]);

    atc_hdlc_u16 rx_fcs = (atc_hdlc_u16)(((atc_hdlc_u16)ctx->rx_buf->buffer[dlen] << 8) |
                                           ctx->rx_buf->buffer[dlen + 1]);

    if (crc == rx_fcs) {
      atc_hdlc_u8 address     = ctx->rx_buf->buffer[0];
      atc_hdlc_u8 ctrl        = ctx->rx_buf->buffer[1];
      const atc_hdlc_u8 *info = NULL;
      atc_hdlc_u16 info_len   = 0;

      LOG_DBG("rx: Valid frame (Addr: 0x%02X, Ctrl: 0x%02X, Len: %lu)",
              address, ctrl, dlen);

      if (dlen > ADDR_LEN + CTRL_LEN) {
        info     = &ctx->rx_buf->buffer[ADDR_LEN + CTRL_LEN];
        info_len = (atc_hdlc_u16)(dlen - (ADDR_LEN + CTRL_LEN));
      }
      dispatch_frame(ctx, address, ctrl, info, info_len);
    } else {
      LOG_WRN("rx: CRC Error! Calc: 0x%04X, RX: 0x%04X", crc, rx_fcs);
    }
  }
  ctx->rx_state = RX_ADDR;
  ctx->rx_index = 0;
}

static void rx_byte(atc_hdlc_context_t *ctx, atc_hdlc_u8 byte) {
  if (byte == FLAG)             { handle_flag(ctx); return; }
  if (ctx->rx_state == RX_HUNT) return;
  if (byte == ESC)              { ctx->rx_state = RX_ESC; return; }
  if (ctx->rx_state == RX_ESC) { byte = (atc_hdlc_u8)(byte ^ XOR_MASK); ctx->rx_state = RX_DATA; }

  if (ctx->rx_index >= ctx->rx_buf->capacity) {
    LOG_WRN("rx: Buffer overflow! Max %lu bytes. Discarding.",
            (unsigned long)ctx->rx_buf->capacity);
    ctx->rx_state = RX_HUNT;
    return;
  }

  ctx->rx_buf->buffer[ctx->rx_index++] = byte;

  if (ctx->rx_index == 1) {
    if (byte != ctx->my_address && byte != ctx->peer_address &&
        byte != ATC_HDLC_BROADCAST_ADDRESS) {
      LOG_WRN("rx: Invalid Address 0x%02X. Frame discarded.", byte);
      ctx->rx_state = RX_HUNT;
      ctx->rx_index = 0;
      return;
    }
    ctx->rx_state = RX_DATA;
  }
}

void atc_hdlc_data_in(atc_hdlc_context_t *ctx, const atc_hdlc_u8 *data, atc_hdlc_u32 len) {
  if (!ctx || !data) return;
  for (atc_hdlc_u32 i = 0; i < len; i++)
    rx_byte(ctx, data[i]);
}
