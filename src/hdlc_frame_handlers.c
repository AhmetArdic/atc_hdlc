/**
 * @file hdlc_frame_handlers.c
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief HDLC Frame Processing (I/S/U Handlers).
 *
 * Contains the frame dispatcher and all handlers for received I, S, and U
 * frames, including connection management commands and sequence number logic.
 */

#include "../inc/hdlc.h"
#include "hdlc_private.h"
#include <string.h>

/* Forward declarations for static helpers */
static void handle_i_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame);
static void handle_s_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame);
static void handle_u_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame);
static inline void hdlc_process_nr(hdlc_context_t *ctx, hdlc_u8 nr);
static inline void hdlc_send_u_frame(hdlc_context_t *ctx, hdlc_u8 address, hdlc_u8 m_lo, hdlc_u8 m_hi, hdlc_u8 pf);
static inline void hdlc_send_ua(hdlc_context_t *ctx, hdlc_u8 pf);
static inline void hdlc_send_dm(hdlc_context_t *ctx, hdlc_u8 pf);
static inline void hdlc_send_s_frame(hdlc_context_t *ctx, hdlc_u8 address, hdlc_u8 s_bits, hdlc_u8 nr, hdlc_u8 pf);
static inline void hdlc_send_rr(hdlc_context_t *ctx, hdlc_u8 pf);
static inline void hdlc_send_rej(hdlc_context_t *ctx, hdlc_u8 pf);

/*
 * --------------------------------------------------------------------------
 * FRAME DISPATCHER
 * --------------------------------------------------------------------------
 */

void process_complete_frame(hdlc_context_t *ctx) {
  hdlc_u8 ctrl = ctx->input_frame_buffer.control.value;

  if ((ctrl & HDLC_FRAME_TYPE_MASK_I) == HDLC_FRAME_TYPE_VAL_I) {
    ctx->input_frame_buffer.type = HDLC_FRAME_I;
    handle_i_frame(ctx, &ctx->input_frame_buffer);
  } else if ((ctrl & HDLC_FRAME_TYPE_MASK_S) == HDLC_FRAME_TYPE_VAL_S) {
    ctx->input_frame_buffer.type = HDLC_FRAME_S;
    handle_s_frame(ctx, &ctx->input_frame_buffer);
  } else if ((ctrl & HDLC_FRAME_TYPE_MASK_U) == HDLC_FRAME_TYPE_VAL_U) {
    ctx->input_frame_buffer.type = HDLC_FRAME_U;
    handle_u_frame(ctx, &ctx->input_frame_buffer);
  } else {
    ctx->input_frame_buffer.type = HDLC_FRAME_INVALID;
  }

  if (ctx->on_frame_cb != NULL) {
    ctx->on_frame_cb(&ctx->input_frame_buffer, ctx->user_data);
  }

  ctx->stats_input_frames++;
}

/*
 * --------------------------------------------------------------------------
 * I-FRAME HANDLER
 * --------------------------------------------------------------------------
 */

static void handle_i_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  hdlc_u8 msg_ns = (frame->control.i_frame.ns);
  hdlc_u8 msg_nr = (frame->control.i_frame.nr);
  hdlc_u8 msg_p  = (frame->control.i_frame.pf);

  if (msg_ns == ctx->vr) {
      ctx->vr = (ctx->vr + 1) % HDLC_SEQUENCE_MODULUS;
      ctx->ack_pending = true;
  } else {
      hdlc_send_rej(ctx, msg_p);
      return; 
  }

  hdlc_process_nr(ctx, msg_nr);
  
  if (msg_p) {
      hdlc_send_rr(ctx, 1);
      ctx->ack_pending = false;
  } 
  else if (ctx->ack_pending) {
       hdlc_send_rr(ctx, 0);
       ctx->ack_pending = false;
  }
}

/*
 * --------------------------------------------------------------------------
 * S-FRAME HANDLER
 * --------------------------------------------------------------------------
 */

static void handle_s_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  hdlc_u8 mode = (frame->control.s_frame.s);
  hdlc_u8 msg_nr = (frame->control.s_frame.nr);
  hdlc_u8 msg_pf = (frame->control.s_frame.pf);
  bool is_command = (frame->address == ctx->my_address);

  if (mode == HDLC_S_RR || mode == HDLC_S_RNR) {
      hdlc_process_nr(ctx, msg_nr);
  }
  else if (mode == HDLC_S_REJ) {
      if (ctx->va != ctx->vs) {
          hdlc_process_nr(ctx, msg_nr);

          hdlc_u8 seq = msg_nr;
          while (seq != ctx->vs) {
              hdlc_u8 slot = seq % ctx->window_size;
              hdlc_control_t ctrl = hdlc_create_i_ctrl(seq, ctx->vr, 0);
              hdlc_output_frame_start(ctx, ctx->peer_address, ctrl.value);
              if (ctx->retransmit_lens[slot] > 0 && ctx->retransmit_buffer != NULL) {
                  hdlc_output_frame_information_bytes(ctx,
                      ctx->retransmit_buffer + (slot * ctx->retransmit_slot_size),
                      ctx->retransmit_lens[slot]);
              }
              hdlc_output_frame_end(ctx);
              seq = (seq + 1) % HDLC_SEQUENCE_MODULUS;
          }

          ctx->retransmit_timer_ms = ctx->retransmit_timeout_ms;
      }
  }

  if (is_command && msg_pf) {
      hdlc_send_rr(ctx, 1);
  }
}

/*
 * --------------------------------------------------------------------------
 * U-FRAME HANDLER
 * --------------------------------------------------------------------------
 */

/* U-Frame sub-handlers */
static void hdlc_process_sabm(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    hdlc_set_protocol_state(ctx, HDLC_PROTOCOL_STATE_CONNECTED);
    hdlc_send_ua(ctx, frame->control.u_frame.pf);
}

static void hdlc_process_snrm(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    hdlc_send_dm(ctx, frame->control.u_frame.pf);
}

static void hdlc_process_sarm(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    hdlc_send_dm(ctx, frame->control.u_frame.pf);
}

static void hdlc_process_sabme(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    hdlc_send_dm(ctx, frame->control.u_frame.pf);
}

static void hdlc_process_snrme(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    hdlc_send_dm(ctx, frame->control.u_frame.pf);
}

static void hdlc_process_sarme(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    hdlc_send_dm(ctx, frame->control.u_frame.pf);
}

static void hdlc_process_disc(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    hdlc_set_protocol_state(ctx, HDLC_PROTOCOL_STATE_DISCONNECTED);
    hdlc_send_ua(ctx, frame->control.u_frame.pf);
}

static void hdlc_process_ua(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    (void)frame;
    if (ctx->current_state == HDLC_PROTOCOL_STATE_CONNECTING) {
        hdlc_set_protocol_state(ctx, HDLC_PROTOCOL_STATE_CONNECTED);
    } else if (ctx->current_state == HDLC_PROTOCOL_STATE_DISCONNECTING) {
        hdlc_set_protocol_state(ctx, HDLC_PROTOCOL_STATE_DISCONNECTED);
    }
}

static void hdlc_process_dm(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    (void)frame;
    hdlc_set_protocol_state(ctx, HDLC_PROTOCOL_STATE_DISCONNECTED);
}

static void hdlc_process_frmr(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
   if (frame->information_len >= HDLC_FRMR_INFO_MIN_LEN) {
       hdlc_frmr_data_t frmr_data;
       memset(&frmr_data, 0, sizeof(frmr_data));
       
       frmr_data.rejected_control = frame->information[0];
       
       hdlc_u8 byte1 = frame->information[1];
       frmr_data.v_s = (byte1 >> HDLC_FRMR_VS_SHIFT) & HDLC_FRMR_VS_MASK;
       frmr_data.cr  = (byte1 & HDLC_FRMR_CR_BIT) ? true : false;
       frmr_data.v_r = (byte1 >> HDLC_FRMR_VR_SHIFT) & HDLC_FRMR_VR_MASK;

       hdlc_u8 byte2 = frame->information[2];
       frmr_data.errors.w = (byte2 & HDLC_FRMR_W_BIT);
       frmr_data.errors.x = (byte2 & HDLC_FRMR_X_BIT);
       frmr_data.errors.y = (byte2 & HDLC_FRMR_Y_BIT);
       frmr_data.errors.z = (byte2 & HDLC_FRMR_Z_BIT);
       frmr_data.errors.v = (byte2 & HDLC_FRMR_V_BIT);
       
       (void)frmr_data;
   }

   hdlc_set_protocol_state(ctx, HDLC_PROTOCOL_STATE_DISCONNECTED);
}

static void hdlc_process_ui(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    (void)ctx;
    (void)frame;
}

static void hdlc_process_test(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    hdlc_output_frame_start(ctx, ctx->my_address,
        hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_TEST, HDLC_U_MODIFIER_HI_TEST,
                           frame->control.u_frame.pf).value);

    if (frame->information != NULL && frame->information_len > 0) {
        hdlc_output_frame_information_bytes(ctx, frame->information, frame->information_len);
    }

    hdlc_output_frame_end(ctx);
}

/* Main U-Frame dispatcher */
static void handle_u_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  hdlc_u8 m_lo = frame->control.u_frame.m_lo;
  hdlc_u8 m_hi = frame->control.u_frame.m_hi;

  /* 1. COMMANDS -> Addressed to ME or BROADCAST */
  if (frame->address == ctx->my_address || frame->address == HDLC_BROADCAST_ADDRESS) {
    
    if (m_lo == HDLC_U_MODIFIER_LO_UI && m_hi == HDLC_U_MODIFIER_HI_UI) {
         hdlc_process_ui(ctx, frame);
         return;
    }

    if (frame->address == HDLC_BROADCAST_ADDRESS) return;

    if (m_lo == HDLC_U_MODIFIER_LO_SABM && m_hi == HDLC_U_MODIFIER_HI_SABM) {
        hdlc_process_sabm(ctx, frame);
    }
    else if (m_lo == HDLC_U_MODIFIER_LO_DISC && m_hi == HDLC_U_MODIFIER_HI_DISC) {
        hdlc_process_disc(ctx, frame);
    }
    else if (m_lo == HDLC_U_MODIFIER_LO_SNRM && m_hi == HDLC_U_MODIFIER_HI_SNRM) {
        hdlc_process_snrm(ctx, frame);
    }
    else if (m_lo == HDLC_U_MODIFIER_LO_SARM && m_hi == HDLC_U_MODIFIER_HI_SARM) {
        hdlc_process_sarm(ctx, frame);
    }
    else if (m_lo == HDLC_U_MODIFIER_LO_SABME && m_hi == HDLC_U_MODIFIER_HI_SABME) {
       hdlc_process_sabme(ctx, frame);
    }
    else if (m_lo == HDLC_U_MODIFIER_LO_SNRME && m_hi == HDLC_U_MODIFIER_HI_SNRME) {
       hdlc_process_snrme(ctx, frame);
    }
    else if (m_lo == HDLC_U_MODIFIER_LO_SARME && m_hi == HDLC_U_MODIFIER_HI_SARME) {
       hdlc_process_sarme(ctx, frame);
    }
    else if (m_lo == HDLC_U_MODIFIER_LO_TEST && m_hi == HDLC_U_MODIFIER_HI_TEST) {
        hdlc_process_test(ctx, frame);
    }
  }

  /* 2. RESPONSES -> Addressed to PEER */
  else if (frame->address == ctx->peer_address) {
    
    if (m_lo == HDLC_U_MODIFIER_LO_UA && m_hi == HDLC_U_MODIFIER_HI_UA) {
        hdlc_process_ua(ctx, frame);
    }
    else if (m_lo == HDLC_U_MODIFIER_LO_DM && m_hi == HDLC_U_MODIFIER_HI_DM) {
        hdlc_process_dm(ctx, frame);
    }
    else if (m_lo == HDLC_U_MODIFIER_LO_FRMR && m_hi == HDLC_U_MODIFIER_HI_FRMR) {
        hdlc_process_frmr(ctx, frame);
    }
    else if (m_lo == HDLC_U_MODIFIER_LO_TEST && m_hi == HDLC_U_MODIFIER_HI_TEST) {
        /* TEST response — passed to on_frame_cb by dispatcher */
    }
  }
}

/*
 * --------------------------------------------------------------------------
 * SEQUENCE NUMBER HELPERS
 * --------------------------------------------------------------------------
 */

static inline bool hdlc_nr_valid(hdlc_u8 va, hdlc_u8 nr, hdlc_u8 vs) {
    if (va == vs) return false;
    hdlc_u8 diff_nr = (nr - va + HDLC_SEQUENCE_MODULUS) % HDLC_SEQUENCE_MODULUS;
    hdlc_u8 diff_vs = (vs - va + HDLC_SEQUENCE_MODULUS) % HDLC_SEQUENCE_MODULUS;
    return (diff_nr > 0 && diff_nr <= diff_vs);
}

static inline void hdlc_process_nr(hdlc_context_t *ctx, hdlc_u8 nr) {
    if (hdlc_nr_valid(ctx->va, nr, ctx->vs)) {
        ctx->va = nr;
        if (ctx->va == ctx->vs) {
            ctx->retransmit_timer_ms = 0;
        } else {
            ctx->retransmit_timer_ms = ctx->retransmit_timeout_ms;
        }
    }
}

/*
 * --------------------------------------------------------------------------
 * FRAME SEND HELPERS
 * --------------------------------------------------------------------------
 */

static inline void hdlc_send_u_frame(hdlc_context_t *ctx, hdlc_u8 address, hdlc_u8 m_lo, hdlc_u8 m_hi, hdlc_u8 pf) {
    hdlc_control_t ctrl = hdlc_create_u_ctrl(m_lo, m_hi, pf);
    hdlc_output_frame_start(ctx, address, ctrl.value);
    hdlc_output_frame_end(ctx);
}

static inline void hdlc_send_ua(hdlc_context_t *ctx, hdlc_u8 pf) {
    hdlc_send_u_frame(ctx, ctx->my_address, HDLC_U_MODIFIER_LO_UA, HDLC_U_MODIFIER_HI_UA, pf);
}

static inline void hdlc_send_dm(hdlc_context_t *ctx, hdlc_u8 pf) {
    hdlc_send_u_frame(ctx, ctx->my_address, HDLC_U_MODIFIER_LO_DM, HDLC_U_MODIFIER_HI_DM, pf);
}

static inline void hdlc_send_s_frame(hdlc_context_t *ctx, hdlc_u8 address, hdlc_u8 s_bits, hdlc_u8 nr, hdlc_u8 pf) {
    hdlc_control_t ctrl = hdlc_create_s_ctrl(s_bits, nr, pf);
    hdlc_output_frame_start(ctx, address, ctrl.value);
    hdlc_output_frame_end(ctx);
}

static inline void hdlc_send_rr(hdlc_context_t *ctx, hdlc_u8 pf) {
    hdlc_send_s_frame(ctx, ctx->peer_address, HDLC_S_RR, ctx->vr, pf);
}

static inline void hdlc_send_rej(hdlc_context_t *ctx, hdlc_u8 pf) {
    hdlc_send_s_frame(ctx, ctx->peer_address, HDLC_S_REJ, ctx->vr, pf);
}
