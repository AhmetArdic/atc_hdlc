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
 * @brief HDLC Frame Processing (I/S/U Handlers).
 *
 * Contains the frame dispatcher and all handlers for received I, S, and U
 * frames, including connection management commands and sequence number logic.
 */

#include "../../inc/hdlc.h"
#include "../hdlc_private.h"
#include <string.h>

/* Forward declarations for static helpers */
static bool handle_i_frame(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame);
static bool handle_s_frame(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame);
static bool handle_u_frame(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame);
static inline void hdlc_process_nr(atc_hdlc_context_t *ctx, atc_hdlc_u8 nr);

/*
 * --------------------------------------------------------------------------
 * FRAME DISPATCHER
 * --------------------------------------------------------------------------
 */

void hdlc_process_complete_frame(atc_hdlc_context_t *ctx) {
  atc_hdlc_u8 ctrl = ctx->rx_frame.control;
  bool pass_to_user = false;

  ctx->rx_frame.type = hdlc_resolve_frame_type(ctrl);

  switch (ctx->rx_frame.type) {
    case ATC_HDLC_FRAME_I:
      pass_to_user = handle_i_frame(ctx, &ctx->rx_frame);
      break;
    case ATC_HDLC_FRAME_S:
      pass_to_user = handle_s_frame(ctx, &ctx->rx_frame);
      break;
    case ATC_HDLC_FRAME_U:
      pass_to_user = handle_u_frame(ctx, &ctx->rx_frame);
      break;
    default:
      break;
  }

  if (pass_to_user && ctx->platform && ctx->platform->on_data) {
    const atc_hdlc_frame_t *f = &ctx->rx_frame;
    ctx->platform->on_data(f->information, f->information_len,
                           ctx->platform->user_ctx);
  }

  ctx->stats.rx_i_frames++;
}

/*
 * --------------------------------------------------------------------------
 * I-FRAME HANDLER
 * --------------------------------------------------------------------------
 */

static bool handle_i_frame(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
  atc_hdlc_u8 msg_ns = HDLC_CTRL_I_NS(frame->control);
  atc_hdlc_u8 msg_nr = HDLC_CTRL_NR(frame->control);
  atc_hdlc_u8 msg_p  = HDLC_CTRL_PF(frame->control);

  ATC_HDLC_LOG_DEBUG("rx: I-Frame N(S)=%u, N(R)=%u, P/F=%u", msg_ns, msg_nr, msg_p);

  if (msg_ns == ctx->vr) {
      ctx->vr = (ctx->vr + 1) % HDLC_SEQUENCE_MODULUS;
      ctx->t2_timer = ctx->config->t2_ms;
  } else {
      ATC_HDLC_LOG_WARN("rx: Out of sequence I-Frame (Exp %u, got %u). Sending REJ.",
                        ctx->vr, msg_ns);
      hdlc_send_rej(ctx, msg_p);
      return false;
  }

  hdlc_process_nr(ctx, msg_nr);

  if (msg_p) {
      hdlc_send_rr(ctx, 1);
      ctx->t2_timer = 0;
  }

  return true;
}

/*
 * --------------------------------------------------------------------------
 * S-FRAME HANDLER
 * --------------------------------------------------------------------------
 */

static void hdlc_retransmit_go_back_n(atc_hdlc_context_t *ctx, atc_hdlc_u8 from_seq) {
    if (ctx->vs == from_seq) return;

    atc_hdlc_u8 old_vs = ctx->vs;

    ATC_HDLC_LOG_WARN("tx: Go-Back-N triggered! Rewinding V(S) from %u back to %u",
                      old_vs, from_seq);

    ctx->vs = from_seq;

    if (ctx->tx_window->slots != NULL && ctx->tx_window->slot_capacity > 0) {
        ctx->next_tx_slot = ctx->tx_window->seq_to_slot[ctx->vs];
    }

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
        if (ctx->tx_window->slots != NULL && ctx->tx_window->slot_capacity > 0) {
            ctx->next_tx_slot = (ctx->next_tx_slot + 1) % ctx->window_size;
        }
    }

    ctx->t1_timer = ctx->config->t1_ms;
}

static bool handle_s_frame(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
  atc_hdlc_u8 mode     = HDLC_CTRL_S_BITS(frame->control);
  atc_hdlc_u8 msg_nr   = HDLC_CTRL_NR(frame->control);
  atc_hdlc_u8 msg_pf   = HDLC_CTRL_PF(frame->control);
  bool is_command = (frame->address == ctx->my_address);

  ATC_HDLC_LOG_DEBUG("rx: S-Frame S=%u, N(R)=%u, P/F=%u", mode, msg_nr, msg_pf);

  if (mode == HDLC_S_RR || mode == HDLC_S_RNR) {
      hdlc_process_nr(ctx, msg_nr);
  } else if (mode == HDLC_S_REJ) {
      hdlc_process_nr(ctx, msg_nr);

      if (!ctx->rej_exception && ctx->va != ctx->vs) {
          ctx->rej_exception = true;
          hdlc_retransmit_go_back_n(ctx, msg_nr);
      }
  }

  if (is_command && msg_pf) {
      hdlc_send_response_rr(ctx, 1);
  } else if (!is_command && msg_pf) {
      ATC_HDLC_LOG_WARN("rx: Received F=1 response. Checking for retransmissions.");
      if (ctx->va != ctx->vs) {
          hdlc_retransmit_go_back_n(ctx, ctx->va);
      }
  }

  return false;
}

/*
 * --------------------------------------------------------------------------
 * U-FRAME HANDLER
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
    ctx->t2_timer      = 0;
    ctx->rej_exception = false;
    ctx->t1_timer      = 0;
    ctx->retry_count   = 0;
    ctx->contention_timer = 0;
}

static void hdlc_process_sabm(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
    if (ctx->current_state == ATC_HDLC_STATE_CONNECTING) {
        if (ctx->peer_address > ctx->my_address) {
            ATC_HDLC_LOG_WARN("state: SABM collision. I lost (addr %u < %u). Backing off.",
                              ctx->my_address, ctx->peer_address);
            ctx->contention_timer = ATC_HDLC_DEFAULT_CONTENTION_DELAY_TIMEOUT;
            return;
        } else {
            ATC_HDLC_LOG_WARN("state: SABM collision. I won (addr %u > %u). Answering UA.",
                              ctx->my_address, ctx->peer_address);
        }
    }

    hdlc_reset_connection_state(ctx);
    hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_CONNECTED, ATC_HDLC_EVENT_INCOMING_CONNECT);
    hdlc_send_ua(ctx, HDLC_CTRL_PF(frame->control));
}

static void hdlc_process_snrm(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
    hdlc_send_dm(ctx, HDLC_CTRL_PF(frame->control));
}

static void hdlc_process_sarm(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
    hdlc_send_dm(ctx, HDLC_CTRL_PF(frame->control));
}

static void hdlc_process_sabme(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
    hdlc_send_dm(ctx, HDLC_CTRL_PF(frame->control));
}

static void hdlc_process_snrme(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
    hdlc_send_dm(ctx, HDLC_CTRL_PF(frame->control));
}

static void hdlc_process_sarme(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
    hdlc_send_dm(ctx, HDLC_CTRL_PF(frame->control));
}

static void hdlc_process_disc(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
    hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_DISCONNECTED, ATC_HDLC_EVENT_PEER_DISCONNECT);
    hdlc_send_ua(ctx, HDLC_CTRL_PF(frame->control));
}

static void hdlc_process_ua(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
    (void)frame;
    if (ctx->current_state == ATC_HDLC_STATE_CONNECTING) {
        hdlc_reset_connection_state(ctx);
        hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_CONNECTED, ATC_HDLC_EVENT_CONNECT_ACCEPTED);
    } else if (ctx->current_state == ATC_HDLC_STATE_DISCONNECTING) {
        hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_DISCONNECTED, ATC_HDLC_EVENT_DISCONNECT_COMPLETE);
    }
}

static void hdlc_process_dm(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
    (void)frame;
    hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_DISCONNECTED, ATC_HDLC_EVENT_PEER_REJECT);
}

static void hdlc_process_frmr(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
   if (frame->information_len >= HDLC_FRMR_INFO_MIN_LEN) {
       atc_hdlc_frmr_data_t frmr_data;
       memset(&frmr_data, 0, sizeof(frmr_data));

       frmr_data.rejected_control = frame->information[0];

       atc_hdlc_u8 byte1 = frame->information[1];
       frmr_data.v_s = (byte1 >> HDLC_FRMR_VS_SHIFT) & HDLC_FRMR_VS_MASK;
       frmr_data.cr  = (byte1 & HDLC_FRMR_CR_BIT) ? true : false;
       frmr_data.v_r = (byte1 >> HDLC_FRMR_VR_SHIFT) & HDLC_FRMR_VR_MASK;

       atc_hdlc_u8 byte2 = frame->information[2];
       frmr_data.errors.w = (byte2 & HDLC_FRMR_W_BIT);
       frmr_data.errors.x = (byte2 & HDLC_FRMR_X_BIT);
       frmr_data.errors.y = (byte2 & HDLC_FRMR_Y_BIT);
       frmr_data.errors.z = (byte2 & HDLC_FRMR_Z_BIT);
       frmr_data.errors.v = (byte2 & HDLC_FRMR_V_BIT);

       (void)frmr_data;
       ATC_HDLC_LOG_ERROR("rx: FRMR Received! Peer rejected frame. (Ctrl: 0x%02X, V(S)=%u, V(R)=%u)",
                      frmr_data.rejected_control, frmr_data.v_s, frmr_data.v_r);
   } else {
       ATC_HDLC_LOG_ERROR("rx: FRMR Received but information field too short.");
   }

   ATC_HDLC_LOG_DEBUG("state: FRMR caused disconnect");
   hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_DISCONNECTED, ATC_HDLC_EVENT_PROTOCOL_ERROR);
}

static void hdlc_process_test(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
    atc_hdlc_transmit_start(ctx, ctx->my_address,
        hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_TEST, HDLC_U_MODIFIER_HI_TEST,
                           HDLC_CTRL_PF(frame->control)));

    if (frame->information != NULL && frame->information_len > 0) {
        atc_hdlc_transmit_data_bytes(ctx, frame->information, frame->information_len);
    }

    atc_hdlc_transmit_end(ctx);
}

static bool handle_u_frame(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
  atc_hdlc_u8 m_lo = HDLC_CTRL_U_M_LO(frame->control);
  atc_hdlc_u8 m_hi = HDLC_CTRL_U_M_HI(frame->control);

  ATC_HDLC_LOG_DEBUG("rx: U-Frame M_LO=%u, M_HI=%u", m_lo, m_hi);

  /* 1. COMMANDS -> Addressed to ME or BROADCAST */
  if (frame->address == ctx->my_address || frame->address == ATC_HDLC_BROADCAST_ADDRESS) {

    if (m_lo == HDLC_U_MODIFIER_LO_UI && m_hi == HDLC_U_MODIFIER_HI_UI) {
         return true; /* UI frames contain user payload */
    }

    if (frame->address == ATC_HDLC_BROADCAST_ADDRESS) return false;

    if (m_lo == HDLC_U_MODIFIER_LO_SABM && m_hi == HDLC_U_MODIFIER_HI_SABM) {
        hdlc_process_sabm(ctx, frame);
    } else if (m_lo == HDLC_U_MODIFIER_LO_DISC && m_hi == HDLC_U_MODIFIER_HI_DISC) {
        hdlc_process_disc(ctx, frame);
    } else if (m_lo == HDLC_U_MODIFIER_LO_SNRM && m_hi == HDLC_U_MODIFIER_HI_SNRM) {
        hdlc_process_snrm(ctx, frame);
    } else if (m_lo == HDLC_U_MODIFIER_LO_SARM && m_hi == HDLC_U_MODIFIER_HI_SARM) {
        hdlc_process_sarm(ctx, frame);
    } else if (m_lo == HDLC_U_MODIFIER_LO_SABME && m_hi == HDLC_U_MODIFIER_HI_SABME) {
       hdlc_process_sabme(ctx, frame);
    } else if (m_lo == HDLC_U_MODIFIER_LO_SNRME && m_hi == HDLC_U_MODIFIER_HI_SNRME) {
       hdlc_process_snrme(ctx, frame);
    } else if (m_lo == HDLC_U_MODIFIER_LO_SARME && m_hi == HDLC_U_MODIFIER_HI_SARME) {
       hdlc_process_sarme(ctx, frame);
    } else if (m_lo == HDLC_U_MODIFIER_LO_TEST && m_hi == HDLC_U_MODIFIER_HI_TEST) {
        hdlc_process_test(ctx, frame);
        return true;
    } else {
        ATC_HDLC_LOG_WARN("rx: Unhandled U-Frame Command (M_LO=%u, M_HI=%u)", m_lo, m_hi);
    }
  }

  /* 2. RESPONSES -> Addressed to PEER */
  else if (frame->address == ctx->peer_address) {

    if (m_lo == HDLC_U_MODIFIER_LO_UA && m_hi == HDLC_U_MODIFIER_HI_UA) {
        hdlc_process_ua(ctx, frame);
    } else if (m_lo == HDLC_U_MODIFIER_LO_DM && m_hi == HDLC_U_MODIFIER_HI_DM) {
        hdlc_process_dm(ctx, frame);
    } else if (m_lo == HDLC_U_MODIFIER_LO_FRMR && m_hi == HDLC_U_MODIFIER_HI_FRMR) {
        hdlc_process_frmr(ctx, frame);
    } else if (m_lo == HDLC_U_MODIFIER_LO_TEST && m_hi == HDLC_U_MODIFIER_HI_TEST) {
        return true; /* TEST response — pass to on_data */
    } else {
        ATC_HDLC_LOG_WARN("rx: Unhandled U-Frame Response (M_LO=%u, M_HI=%u)", m_lo, m_hi);
    }
  }

  return false;
}

/*
 * --------------------------------------------------------------------------
 * SEQUENCE NUMBER HELPERS
 * --------------------------------------------------------------------------
 */

static inline bool hdlc_nr_valid(atc_hdlc_u8 va, atc_hdlc_u8 nr, atc_hdlc_u8 vs) {
    atc_hdlc_u8 diff_nr = (nr - va) & (HDLC_SEQUENCE_MODULUS - 1);
    atc_hdlc_u8 diff_vs = (vs - va) & (HDLC_SEQUENCE_MODULUS - 1);
    return (diff_nr <= diff_vs);
}

static inline void hdlc_process_nr(atc_hdlc_context_t *ctx, atc_hdlc_u8 nr) {
    if (hdlc_nr_valid(ctx->va, nr, ctx->vs)) {
        atc_hdlc_u8 old_va = ctx->va;
        if (nr < ctx->va && nr <= ctx->vs) {
             ATC_HDLC_LOG_DEBUG("rx: Peer acknowledged across a wrap-around! (V(A)=%u -> N(R)=%u)",
                                ctx->va, nr);
        } else {
             ATC_HDLC_LOG_DEBUG("rx: Peer acknowledged up to V(A)=%u (now %u)", ctx->va, nr);
        }
        ctx->va = nr;
        ctx->retry_count = 0;

        if (ctx->va != old_va) {
            ctx->rej_exception = false;
        }

        if (ctx->va == ctx->vs) {
            ctx->t1_timer = 0;
        } else {
            ctx->t1_timer = ctx->config->t1_ms;
        }
    } else {
        ATC_HDLC_LOG_WARN("rx: Ignored invalid N(R)=%u (V(A)=%u, V(S)=%u)", nr, ctx->va, ctx->vs);
    }
}
