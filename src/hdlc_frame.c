/**
 * @file hdlc_frame.c
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief HDLC Frame Serialization (Pack / Unpack).
 */

#include "../inc/hdlc.h"
#include "hdlc_crc.h"
#include "hdlc_private.h"

/* --- Encoding Helpers --- */

void output_byte_to_callback(hdlc_encode_ctx_t *enc_ctx, atc_hdlc_u8 byte, atc_hdlc_bool flush) {
  if (enc_ctx->ctx && enc_ctx->ctx->output_byte_cb) {
    enc_ctx->ctx->output_byte_cb(byte, flush, enc_ctx->ctx->user_data);
  }
}

static void output_byte_to_buffer(hdlc_encode_ctx_t *enc_ctx, atc_hdlc_u8 byte, atc_hdlc_bool flush) {
  (void)flush;
  if (enc_ctx->current_len < enc_ctx->buffer_len) {
    enc_ctx->buffer[enc_ctx->current_len++] = byte;
  } else {
    enc_ctx->success = false;
  }
}

static inline void pack_byte(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn, atc_hdlc_u8 byte, atc_hdlc_bool flush) {
  put_fn(ctx, byte, flush);
}

static void pack_escaped(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn, atc_hdlc_u8 byte) {
  if (byte == HDLC_FLAG || byte == HDLC_ESCAPE) {
    pack_byte(ctx, put_fn, HDLC_ESCAPE, false);
    pack_byte(ctx, put_fn, byte ^ HDLC_XOR_MASK, false);
  } else {
    pack_byte(ctx, put_fn, byte, false);
  }
}

static void pack_escaped_crc_update(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn, atc_hdlc_u8 byte, atc_hdlc_u16 *crc) {
  *crc = atc_hdlc_crc_ccitt_update(*crc, byte);
  pack_escaped(ctx, put_fn, byte);
}

/* --- Core Serialization Engine --- */

atc_hdlc_bool frame_pack_core(const atc_hdlc_frame_t *frame, hdlc_put_byte_fn put_fn, hdlc_encode_ctx_t *enc_ctx) {
  atc_hdlc_u16 crc = ATC_HDLC_FCS_INIT_VALUE;

  pack_byte(enc_ctx, put_fn, HDLC_FLAG, false);
  if (!enc_ctx->success) return false;

  pack_escaped_crc_update(enc_ctx, put_fn, frame->address, &crc);
  if (!enc_ctx->success) return false;

  pack_escaped_crc_update(enc_ctx, put_fn, frame->control.value, &crc);
  if (!enc_ctx->success) return false;

  if (frame->information != NULL && frame->information_len > 0) {
    for (atc_hdlc_u16 i = 0; i < frame->information_len; i++) {
      pack_escaped_crc_update(enc_ctx, put_fn, frame->information[i], &crc);
      if (!enc_ctx->success) return false;
    }
  }

  atc_hdlc_u8 fcs_hi = (crc >> 8) & 0xFF;
  atc_hdlc_u8 fcs_lo = crc & 0xFF;

  pack_escaped(enc_ctx, put_fn, fcs_hi);
  if (!enc_ctx->success) return false;

  pack_escaped(enc_ctx, put_fn, fcs_lo);
  if (!enc_ctx->success) return false;

  pack_byte(enc_ctx, put_fn, HDLC_FLAG, true);
  return enc_ctx->success;
}

/* --- Public API --- */

atc_hdlc_bool atc_hdlc_frame_pack(const atc_hdlc_frame_t *frame, atc_hdlc_u8 *buffer, atc_hdlc_u32 buffer_len, atc_hdlc_u32 *encoded_len) {
  if (frame == NULL || buffer == NULL || encoded_len == NULL) return false;

  hdlc_encode_ctx_t enc_ctx = {.ctx = NULL, .buffer = buffer, .buffer_len = buffer_len, .current_len = 0, .success = true};

  if (frame_pack_core(frame, output_byte_to_buffer, &enc_ctx)) {
    *encoded_len = enc_ctx.current_len;
    return true;
  }

  *encoded_len = 0;
  return false;
}

atc_hdlc_bool atc_hdlc_frame_unpack(const atc_hdlc_u8 *buffer, atc_hdlc_u32 buffer_len, atc_hdlc_frame_t *frame, atc_hdlc_u8 *flat_buffer, atc_hdlc_u32 flat_buffer_len) {
  if (buffer == NULL || frame == NULL || flat_buffer == NULL) return false;

  if (buffer_len < ATC_HDLC_MIN_FRAME_LEN + 2 * ATC_HDLC_FLAG_LEN) {
      if (buffer_len < ATC_HDLC_MIN_FRAME_LEN) return false;
  }

  atc_hdlc_u32 write_idx = 0;
  atc_hdlc_bool inside_frame = false;
  atc_hdlc_bool escape_next = false;
  atc_hdlc_bool frame_complete = false;

  for (atc_hdlc_u32 i = 0; i < buffer_len; i++) {
    atc_hdlc_u8 byte = buffer[i];

    if (byte == HDLC_FLAG) {
      if (inside_frame) {
        if (write_idx >= ATC_HDLC_MIN_FRAME_LEN) { frame_complete = true; break; }
        else { write_idx = 0; continue; }
      } else {
        inside_frame = true; write_idx = 0; continue;
      }
    }

    if (!inside_frame) continue;

    if (byte == HDLC_ESCAPE) { escape_next = true; continue; }

    if (escape_next) { byte ^= HDLC_XOR_MASK; escape_next = false; }

    if (write_idx < flat_buffer_len) { flat_buffer[write_idx++] = byte; }
    else { return false; }
  }

  if (!frame_complete) {
     if (write_idx < ATC_HDLC_MIN_FRAME_LEN) return false;
  }

  atc_hdlc_u16 calced_crc = ATC_HDLC_FCS_INIT_VALUE;
  atc_hdlc_u32 data_len = write_idx - ATC_HDLC_FCS_LEN;

  for (atc_hdlc_u32 i = 0; i < data_len; i++) {
    calced_crc = atc_hdlc_crc_ccitt_update(calced_crc, flat_buffer[i]);
  }

  atc_hdlc_fcs_t *fcs = (atc_hdlc_fcs_t *)&flat_buffer[data_len];
  atc_hdlc_u16 rx_fcs = (fcs->fcs[0] << 8) | fcs->fcs[1];

  if (calced_crc != rx_fcs) return false;

  frame->address = flat_buffer[0];
  frame->control.value = flat_buffer[1];

  atc_hdlc_u32 header_len = ATC_HDLC_ADDRESS_LEN + ATC_HDLC_CONTROL_LEN;
  if (data_len > header_len) {
    frame->information = &flat_buffer[header_len];
    frame->information_len = (atc_hdlc_u16)(data_len - header_len);
  } else {
    frame->information = NULL;
    frame->information_len = 0;
  }

  if ((frame->control.value & ATC_HDLC_FRAME_TYPE_MASK_I) == ATC_HDLC_FRAME_TYPE_VAL_I) {
      frame->type = ATC_HDLC_FRAME_I;
  } else if ((frame->control.value & ATC_HDLC_FRAME_TYPE_MASK_S) == ATC_HDLC_FRAME_TYPE_VAL_S) {
      frame->type = ATC_HDLC_FRAME_S;
  } else if ((frame->control.value & ATC_HDLC_FRAME_TYPE_MASK_U) == ATC_HDLC_FRAME_TYPE_VAL_U) {
      frame->type = ATC_HDLC_FRAME_U;
  } else {
      frame->type = ATC_HDLC_FRAME_INVALID;
  }

  return true;
}

/* --- Control Field Constructors --- */

/**
 * @brief Create an I-Frame Control Field.
 * @see hdlc.h
 */
atc_hdlc_control_t atc_hdlc_create_i_ctrl(atc_hdlc_u8 ns, atc_hdlc_u8 nr, atc_hdlc_u8 pf) {
  atc_hdlc_control_t ctrl = {0};
  ctrl.i_frame.frame_type_0 = 0;
  ctrl.i_frame.ns = ns;
  ctrl.i_frame.pf = pf;
  ctrl.i_frame.nr = nr;
  return ctrl;
}

/**
 * @brief Create an S-Frame Control Field.
 * @see hdlc.h
 */
atc_hdlc_control_t atc_hdlc_create_s_ctrl(atc_hdlc_u8 s_bits, atc_hdlc_u8 nr, atc_hdlc_u8 pf) {
  atc_hdlc_control_t ctrl = {0};
  ctrl.s_frame.frame_type_0 = 1;
  ctrl.s_frame.frame_type_1 = 0;
  ctrl.s_frame.s = s_bits;
  ctrl.s_frame.pf = pf;
  ctrl.s_frame.nr = nr;
  return ctrl;
}

/**
 * @brief Create a U-Frame Control Field.
 * @see hdlc.h
 */
atc_hdlc_control_t atc_hdlc_create_u_ctrl(atc_hdlc_u8 m_lo, atc_hdlc_u8 m_hi, atc_hdlc_u8 pf) {
  atc_hdlc_control_t ctrl = {0};
  ctrl.u_frame.frame_type_0 = 1;
  ctrl.u_frame.frame_type_1 = 1;
  ctrl.u_frame.m_lo = m_lo;
  ctrl.u_frame.pf = pf;
  ctrl.u_frame.m_hi = m_hi;
  return ctrl;
}

/* --- Control Field Analyzers --- */

/**
 * @brief Get the S-Frame sub-type from a control field.
 */
atc_hdlc_s_frame_sub_type_t atc_hdlc_get_s_frame_sub_type(const atc_hdlc_control_t *control) {
    if (!control) return ATC_HDLC_S_FRAME_TYPE_UNKNOWN;

    if (control->s_frame.frame_type_0 == 1 && control->s_frame.frame_type_1 == 0) {
        switch (control->s_frame.s) {
            case HDLC_S_RR:  return ATC_HDLC_S_FRAME_TYPE_RR;
            case HDLC_S_RNR: return ATC_HDLC_S_FRAME_TYPE_RNR;
            case HDLC_S_REJ: return ATC_HDLC_S_FRAME_TYPE_REJ;
            default:         return ATC_HDLC_S_FRAME_TYPE_UNKNOWN;
        }
    }
    return ATC_HDLC_S_FRAME_TYPE_UNKNOWN;
}

/**
 * @brief Get the U-Frame sub-type from a control field.
 */
atc_hdlc_u_frame_sub_type_t atc_hdlc_get_u_frame_sub_type(const atc_hdlc_control_t *control) {
    if (!control) return ATC_HDLC_U_FRAME_TYPE_UNKNOWN;

    if (control->u_frame.frame_type_0 == 1 && control->u_frame.frame_type_1 == 1) {
        atc_hdlc_u8 m_hi = control->u_frame.m_hi;
        atc_hdlc_u8 m_lo = control->u_frame.m_lo;

        if (m_hi == HDLC_U_MODIFIER_HI_SABM && m_lo == HDLC_U_MODIFIER_LO_SABM) return ATC_HDLC_U_FRAME_TYPE_SABM;
        if (m_hi == HDLC_U_MODIFIER_HI_SNRM && m_lo == HDLC_U_MODIFIER_LO_SNRM) return ATC_HDLC_U_FRAME_TYPE_SNRM;
        if (m_hi == HDLC_U_MODIFIER_HI_SABME && m_lo == HDLC_U_MODIFIER_LO_SABME) return ATC_HDLC_U_FRAME_TYPE_SABME;
        if (m_hi == HDLC_U_MODIFIER_HI_SNRME && m_lo == HDLC_U_MODIFIER_LO_SNRME) return ATC_HDLC_U_FRAME_TYPE_SNRME;
        if (m_hi == HDLC_U_MODIFIER_HI_SARME && m_lo == HDLC_U_MODIFIER_LO_SARME) return ATC_HDLC_U_FRAME_TYPE_SARME;
        if (m_hi == HDLC_U_MODIFIER_HI_DISC && m_lo == HDLC_U_MODIFIER_LO_DISC) return ATC_HDLC_U_FRAME_TYPE_DISC;
        if (m_hi == HDLC_U_MODIFIER_HI_UA && m_lo == HDLC_U_MODIFIER_LO_UA) return ATC_HDLC_U_FRAME_TYPE_UA;
        if (m_hi == HDLC_U_MODIFIER_HI_FRMR && m_lo == HDLC_U_MODIFIER_LO_FRMR) return ATC_HDLC_U_FRAME_TYPE_FRMR;
        if (m_hi == HDLC_U_MODIFIER_HI_UI && m_lo == HDLC_U_MODIFIER_LO_UI) return ATC_HDLC_U_FRAME_TYPE_UI;
        if (m_hi == HDLC_U_MODIFIER_HI_TEST && m_lo == HDLC_U_MODIFIER_LO_TEST) return ATC_HDLC_U_FRAME_TYPE_TEST;

        if (m_hi == HDLC_U_MODIFIER_HI_DM && m_lo == HDLC_U_MODIFIER_LO_DM) {
            return ATC_HDLC_U_FRAME_TYPE_DM; // Also SARM, but DM is standard response
        }
    }
    return ATC_HDLC_U_FRAME_TYPE_UNKNOWN;
}
