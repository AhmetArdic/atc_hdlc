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
 * @file hdlc_frame_handlers.c
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief HDLC frame processing — per-state dispatch (Linux LAPB pattern).
 *
 * The frame dispatcher routes each complete, CRC-verified frame to the
 * appropriate per-state handler:
 *
 *   hdlc_state_disconnected()   — State 0: no logical connection
 *   hdlc_state_connecting()     — State 1: SABM sent, awaiting UA
 *   hdlc_state_connected()      — State 3: active data transfer
 *   hdlc_state_disconnecting()  — State 2: DISC sent, awaiting UA
 *   hdlc_state_frmr_error()     — State 4: lock-down after FRMR
 *
 * Sub-conditions within CONNECTED (remote_busy, local_busy, rej_exception)
 * are modelled as boolean flags in the context, not as separate states,
 * consistent with ISO/IEC 13239 §5.5.
 */

#include "../../inc/hdlc.h"
#include "../hdlc_private.h"
#include <string.h>

/*
 * --------------------------------------------------------------------------
 * FORWARD DECLARATIONS
 * --------------------------------------------------------------------------
 */
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

/*
 * --------------------------------------------------------------------------
 * CONNECTION STATE RESET
 * --------------------------------------------------------------------------
 */

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
    hdlc_t3_stop(ctx);
}

/*
 * --------------------------------------------------------------------------
 * FRMR SENDER
 * --------------------------------------------------------------------------
 */

/**
 * @brief Transmit a FRMR response and enter FRMR_ERROR lock-down.
 *
 * FRMR information field (3 bytes):
 *   Byte 0 : rejected control field
 *   Byte 1 : 0 V(S) C/R V(R)  (bits: [7:5]=V(R), [4]=C/R, [3:1]=V(S), [0]=0)
 *   Byte 2 : W X Y Z 0 0 0 0
 */
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

    atc_hdlc_u8 ctrl = hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_FRMR, HDLC_U_MODIFIER_HI_FRMR, 0);
    atc_hdlc_transmit_start(ctx, ctx->my_address, ctrl);
    atc_hdlc_transmit_data_bytes(ctx, info, sizeof(info));
    atc_hdlc_transmit_end(ctx);

    HDLC_STAT_INC(ctx, frmr_count);

    hdlc_t1_stop(ctx);
    hdlc_t2_stop(ctx);
    hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_FRMR_ERROR, ATC_HDLC_EVENT_PROTOCOL_ERROR);
}

/** Convenience wrapper used inline in state handlers. */
static inline void hdlc_enter_frmr_state(atc_hdlc_context_t *ctx,
                                          atc_hdlc_u8 rejected_ctrl,
                                          atc_hdlc_bool w, atc_hdlc_bool x,
                                          atc_hdlc_bool y, atc_hdlc_bool z)
{
    hdlc_send_frmr(ctx, rejected_ctrl, w, x, y, z);
}

/*
 * --------------------------------------------------------------------------
 * MAIN DISPATCHER — routes frame to per-state handler
 * --------------------------------------------------------------------------
 */

void hdlc_process_complete_frame(atc_hdlc_context_t *ctx) {
    atc_hdlc_u8 ctrl = ctx->rx_frame.control;
    ctx->rx_frame.type = hdlc_resolve_frame_type(ctrl);

    /* T3 keep-alive: restart on every received frame while CONNECTED */
    if (ctx->current_state == ATC_HDLC_STATE_CONNECTED && ctx->t3_active) {
        hdlc_t3_stop(ctx);
        hdlc_t3_start(ctx);
    }

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

/*
 * --------------------------------------------------------------------------
 * HELPER: U-frame sub-type shorthand
 * --------------------------------------------------------------------------
 */
static inline atc_hdlc_u_frame_sub_type_t frame_u_type(const atc_hdlc_frame_t *f) {
    return atc_hdlc_get_u_frame_sub_type(f->control);
}

/*
 * --------------------------------------------------------------------------
 * STATE 0 — DISCONNECTED
 * Ref: Linux lapb_state0_machine()
 * --------------------------------------------------------------------------
 * Accepts SABM (passive open), DISC (responds UA), ignores everything else.
 */
static void hdlc_state_disconnected(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame)
{
    if (frame->type != ATC_HDLC_FRAME_U) return;

    switch (frame_u_type(frame)) {
        case ATC_HDLC_U_FRAME_TYPE_SABM:
            ATC_HDLC_LOG_DEBUG("S0 RX SABM -> S3 TX UA");
            hdlc_reset_connection_state(ctx);
            hdlc_send_ua(ctx, HDLC_CTRL_PF(frame->control));
            hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_CONNECTED,
                                     ATC_HDLC_EVENT_INCOMING_CONNECT);
            hdlc_t3_start(ctx);
            break;

        case ATC_HDLC_U_FRAME_TYPE_DISC:
            /* Peer sent DISC while we are already disconnected — respond UA */
            ATC_HDLC_LOG_DEBUG("S0 RX DISC -> TX UA");
            hdlc_send_ua(ctx, HDLC_CTRL_PF(frame->control));
            break;

        case ATC_HDLC_U_FRAME_TYPE_UI:
            /* Connectionless delivery — fire on_data even for empty payload */
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
            /* Unsupported mode setup commands: respond DM */
            ATC_HDLC_LOG_DEBUG("S0 RX unsupported mode -> TX DM");
            hdlc_send_dm(ctx, HDLC_CTRL_PF(frame->control));
            break;

        default:
            /* All other frames ignored in DISCONNECTED */
            break;
    }
}

/*
 * --------------------------------------------------------------------------
 * STATE 1 — CONNECTING (Awaiting UA after SABM)
 * Ref: Linux lapb_state1_machine()
 * --------------------------------------------------------------------------
 */
static void hdlc_state_connecting(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame)
{
    if (frame->type == ATC_HDLC_FRAME_U) {
        switch (frame_u_type(frame)) {
            case ATC_HDLC_U_FRAME_TYPE_SABM:
                /* Contention: both sides sent SABM simultaneously */
                if (ctx->peer_address > ctx->my_address) {
                    /* I lost — back off and let T1 retry */
                    ATC_HDLC_LOG_WARN("S1 SABM collision: I lost, backing off");
                    return;
                }
                /* I won — send UA, transition to CONNECTED */
                ATC_HDLC_LOG_WARN("S1 SABM collision: I won, sending UA");
                hdlc_reset_connection_state(ctx);
                hdlc_send_ua(ctx, HDLC_CTRL_PF(frame->control));
                hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_CONNECTED,
                                         ATC_HDLC_EVENT_INCOMING_CONNECT);
                hdlc_t3_start(ctx);
                break;

            case ATC_HDLC_U_FRAME_TYPE_UA:
                /* Normal connection acceptance */
                if (HDLC_CTRL_PF(frame->control)) {
                    ATC_HDLC_LOG_DEBUG("S1 RX UA(F=1) -> S3 CONNECTED");
                    hdlc_t1_stop(ctx);
                    hdlc_reset_connection_state(ctx);
                    hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_CONNECTED,
                                             ATC_HDLC_EVENT_CONNECT_ACCEPTED);
                    hdlc_t3_start(ctx);
                }
                break;

            case ATC_HDLC_U_FRAME_TYPE_DM:
                /* Peer refused connection */
                if (HDLC_CTRL_PF(frame->control)) {
                    ATC_HDLC_LOG_DEBUG("S1 RX DM(F=1) -> S0 DISCONNECTED");
                    hdlc_t1_stop(ctx);
                    hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_DISCONNECTED,
                                             ATC_HDLC_EVENT_PEER_REJECT);
                }
                break;

            case ATC_HDLC_U_FRAME_TYPE_DISC:
                /* Peer wants to disconnect while we're connecting */
                ATC_HDLC_LOG_DEBUG("S1 RX DISC -> TX DM");
                hdlc_send_dm(ctx, HDLC_CTRL_PF(frame->control));
                break;

            default:
                break;
        }
    }
    /* I/S frames in CONNECTING state are ignored */
}

/*
 * --------------------------------------------------------------------------
 * STATE 3 — CONNECTED
 * Ref: Linux lapb_state3_machine()
 * --------------------------------------------------------------------------
 * Main data-transfer state. Handles I/S/U frames. Sub-conditions
 * (remote_busy, local_busy, rej_exception) are boolean flags, not states.
 */

/** Process an acknowledged N(R) — advance V(A), manage T1. */
static void hdlc_process_nr(atc_hdlc_context_t *ctx, atc_hdlc_u8 nr) {
    /* Validate: V(A) <= N(R) <= V(S)  (modulo arithmetic) */
    atc_hdlc_u8 diff_nr = (nr - ctx->va) & (HDLC_SEQUENCE_MODULUS - 1);
    atc_hdlc_u8 diff_vs = (ctx->vs - ctx->va) & (HDLC_SEQUENCE_MODULUS - 1);

    if (diff_nr > diff_vs) {
        /* Invalid N(R) — send FRMR Z */
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

        /* Fire WINDOW_OPEN if the window was full before this ACK */
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
        hdlc_t1_stop(ctx);  /* All frames acknowledged */
    } else {
        hdlc_t1_start(ctx); /* Outstanding frames remain */
    }
}

/** Go-Back-N retransmit from @p from_seq. */
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
        atc_hdlc_u8 ctrl = hdlc_create_i_ctrl(ctx->vs, ctx->vr, 0);
        atc_hdlc_transmit_start(ctx, ctx->peer_address, ctrl);
        if (ctx->tx_window->slot_lens[slot] > 0 && ctx->tx_window->slots != NULL) {
            atc_hdlc_transmit_data_bytes(ctx,
                ctx->tx_window->slots + (slot * ctx->tx_window->slot_capacity),
                ctx->tx_window->slot_lens[slot]);
        }
        atc_hdlc_transmit_end(ctx);
        ctx->vs = (ctx->vs + 1) % HDLC_SEQUENCE_MODULUS;
        if (ctx->tx_window->slots != NULL)
            ctx->next_tx_slot = (ctx->next_tx_slot + 1) % ctx->window_size;
    }
    hdlc_t1_start(ctx);
}

static void hdlc_state_connected(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame)
{
    atc_hdlc_u8 ctrl   = frame->control;
    atc_hdlc_u8 msg_pf = HDLC_CTRL_PF(ctrl);

    switch (frame->type) {

    /* ---- I-FRAME ---- */
    case ATC_HDLC_FRAME_I: {
        atc_hdlc_u8 msg_ns = HDLC_CTRL_I_NS(ctrl);
        atc_hdlc_u8 msg_nr = HDLC_CTRL_NR(ctrl);

        ATC_HDLC_LOG_DEBUG("S3 RX I N(S)=%u N(R)=%u P=%u", msg_ns, msg_nr, msg_pf);

        /* Validate N(R) first — may send FRMR and return */
        hdlc_process_nr(ctx, msg_nr);
        if (ctx->current_state == ATC_HDLC_STATE_FRMR_ERROR) return;

        if (msg_ns == ctx->vr) {
            /* In-sequence frame */
            ctx->vr = (ctx->vr + 1) % HDLC_SEQUENCE_MODULUS;
            ctx->rej_exception = false;
            HDLC_STAT_INC(ctx, rx_i_frames);
            HDLC_STAT_ADD(ctx, rx_bytes, frame->information_len);

            /* Deliver payload to upper layer (even if empty — caller may want count) */
            if (ctx->platform && ctx->platform->on_data) {
                ctx->platform->on_data(frame->information, frame->information_len,
                                       ctx->platform->user_ctx);
            }

            if (msg_pf) {
                /* P=1 command: respond with F=1 immediately */
                if (ctx->local_busy) {
                    hdlc_send_rnr(ctx, 1);
                    HDLC_STAT_INC(ctx, rnr_sent);
                } else {
                    hdlc_send_response_rr(ctx, 1);
                }
                hdlc_t2_stop(ctx);
            } else {
                /* Start T2 for delayed ACK (unless already running) */
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
            /* Out-of-sequence frame */
            if (ctx->rej_exception) {
                /* REJ already sent — suppress duplicate; respond to P=1 only */
                if (msg_pf) {
                    hdlc_send_response_rr(ctx, 1);
                }
            } else {
                /* First OOS: send REJ, set rej_exception */
                ATC_HDLC_LOG_WARN("S3 OOS I N(S)=%u exp=%u -> REJ", msg_ns, ctx->vr);
                ctx->rej_exception = true;
                hdlc_send_rej(ctx, msg_pf);
                HDLC_STAT_INC(ctx, rej_sent);
                hdlc_t2_stop(ctx);
            }
        }
        break;
    }

    /* ---- S-FRAME ---- */
    case ATC_HDLC_FRAME_S: {
        atc_hdlc_u8 s_bits = HDLC_CTRL_S_BITS(ctrl);
        atc_hdlc_u8 msg_nr = HDLC_CTRL_NR(ctrl);
        bool is_cmd = (frame->address == ctx->my_address);

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

        /* P/F response handling */
        if (is_cmd && msg_pf) {
            hdlc_send_response_rr(ctx, 1);
        } else if (!is_cmd && msg_pf) {
            /* F=1 response: we issued a poll, now check for retransmit */
            ATC_HDLC_LOG_DEBUG("S3 F=1 response — check retransmit");
            if (ctx->va != ctx->vs)
                hdlc_retransmit_go_back_n(ctx, ctx->va);
        }
        break;
    }

    /* ---- U-FRAME ---- */
    case ATC_HDLC_FRAME_U:
        switch (frame_u_type(frame)) {
            case ATC_HDLC_U_FRAME_TYPE_SABM:
                /* Peer reset the link — re-synchronise */
                ATC_HDLC_LOG_DEBUG("S3 RX SABM -> reset + UA");
                hdlc_reset_connection_state(ctx);
                hdlc_send_ua(ctx, msg_pf);
                /* Stay CONNECTED — this is a remote-initiated reset */
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
                /* Peer rejected one of our frames — enter lock-down, re-establish */
                ATC_HDLC_LOG_ERROR("S3 RX FRMR -> S4 + re-establish");
                HDLC_STAT_INC(ctx, frmr_count);
                hdlc_reset_connection_state(ctx);
                hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_FRMR_ERROR,
                                         ATC_HDLC_EVENT_PROTOCOL_ERROR);
                break;

            case ATC_HDLC_U_FRAME_TYPE_UI:
                /* Connectionless delivery — fire on_data even for empty payload */
                if (ctx->platform && ctx->platform->on_data) {
                    ctx->platform->on_data(frame->information, frame->information_len,
                                           ctx->platform->user_ctx);
                }
                break;

            case ATC_HDLC_U_FRAME_TYPE_TEST:
                /* Echo TEST frame back (command → response) */
                if (frame->address == ctx->my_address) {
                    atc_hdlc_u8 resp_ctrl = hdlc_create_u_ctrl(
                        HDLC_U_MODIFIER_LO_TEST, HDLC_U_MODIFIER_HI_TEST, msg_pf);
                    atc_hdlc_transmit_start(ctx, ctx->my_address, resp_ctrl);
                    if (frame->information != NULL && frame->information_len > 0)
                        atc_hdlc_transmit_data_bytes(ctx, frame->information,
                                                      frame->information_len);
                    atc_hdlc_transmit_end(ctx);
                } else {
                    /* TEST response — pass payload to upper layer */
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
                /* Unsupported mode commands — respond DM */
                hdlc_send_dm(ctx, msg_pf);
                break;

            default:
                /* Unimplemented U-frame — send FRMR W */
                ATC_HDLC_LOG_WARN("S3 RX unknown U-frame -> FRMR W");
                hdlc_enter_frmr_state(ctx, ctrl, true, false, false, false);
                break;
        }
        break;

    default:
        break;
    }
}

/*
 * --------------------------------------------------------------------------
 * STATE 2 — DISCONNECTING (Awaiting UA after DISC)
 * Ref: Linux lapb_state2_machine()
 * --------------------------------------------------------------------------
 */
static void hdlc_state_disconnecting(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame)
{
    if (frame->type != ATC_HDLC_FRAME_U) {
        /* I/S frames in DISCONNECTING → respond DM on P=1 */
        if (HDLC_CTRL_PF(frame->control))
            hdlc_send_dm(ctx, 1);
        return;
    }

    switch (frame_u_type(frame)) {
        case ATC_HDLC_U_FRAME_TYPE_SABM:
        case ATC_HDLC_U_FRAME_TYPE_SABME:
            /* Peer initiated new connection while we're disconnecting */
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

/*
 * --------------------------------------------------------------------------
 * STATE 4 — FRMR_ERROR (Lock-down after FRMR)
 * Ref: Linux lapb_state4_machine()
 * --------------------------------------------------------------------------
 * Only SABM (re-establish) is accepted. All other frames are silently
 * ignored (or DM is sent if a P=1 is received).
 * The user must call atc_hdlc_link_reset() or atc_hdlc_disconnect() to
 * escape this state.
 */
static void hdlc_state_frmr_error(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame)
{
    if (frame->type != ATC_HDLC_FRAME_U) {
        /* Ignore I/S frames in FRMR_ERROR */
        return;
    }

    switch (frame_u_type(frame)) {
        case ATC_HDLC_U_FRAME_TYPE_SABM:
            /* Peer trying to re-establish — accept */
            ATC_HDLC_LOG_DEBUG("S4 RX SABM -> S3 TX UA");
            hdlc_reset_connection_state(ctx);
            hdlc_send_ua(ctx, HDLC_CTRL_PF(frame->control));
            hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_CONNECTED,
                                     ATC_HDLC_EVENT_INCOMING_CONNECT);
            hdlc_t3_start(ctx);
            break;

        default:
            /* All other frames ignored in FRMR_ERROR */
            break;
    }
}
