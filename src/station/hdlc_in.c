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

static void hdlc_state_disconnected (atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame);
static void hdlc_state_connecting   (atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame);
static void hdlc_state_connected    (atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame);
static void hdlc_state_disconnecting(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame);
static void hdlc_state_frmr_error   (atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame);

static void         hdlc_process_nr          (atc_hdlc_context_t *ctx, atc_hdlc_u8 nr);
static void         hdlc_retransmit_go_back_n(atc_hdlc_context_t *ctx, atc_hdlc_u8 from_seq);
static inline void  hdlc_enter_frmr_state    (atc_hdlc_context_t *ctx,
                                               atc_hdlc_u8 rejected_ctrl,
                                               atc_hdlc_bool w, atc_hdlc_bool x,
                                               atc_hdlc_bool y, atc_hdlc_bool z);
static inline atc_hdlc_u_frame_sub_type_t frame_u_type(const atc_hdlc_frame_t *f);

void hdlc_reset_connection_state(atc_hdlc_context_t *ctx) {
    ctx->vs = 0;
    ctx->vr = 0;
    ctx->va = 0;
    if (ctx->tx_window != NULL && ctx->tx_window->slot_lens != NULL) {
        memset(ctx->tx_window->slot_lens, 0,
               ctx->tx_window->slot_count * sizeof(ctx->tx_window->slot_lens[0]));
    }
    ctx->next_tx_slot  = 0;
    ctx->rej_exception = false;
    ctx->remote_busy   = false;
    ctx->local_busy    = false;
    ctx->retry_count   = 0;
    hdlc_t1_stop(ctx);
    hdlc_t2_stop(ctx);
}

void hdlc_send_frmr(atc_hdlc_context_t *ctx,
                    atc_hdlc_u8 rejected_ctrl,
                    atc_hdlc_bool w, atc_hdlc_bool x,
                    atc_hdlc_bool y, atc_hdlc_bool z)
{
    atc_hdlc_u8 info[3];
    info[0] = rejected_ctrl;
    info[1] = (atc_hdlc_u8)(((ctx->vr & 0x07) << 5) | 0x00 | ((ctx->vs & 0x07) << 1));
    info[2] = (atc_hdlc_u8)((w ? HDLC_FRMR_W_BIT : 0) |
                              (x ? HDLC_FRMR_X_BIT : 0) |
                              (y ? HDLC_FRMR_Y_BIT : 0) |
                              (z ? HDLC_FRMR_Z_BIT : 0));

    ATC_HDLC_LOG_ERROR("tx: FRMR ctrl=0x%02X W=%u X=%u Y=%u Z=%u",
                       rejected_ctrl, (unsigned)w, (unsigned)x,
                       (unsigned)y, (unsigned)z);

    atc_hdlc_u8 ctrl = HDLC_U_CTRL(HDLC_U_FRMR, 0);
    atc_hdlc_transmit_start(ctx, ctx->my_address, ctrl);
    atc_hdlc_transmit_data(ctx, info, sizeof(info));
    atc_hdlc_transmit_end(ctx);

    HDLC_STAT_INC(ctx, frmr_count);

    hdlc_t1_stop(ctx);
    hdlc_t2_stop(ctx);
    hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_FRMR_ERROR, ATC_HDLC_EVENT_PROTOCOL_ERROR);
}

static inline void hdlc_enter_frmr_state(atc_hdlc_context_t *ctx,
                                          atc_hdlc_u8 rejected_ctrl,
                                          atc_hdlc_bool w, atc_hdlc_bool x,
                                          atc_hdlc_bool y, atc_hdlc_bool z)
{
    hdlc_send_frmr(ctx, rejected_ctrl, w, x, y, z);
}

static inline atc_hdlc_u_frame_sub_type_t frame_u_type(const atc_hdlc_frame_t *f) {
    return atc_hdlc_get_u_frame_sub_type(f->control);
}

static void hdlc_state_disconnected(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame)
{
    if (!hdlc_is_u_frame(frame->control)) return;

    switch (frame_u_type(frame)) {
        case ATC_HDLC_U_FRAME_TYPE_SABM:
            ATC_HDLC_LOG_DEBUG("S0 RX SABM -> S3 TX UA");
            hdlc_reset_connection_state(ctx);
            hdlc_send_ua(ctx, HDLC_CTRL_PF(frame->control));
            hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_CONNECTED,
                                     ATC_HDLC_EVENT_INCOMING_CONNECT);
            break;

        case ATC_HDLC_U_FRAME_TYPE_DISC:
            ATC_HDLC_LOG_DEBUG("S0 RX DISC -> TX UA");
            hdlc_send_ua(ctx, HDLC_CTRL_PF(frame->control));
            break;

        case ATC_HDLC_U_FRAME_TYPE_UI:
            if (ctx->platform && ctx->platform->on_data) {
                ctx->platform->on_data(frame->information, frame->information_len,
                                       ctx->platform->user_ctx);
            }
            break;

        case ATC_HDLC_U_FRAME_TYPE_SNRM:
        case ATC_HDLC_U_FRAME_TYPE_SARM:
        case ATC_HDLC_U_FRAME_TYPE_SABME:
        case ATC_HDLC_U_FRAME_TYPE_SNRME:
        case ATC_HDLC_U_FRAME_TYPE_SARME:
            ATC_HDLC_LOG_DEBUG("S0 RX unsupported mode -> TX DM");
            hdlc_send_dm(ctx, HDLC_CTRL_PF(frame->control));
            break;

        default:
            break;
    }
}

static void hdlc_state_connecting(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame)
{
    if (hdlc_is_u_frame(frame->control)) {
        switch (frame_u_type(frame)) {
            case ATC_HDLC_U_FRAME_TYPE_SABM:
                if (ctx->peer_address > ctx->my_address) {
                    ATC_HDLC_LOG_WARN("S1 SABM collision: I lost, backing off");
                    return;
                }
                ATC_HDLC_LOG_WARN("S1 SABM collision: I won, sending UA");
                hdlc_reset_connection_state(ctx);
                hdlc_send_ua(ctx, HDLC_CTRL_PF(frame->control));
                hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_CONNECTED,
                                         ATC_HDLC_EVENT_INCOMING_CONNECT);
                break;

            case ATC_HDLC_U_FRAME_TYPE_UA:
                if (HDLC_CTRL_PF(frame->control)) {
                    ATC_HDLC_LOG_DEBUG("S1 RX UA(F=1) -> S3 CONNECTED");
                    hdlc_t1_stop(ctx);
                    hdlc_reset_connection_state(ctx);
                    hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_CONNECTED,
                                             ATC_HDLC_EVENT_CONNECT_ACCEPTED);
                }
                break;

            case ATC_HDLC_U_FRAME_TYPE_DM:
                if (HDLC_CTRL_PF(frame->control)) {
                    ATC_HDLC_LOG_DEBUG("S1 RX DM(F=1) -> S0 DISCONNECTED");
                    hdlc_t1_stop(ctx);
                    hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_DISCONNECTED,
                                             ATC_HDLC_EVENT_PEER_REJECT);
                }
                break;

            case ATC_HDLC_U_FRAME_TYPE_DISC:
                ATC_HDLC_LOG_DEBUG("S1 RX DISC -> TX DM");
                hdlc_send_dm(ctx, HDLC_CTRL_PF(frame->control));
                break;

            default:
                break;
        }
    }
}

static void hdlc_process_nr(atc_hdlc_context_t *ctx, atc_hdlc_u8 nr) {
    atc_hdlc_u8 diff_nr = (nr - ctx->va) & (HDLC_SEQUENCE_MODULUS - 1);
    atc_hdlc_u8 diff_vs = (ctx->vs - ctx->va) & (HDLC_SEQUENCE_MODULUS - 1);

    if (diff_nr > diff_vs) {
        ATC_HDLC_LOG_WARN("rx: Invalid N(R)=%u (V(A)=%u, V(S)=%u) -> FRMR Z",
                          nr, ctx->va, ctx->vs);
        hdlc_enter_frmr_state(ctx, ctx->rx_frame.control, false, false, false, true);
        return;
    }

    if (nr != ctx->va) {
        atc_hdlc_u8 old_va = ctx->va;
        ctx->va = nr;
        ctx->retry_count = 0;
        ctx->rej_exception = false;

        atc_hdlc_u8 was_outstanding = (atc_hdlc_u8)((ctx->vs - old_va) &
                                       (HDLC_SEQUENCE_MODULUS - 1));
        if (was_outstanding >= ctx->window_size) {
            if (ctx->platform && ctx->platform->on_event) {
                ctx->platform->on_event(ATC_HDLC_EVENT_WINDOW_OPEN,
                                         ctx->platform->user_ctx);
            }
        }

        ATC_HDLC_LOG_DEBUG("rx: N(R)=%u acknowledged, V(A) -> %u", nr, ctx->va);
    }

    if (ctx->va == ctx->vs) {
        hdlc_t1_stop(ctx);
    } else {
        hdlc_t1_start(ctx);
    }
}

static void hdlc_retransmit_go_back_n(atc_hdlc_context_t *ctx, atc_hdlc_u8 from_seq) {
    if (ctx->tx_window == NULL) return;
    if (ctx->vs == from_seq) return;

    atc_hdlc_u8 old_vs = ctx->vs;
    ATC_HDLC_LOG_WARN("tx: Go-Back-N V(S) %u -> %u", old_vs, from_seq);

    ctx->vs = from_seq;
    if (ctx->tx_window->slots != NULL)
        ctx->next_tx_slot = ctx->tx_window->seq_to_slot[ctx->vs];

    while (ctx->vs != old_vs) {
        atc_hdlc_u8 slot = ctx->tx_window->seq_to_slot[ctx->vs];

        atc_hdlc_frame_t frame = {
            .address = ctx->peer_address,
            .control = HDLC_I_CTRL(ctx->vs, ctx->vr, 0),
            .information = ctx->tx_window->slots + (slot * ctx->tx_window->slot_capacity),
            .information_len = (atc_hdlc_u16)ctx->tx_window->slot_lens[slot]
        };

        hdlc_transmit_frame(ctx, &frame);

        HDLC_STAT_INC(ctx, tx_i_frames);
        
        ctx->vs = (atc_hdlc_u8)((ctx->vs + 1) % HDLC_SEQUENCE_MODULUS);
        
        if (ctx->tx_window->slots != NULL)
            ctx->next_tx_slot = (atc_hdlc_u8)((ctx->next_tx_slot + 1) % ctx->window_size);
    }
    hdlc_t1_start(ctx);
}

static void hdlc_state_connected(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame)
{
    atc_hdlc_u8 ctrl   = frame->control;
    atc_hdlc_u8 msg_pf = HDLC_CTRL_PF(ctrl);

    if (hdlc_is_i_frame(ctrl)) {
        atc_hdlc_u8 msg_ns = HDLC_CTRL_I_NS(ctrl);
        atc_hdlc_u8 msg_nr = HDLC_CTRL_NR(ctrl);

        ATC_HDLC_LOG_DEBUG("S3 RX I N(S)=%u N(R)=%u P=%u", msg_ns, msg_nr, msg_pf);

        hdlc_process_nr(ctx, msg_nr);
        if (ctx->current_state == ATC_HDLC_STATE_FRMR_ERROR) return;

        if (msg_ns == ctx->vr) {
            ctx->vr = (atc_hdlc_u8)((ctx->vr + 1) % HDLC_SEQUENCE_MODULUS);
            ctx->rej_exception = false;
            HDLC_STAT_INC(ctx, rx_i_frames);
            HDLC_STAT_ADD(ctx, rx_bytes, frame->information_len);

            if (ctx->platform && ctx->platform->on_data) {
                ctx->platform->on_data(frame->information, frame->information_len,
                                       ctx->platform->user_ctx);
            }

            if (msg_pf) {
                if (ctx->local_busy) {
                    hdlc_send_rnr(ctx, 1);
                    HDLC_STAT_INC(ctx, rnr_sent);
                } else {
                    hdlc_send_response_rr(ctx, 1);
                }
                hdlc_t2_stop(ctx);
            } else {
                if (!ctx->t2_active) {
                    if (ctx->local_busy) {
                        hdlc_send_rnr(ctx, 0);
                        HDLC_STAT_INC(ctx, rnr_sent);
                    } else {
                        hdlc_t2_start(ctx);
                    }
                }
            }
        } else {
            if (ctx->rej_exception) {
                if (msg_pf) {
                    hdlc_send_response_rr(ctx, 1);
                }
            } else {
                ATC_HDLC_LOG_WARN("S3 OOS I N(S)=%u exp=%u -> REJ", msg_ns, ctx->vr);
                ctx->rej_exception = true;
                hdlc_send_rej(ctx, msg_pf);
                HDLC_STAT_INC(ctx, rej_sent);
                hdlc_t2_stop(ctx);
            }
        }

    } else if (hdlc_is_s_frame(ctrl)) {
        atc_hdlc_u8 s_bits = HDLC_CTRL_S_BITS(ctrl);
        atc_hdlc_u8 msg_nr = HDLC_CTRL_NR(ctrl);
        bool is_cmd = hdlc_is_cmd(ctx, frame);

        ATC_HDLC_LOG_DEBUG("S3 RX S s=%u N(R)=%u P/F=%u", s_bits, msg_nr, msg_pf);

        if (s_bits == HDLC_S_RR) {
            bool was_busy = ctx->remote_busy;
            ctx->remote_busy = false;
            if (was_busy) {
                ATC_HDLC_LOG_DEBUG("flow: remote_busy cleared by RR");
                if (ctx->platform && ctx->platform->on_event)
                    ctx->platform->on_event(ATC_HDLC_EVENT_REMOTE_BUSY_OFF,
                                             ctx->platform->user_ctx);
            }
            hdlc_process_nr(ctx, msg_nr);
            if (ctx->current_state == ATC_HDLC_STATE_FRMR_ERROR) return;

        } else if (s_bits == HDLC_S_RNR) {
            if (!ctx->remote_busy) {
                ctx->remote_busy = true;
                ATC_HDLC_LOG_DEBUG("flow: remote_busy set by RNR");
                HDLC_STAT_INC(ctx, rnr_received);
                if (ctx->platform && ctx->platform->on_event)
                    ctx->platform->on_event(ATC_HDLC_EVENT_REMOTE_BUSY_ON,
                                             ctx->platform->user_ctx);
            }
            hdlc_process_nr(ctx, msg_nr);
            if (ctx->current_state == ATC_HDLC_STATE_FRMR_ERROR) return;

        } else if (s_bits == HDLC_S_REJ) {
            ctx->remote_busy = false;
            hdlc_process_nr(ctx, msg_nr);
            if (ctx->current_state == ATC_HDLC_STATE_FRMR_ERROR) return;

            if (!ctx->rej_exception && ctx->va != ctx->vs) {
                ctx->rej_exception = true;
                HDLC_STAT_INC(ctx, rej_received);
                hdlc_retransmit_go_back_n(ctx, msg_nr);
            }
        }

        if (is_cmd && msg_pf) {
            hdlc_send_response_rr(ctx, 1);
        } else if (!is_cmd && msg_pf) {
            ATC_HDLC_LOG_DEBUG("S3 F=1 response — check retransmit");
            if (ctx->va != ctx->vs)
                hdlc_retransmit_go_back_n(ctx, ctx->va);
        }

    } else if (hdlc_is_u_frame(ctrl)) {
        switch (frame_u_type(frame)) {
            case ATC_HDLC_U_FRAME_TYPE_SABM:
                ATC_HDLC_LOG_DEBUG("S3 RX SABM -> reset + UA");
                hdlc_reset_connection_state(ctx);
                hdlc_send_ua(ctx, msg_pf);
                break;

            case ATC_HDLC_U_FRAME_TYPE_DISC:
                ATC_HDLC_LOG_DEBUG("S3 RX DISC -> S0");
                hdlc_reset_connection_state(ctx);
                hdlc_send_ua(ctx, msg_pf);
                hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_DISCONNECTED,
                                         ATC_HDLC_EVENT_PEER_DISCONNECT);
                break;

            case ATC_HDLC_U_FRAME_TYPE_DM:
                ATC_HDLC_LOG_DEBUG("S3 RX DM -> S0");
                hdlc_reset_connection_state(ctx);
                hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_DISCONNECTED,
                                         ATC_HDLC_EVENT_PEER_REJECT);
                break;

            case ATC_HDLC_U_FRAME_TYPE_FRMR:
                ATC_HDLC_LOG_ERROR("S3 RX FRMR -> S4 + re-establish");
                HDLC_STAT_INC(ctx, frmr_count);
                hdlc_reset_connection_state(ctx);
                hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_FRMR_ERROR,
                                         ATC_HDLC_EVENT_PROTOCOL_ERROR);
                break;

            case ATC_HDLC_U_FRAME_TYPE_UI:
                if (ctx->platform && ctx->platform->on_data) {
                    ctx->platform->on_data(frame->information, frame->information_len,
                                           ctx->platform->user_ctx);
                }
                break;

            case ATC_HDLC_U_FRAME_TYPE_TEST:
                if (hdlc_is_cmd(ctx, frame)) {
                    atc_hdlc_u8 resp_ctrl = HDLC_U_CTRL(HDLC_U_TEST, msg_pf);
                    atc_hdlc_transmit_start(ctx, ctx->my_address, resp_ctrl);
                    if (frame->information != NULL && frame->information_len > 0)
                        atc_hdlc_transmit_data(ctx, frame->information,
                                               frame->information_len);
                    atc_hdlc_transmit_end(ctx);
                } else {
                    if (ctx->platform && ctx->platform->on_data &&
                        frame->information != NULL) {
                        ctx->platform->on_data(frame->information, frame->information_len,
                                               ctx->platform->user_ctx);
                    }
                }
                break;

            case ATC_HDLC_U_FRAME_TYPE_SNRM:
            case ATC_HDLC_U_FRAME_TYPE_SARM:
            case ATC_HDLC_U_FRAME_TYPE_SABME:
            case ATC_HDLC_U_FRAME_TYPE_SNRME:
            case ATC_HDLC_U_FRAME_TYPE_SARME:
                hdlc_send_dm(ctx, msg_pf);
                break;

            default:
                ATC_HDLC_LOG_WARN("S3 RX unknown U-frame -> FRMR W");
                hdlc_enter_frmr_state(ctx, ctrl, true, false, false, false);
                break;
        }
    }
}

static void hdlc_state_disconnecting(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame)
{
    if (!hdlc_is_u_frame(frame->control)) {
        if (HDLC_CTRL_PF(frame->control))
            hdlc_send_dm(ctx, 1);
        return;
    }

    switch (frame_u_type(frame)) {
        case ATC_HDLC_U_FRAME_TYPE_SABM:
        case ATC_HDLC_U_FRAME_TYPE_SABME:
            ATC_HDLC_LOG_DEBUG("S2 RX SABM/SABME -> TX DM");
            hdlc_send_dm(ctx, HDLC_CTRL_PF(frame->control));
            break;

        case ATC_HDLC_U_FRAME_TYPE_DISC:
            ATC_HDLC_LOG_DEBUG("S2 RX DISC -> TX UA");
            hdlc_send_ua(ctx, HDLC_CTRL_PF(frame->control));
            break;

        case ATC_HDLC_U_FRAME_TYPE_UA:
            if (HDLC_CTRL_PF(frame->control)) {
                ATC_HDLC_LOG_DEBUG("S2 RX UA(F=1) -> S0 DISCONNECTED");
                hdlc_t1_stop(ctx);
                hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_DISCONNECTED,
                                         ATC_HDLC_EVENT_DISCONNECT_COMPLETE);
            }
            break;

        case ATC_HDLC_U_FRAME_TYPE_DM:
            if (HDLC_CTRL_PF(frame->control)) {
                ATC_HDLC_LOG_DEBUG("S2 RX DM(F=1) -> S0 DISCONNECTED");
                hdlc_t1_stop(ctx);
                hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_DISCONNECTED,
                                         ATC_HDLC_EVENT_PEER_REJECT);
            }
            break;

        default:
            break;
    }
}

static void hdlc_state_frmr_error(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame)
{
    if (!hdlc_is_u_frame(frame->control)) {
        return;
    }

    switch (frame_u_type(frame)) {
        case ATC_HDLC_U_FRAME_TYPE_SABM:
            ATC_HDLC_LOG_DEBUG("S4 RX SABM -> S3 TX UA");
            hdlc_reset_connection_state(ctx);
            hdlc_send_ua(ctx, HDLC_CTRL_PF(frame->control));
            hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_CONNECTED,
                                     ATC_HDLC_EVENT_INCOMING_CONNECT);
            break;

        default:
            break;
    }
}

void hdlc_process_complete_frame(atc_hdlc_context_t *ctx) {
    switch (ctx->current_state) {
        case ATC_HDLC_STATE_DISCONNECTED:
            hdlc_state_disconnected(ctx, &ctx->rx_frame);
            break;
        case ATC_HDLC_STATE_CONNECTING:
            hdlc_state_connecting(ctx, &ctx->rx_frame);
            break;
        case ATC_HDLC_STATE_CONNECTED:
            hdlc_state_connected(ctx, &ctx->rx_frame);
            break;
        case ATC_HDLC_STATE_DISCONNECTING:
            hdlc_state_disconnecting(ctx, &ctx->rx_frame);
            break;
        case ATC_HDLC_STATE_FRMR_ERROR:
            hdlc_state_frmr_error(ctx, &ctx->rx_frame);
            break;
        default:
            break;
    }
}

static void hdlc_data_in(atc_hdlc_context_t *ctx, atc_hdlc_u8 byte) {
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
