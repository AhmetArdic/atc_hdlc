/*
 * Copyright (C) 2026 Ahmet Talha ARDIC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../inc/hdlc.h"
#include "hdlc_frame.h"
#include <string.h>

void reset_state(atc_hdlc_context_t* ctx) {
    ctx->vs = 0;
    ctx->vr = 0;
    ctx->va = 0;
    ctx->tx_next_slot = 0;
    if (ctx->tx_window && ctx->tx_window->slot_lens)
        memset(ctx->tx_window->slot_lens, 0,
               ctx->tx_window->slot_count * sizeof(ctx->tx_window->slot_lens[0]));
    CTX_CLR(ctx, HDLC_F_REJ_EXCEPTION);
    CTX_CLR(ctx, HDLC_F_REMOTE_BUSY);
    CTX_CLR(ctx, HDLC_F_LOCAL_BUSY);
    ctx->n2 = 0;
    t1_stop(ctx);
    t2_stop(ctx);
}

static bool nr_in_range(atc_hdlc_context_t* ctx, atc_hdlc_u8 nr) {
    atc_hdlc_u8 diff_nr = (atc_hdlc_u8)((nr - ctx->va) & (MOD8 - 1));
    atc_hdlc_u8 diff_vs = (atc_hdlc_u8)((ctx->vs - ctx->va) & (MOD8 - 1));
    return diff_nr <= diff_vs;
}

static void frames_acked(atc_hdlc_context_t* ctx, atc_hdlc_u8 nr) {
    if (nr == ctx->va)
        return;

    atc_hdlc_u8 prev_out = (atc_hdlc_u8)((ctx->vs - ctx->va) & (MOD8 - 1));
    ctx->va = nr;
    ctx->n2 = 0;

    if (prev_out >= ctx->tx_window->slot_count)
        if (ctx->platform->on_event)
            ctx->platform->on_event(ATC_HDLC_EVENT_WINDOW_OPEN, ctx->platform->user_ctx);

    LOG_DBG("rx: N(R)=%u acknowledged, V(A) -> %u", nr, ctx->va);
}

static void process_nr(atc_hdlc_context_t* ctx, atc_hdlc_u8 nr) {
    if (ctx->vs == nr) {
        frames_acked(ctx, nr);
        t1_stop(ctx);
    } else if (ctx->va != nr) {
        frames_acked(ctx, nr);
        t1_start(ctx);
    }
}

static void send_rr_if_polled(atc_hdlc_context_t* ctx, int cmd, atc_hdlc_u8 pf) {
    if (cmd && pf) {
        send_rr_resp(ctx, 1);
        t2_stop(ctx);
    }
}

static void trigger_retransmit(atc_hdlc_context_t* ctx, atc_hdlc_u8 from_seq) {
    if (!ctx->tx_window || ctx->vs == from_seq)
        return;
    LOG_WRN("tx: Go-Back-N V(S) %u -> %u", ctx->vs, from_seq);
    ctx->retransmit_from = from_seq;
    CTX_SET(ctx, HDLC_F_RETRANSMIT_PENDING);
}

static void state_disconnected(atc_hdlc_context_t* ctx, atc_hdlc_u8 address, atc_hdlc_u8 ctrl,
                               const atc_hdlc_u8* info, atc_hdlc_u16 info_len) {
    (void)address;
    if (!is_uframe(ctrl))
        return;

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
        if (ctx->platform->on_data)
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

static void state_connecting(atc_hdlc_context_t* ctx, atc_hdlc_u8 address, atc_hdlc_u8 ctrl,
                             const atc_hdlc_u8* info, atc_hdlc_u16 info_len) {
    (void)address;
    (void)info;
    (void)info_len;
    if (!is_uframe(ctrl))
        return;

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

static bool validate_nr(atc_hdlc_context_t* ctx, atc_hdlc_u8 ctrl, atc_hdlc_u8 nr) {
    if (nr_in_range(ctx, nr))
        return true;
    LOG_WRN("rx: Invalid N(R)=%u (V(A)=%u, V(S)=%u) -> FRMR Z", nr, ctx->va, ctx->vs);
    send_frmr(ctx, ctrl, false, false, false, true);
    set_state(ctx, ATC_HDLC_STATE_FRMR_ERROR, ATC_HDLC_EVENT_PROTOCOL_ERROR);
    return false;
}

static void handle_in_sequence_iframe(atc_hdlc_context_t* ctx, atc_hdlc_u8 pf,
                                      const atc_hdlc_u8* info, atc_hdlc_u16 info_len) {
    ctx->vr = (atc_hdlc_u8)((ctx->vr + 1) % MOD8);
    CTX_CLR(ctx, HDLC_F_REJ_EXCEPTION);

    if (ctx->platform->on_data)
        ctx->platform->on_data(info, info_len, ctx->platform->user_ctx);

    if (pf) {
        if (CTX_FLAG(ctx, HDLC_F_LOCAL_BUSY))
            send_rnr(ctx, 1);
        else
            send_rr_resp(ctx, 1);
        t2_stop(ctx);
    } else {
        if (!CTX_FLAG(ctx, HDLC_F_T2_ACTIVE)) {
            if (CTX_FLAG(ctx, HDLC_F_LOCAL_BUSY))
                send_rnr(ctx, 0);
            else
                t2_start(ctx);
        }
    }
}

static void handle_out_of_sequence_iframe(atc_hdlc_context_t* ctx, atc_hdlc_u8 ns, atc_hdlc_u8 pf) {
    (void)ns; /* used only in LOG_WRN; suppress warning when logs disabled */
    if (CTX_FLAG(ctx, HDLC_F_REJ_EXCEPTION)) {
        if (pf)
            send_rr_resp(ctx, 1);
    } else {
        LOG_WRN("S3 OOS I N(S)=%u exp=%u -> REJ", ns, ctx->vr);
        CTX_SET(ctx, HDLC_F_REJ_EXCEPTION);
        send_rej(ctx, pf);
        t2_stop(ctx);
    }
}

static void handle_iframe(atc_hdlc_context_t* ctx, atc_hdlc_u8 ctrl, const atc_hdlc_u8* info,
                          atc_hdlc_u16 info_len) {
    atc_hdlc_u8 ns = CTRL_NS(ctrl);
    atc_hdlc_u8 nr = CTRL_NR(ctrl);
    atc_hdlc_u8 pf = CTRL_PF(ctrl);

    LOG_DBG("S3 RX I N(S)=%u N(R)=%u P=%u", ns, nr, pf);

    if (!validate_nr(ctx, ctrl, nr))
        return;
    process_nr(ctx, nr);

    if (ns == ctx->vr)
        handle_in_sequence_iframe(ctx, pf, info, info_len);
    else
        handle_out_of_sequence_iframe(ctx, ns, pf);
}

static void handle_sframe(atc_hdlc_context_t* ctx, atc_hdlc_u8 address, atc_hdlc_u8 ctrl) {
    atc_hdlc_u8 s = CTRL_S(ctrl);
    atc_hdlc_u8 nr = CTRL_NR(ctrl);
    atc_hdlc_u8 pf = CTRL_PF(ctrl);
    int cmd = is_cmd(ctx, address);

    LOG_DBG("S3 RX S s=%u N(R)=%u P/F=%u", s, nr, pf);

    if (s == S_RR) {
        int was_busy = CTX_FLAG(ctx, HDLC_F_REMOTE_BUSY);
        CTX_CLR(ctx, HDLC_F_REMOTE_BUSY);
        if (was_busy) {
            LOG_DBG("flow: remote_busy cleared by RR");
            if (ctx->platform->on_event)
                ctx->platform->on_event(ATC_HDLC_EVENT_REMOTE_BUSY_OFF, ctx->platform->user_ctx);
        }
    } else if (s == S_RNR) {
        if (!CTX_FLAG(ctx, HDLC_F_REMOTE_BUSY)) {
            CTX_SET(ctx, HDLC_F_REMOTE_BUSY);
            LOG_DBG("flow: remote_busy set by RNR");
            if (ctx->platform->on_event)
                ctx->platform->on_event(ATC_HDLC_EVENT_REMOTE_BUSY_ON, ctx->platform->user_ctx);
        }
    } else if (s == S_REJ) {
        CTX_CLR(ctx, HDLC_F_REMOTE_BUSY);
    }

    send_rr_if_polled(ctx, cmd, pf);
    if (!validate_nr(ctx, ctrl, nr))
        return;

    if (s == S_REJ) {
        frames_acked(ctx, nr);
        t1_stop(ctx);
        ctx->n2 = 0;
        if (ctx->va != ctx->vs)
            trigger_retransmit(ctx, nr);
    } else {
        process_nr(ctx, nr);
    }

    if (!cmd && pf) {
        LOG_DBG("S3 F=1 response — check retransmit");
        if (ctx->va != ctx->vs)
            trigger_retransmit(ctx, ctx->va);
    }
}

static void handle_uframe(atc_hdlc_context_t* ctx, atc_hdlc_u8 address, atc_hdlc_u8 ctrl,
                          const atc_hdlc_u8* info, atc_hdlc_u16 info_len) {
    atc_hdlc_u8 pf = CTRL_PF(ctrl);

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
        if (ctx->platform->on_data)
            ctx->platform->on_data(info, info_len, ctx->platform->user_ctx);
        break;

    case U_TEST:
        if (is_cmd(ctx, address)) {
            frame_begin(ctx, ctx->my_address, U_CTRL(U_TEST, pf));
            for (atc_hdlc_u16 i = 0; i < info_len; i++)
                emit(ctx, info[i]);
            frame_end(ctx);
        } else {
            if (ctx->platform->on_data && info)
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

static void state_connected(atc_hdlc_context_t* ctx, atc_hdlc_u8 address, atc_hdlc_u8 ctrl,
                            const atc_hdlc_u8* info, atc_hdlc_u16 info_len) {
    if (is_iframe(ctrl))
        handle_iframe(ctx, ctrl, info, info_len);
    else if (is_sframe(ctrl))
        handle_sframe(ctx, address, ctrl);
    else if (is_uframe(ctrl))
        handle_uframe(ctx, address, ctrl, info, info_len);
}

static void state_disconnecting(atc_hdlc_context_t* ctx, atc_hdlc_u8 address, atc_hdlc_u8 ctrl,
                                const atc_hdlc_u8* info, atc_hdlc_u16 info_len) {
    (void)address;
    (void)info;
    (void)info_len;

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

static void state_frmr_error(atc_hdlc_context_t* ctx, atc_hdlc_u8 address, atc_hdlc_u8 ctrl,
                             const atc_hdlc_u8* info, atc_hdlc_u16 info_len) {
    (void)address;
    (void)info;
    (void)info_len;
    if (!is_uframe(ctrl))
        return;

    if ((ctrl & ~PF_BIT) == U_SABM) {
        LOG_INFO("S4 RX SABM -> S3 TX UA");
        reset_state(ctx);
        send_ua(ctx, CTRL_PF(ctrl));
        set_state(ctx, ATC_HDLC_STATE_CONNECTED, ATC_HDLC_EVENT_INCOMING_CONNECT);
    }
}

void dispatch_frame(atc_hdlc_context_t* ctx, atc_hdlc_u8 address, atc_hdlc_u8 ctrl,
                    const atc_hdlc_u8* info, atc_hdlc_u16 info_len) {
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
