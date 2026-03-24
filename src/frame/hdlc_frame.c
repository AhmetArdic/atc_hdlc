/*
 * Copyright (C) 2026 Ahmet Talha ARDIC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../../inc/hdlc.h"
#include "hdlc_crc.h"
#include "../hdlc_private.h"

void hdlc_write_byte(hdlc_encode_ctx_t *enc_ctx, atc_hdlc_u8 byte, atc_hdlc_bool flush) {
  if (enc_ctx->ctx && enc_ctx->ctx->platform && enc_ctx->ctx->platform->on_send) {
    enc_ctx->ctx->platform->on_send(byte, flush, enc_ctx->ctx->platform->user_ctx);
  }
}

static void hdlc_write_byte_to_buf(hdlc_encode_ctx_t *enc_ctx, atc_hdlc_u8 byte, atc_hdlc_bool flush) {
  (void)flush;
  if (enc_ctx->current_len < enc_ctx->buffer_len) {
    enc_ctx->buffer[enc_ctx->current_len++] = byte;
  } else {
    enc_ctx->success = false;
  }
}

static inline void hdlc_put_byte(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn,
                                  atc_hdlc_u8 byte, atc_hdlc_bool flush) {
  put_fn(ctx, byte, flush);
}

void hdlc_pack_escaped(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn, atc_hdlc_u8 byte) {
  if (byte == HDLC_FLAG || byte == HDLC_ESCAPE) {
    hdlc_put_byte(ctx, put_fn, HDLC_ESCAPE, false);
    hdlc_put_byte(ctx, put_fn, byte ^ HDLC_XOR_MASK, false);
  } else {
    hdlc_put_byte(ctx, put_fn, byte, false);
  }
}

void hdlc_pack_escaped_crc(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn,
                             atc_hdlc_u8 byte, atc_hdlc_u16 *crc) {
  *crc = atc_hdlc_crc_ccitt_update(*crc, byte);
  hdlc_pack_escaped(ctx, put_fn, byte);
}

atc_hdlc_bool hdlc_frame_pack_core(const atc_hdlc_frame_t *frame, hdlc_put_byte_fn put_fn,
                                     hdlc_encode_ctx_t *enc_ctx) {
  atc_hdlc_u16 crc = ATC_HDLC_FCS_INIT_VALUE;

  hdlc_put_byte(enc_ctx, put_fn, HDLC_FLAG, false);
  if (!enc_ctx->success) return false;

  hdlc_pack_escaped_crc(enc_ctx, put_fn, frame->address, &crc);
  if (!enc_ctx->success) return false;

  hdlc_pack_escaped_crc(enc_ctx, put_fn, frame->control, &crc);
  if (!enc_ctx->success) return false;

  if (frame->information != NULL && frame->information_len > 0) {
    for (atc_hdlc_u16 i = 0; i < frame->information_len; i++) {
      hdlc_pack_escaped_crc(enc_ctx, put_fn, frame->information[i], &crc);
      if (!enc_ctx->success) return false;
    }
  }

  atc_hdlc_u8 fcs_hi = (atc_hdlc_u8)((crc >> 8) & 0xFF);
  atc_hdlc_u8 fcs_lo = (atc_hdlc_u8)(crc & 0xFF);

  hdlc_pack_escaped(enc_ctx, put_fn, fcs_hi);
  if (!enc_ctx->success) return false;

  hdlc_pack_escaped(enc_ctx, put_fn, fcs_lo);
  if (!enc_ctx->success) return false;

  hdlc_put_byte(enc_ctx, put_fn, HDLC_FLAG, true);
  return enc_ctx->success;
}

atc_hdlc_bool atc_hdlc_frame_pack(const atc_hdlc_frame_t *frame, atc_hdlc_u8 *buffer,
                                    atc_hdlc_u32 buffer_len, atc_hdlc_u32 *encoded_len) {
  if (frame == NULL || buffer == NULL || encoded_len == NULL) return false;

  hdlc_encode_ctx_t enc_ctx = {.ctx = NULL, .buffer = buffer, .buffer_len = buffer_len,
                                .current_len = 0, .success = true};

  if (hdlc_frame_pack_core(frame, hdlc_write_byte_to_buf, &enc_ctx)) {
    *encoded_len = enc_ctx.current_len;
    return true;
  }

  *encoded_len = 0;
  return false;
}

atc_hdlc_bool atc_hdlc_frame_unpack(const atc_hdlc_u8 *buffer, atc_hdlc_u32 buffer_len,
                                      atc_hdlc_frame_t *frame, atc_hdlc_u8 *flat_buffer,
                                      atc_hdlc_u32 flat_buffer_len) {
  if (buffer == NULL || frame == NULL || flat_buffer == NULL) return false;

  if (buffer_len < HDLC_MIN_FRAME_LEN + 2 * HDLC_FLAG_LEN) {
      if (buffer_len < HDLC_MIN_FRAME_LEN) return false;
  }

  atc_hdlc_u32 write_idx = 0;
  atc_hdlc_bool inside_frame = false;
  atc_hdlc_bool escape_next  = false;
  atc_hdlc_bool frame_complete = false;

  for (atc_hdlc_u32 i = 0; i < buffer_len; i++) {
    atc_hdlc_u8 byte = buffer[i];

    if (byte == HDLC_FLAG) {
      if (inside_frame) {
        if (write_idx >= HDLC_MIN_FRAME_LEN) { frame_complete = true; break; }
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
     if (write_idx < HDLC_MIN_FRAME_LEN) return false;
  }

  atc_hdlc_u16 calced_crc = ATC_HDLC_FCS_INIT_VALUE;
  atc_hdlc_u32 data_len = write_idx - HDLC_FCS_LEN;

  for (atc_hdlc_u32 i = 0; i < data_len; i++) {
    calced_crc = atc_hdlc_crc_ccitt_update(calced_crc, flat_buffer[i]);
  }

  atc_hdlc_u16 rx_fcs = ((atc_hdlc_u16)flat_buffer[data_len] << 8) | flat_buffer[data_len + 1];

  if (calced_crc != rx_fcs) return false;

  frame->address = flat_buffer[0];
  frame->control = flat_buffer[1];

  atc_hdlc_u32 header_len = HDLC_ADDRESS_LEN + HDLC_CONTROL_LEN;
  if (data_len > header_len) {
    frame->information = &flat_buffer[header_len];
    frame->information_len = (atc_hdlc_u16)(data_len - header_len);
  } else {
    frame->information = NULL;
    frame->information_len = 0;
  }

  return true;
}


atc_hdlc_s_frame_sub_type_t atc_hdlc_get_s_frame_sub_type(atc_hdlc_u8 control) {
    if (hdlc_resolve_frame_type(control) == ATC_HDLC_FRAME_S) {
        switch (HDLC_CTRL_S_BITS(control)) {
            case HDLC_S_RR:  return ATC_HDLC_S_FRAME_TYPE_RR;
            case HDLC_S_RNR: return ATC_HDLC_S_FRAME_TYPE_RNR;
            case HDLC_S_REJ: return ATC_HDLC_S_FRAME_TYPE_REJ;
            default:         return ATC_HDLC_S_FRAME_TYPE_UNKNOWN;
        }
    }
    return ATC_HDLC_S_FRAME_TYPE_UNKNOWN;
}

atc_hdlc_u_frame_sub_type_t atc_hdlc_get_u_frame_sub_type(atc_hdlc_u8 control) {
    if (!hdlc_is_u_frame(control))
        return ATC_HDLC_U_FRAME_TYPE_UNKNOWN;
    switch (control & ~HDLC_PF_BIT) {
    case HDLC_U_SABM:  return ATC_HDLC_U_FRAME_TYPE_SABM;
    case HDLC_U_DISC:  return ATC_HDLC_U_FRAME_TYPE_DISC;
    case HDLC_U_UA:    return ATC_HDLC_U_FRAME_TYPE_UA;
    case HDLC_U_DM:    return ATC_HDLC_U_FRAME_TYPE_DM;
    case HDLC_U_FRMR:  return ATC_HDLC_U_FRAME_TYPE_FRMR;
    case HDLC_U_UI:    return ATC_HDLC_U_FRAME_TYPE_UI;
    case HDLC_U_TEST:  return ATC_HDLC_U_FRAME_TYPE_TEST;
    case HDLC_U_SNRM:  return ATC_HDLC_U_FRAME_TYPE_SNRM;
    case HDLC_U_SABME: return ATC_HDLC_U_FRAME_TYPE_SABME;
    case HDLC_U_SNRME: return ATC_HDLC_U_FRAME_TYPE_SNRME;
    case HDLC_U_SARME: return ATC_HDLC_U_FRAME_TYPE_SARME;
    default:           return ATC_HDLC_U_FRAME_TYPE_UNKNOWN;
    }
}
