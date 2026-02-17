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

void output_byte_to_callback(hdlc_encode_ctx_t *enc_ctx, hdlc_u8 byte, hdlc_bool flush) {
  if (enc_ctx->ctx && enc_ctx->ctx->output_byte_cb) {
    enc_ctx->ctx->output_byte_cb(byte, flush, enc_ctx->ctx->user_data);
  }
}

static void output_byte_to_buffer(hdlc_encode_ctx_t *enc_ctx, hdlc_u8 byte, hdlc_bool flush) {
  (void)flush;
  if (enc_ctx->current_len < enc_ctx->buffer_len) {
    enc_ctx->buffer[enc_ctx->current_len++] = byte;
  } else {
    enc_ctx->success = false;
  }
}

static inline void pack_byte(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn, hdlc_u8 byte, hdlc_bool flush) {
  put_fn(ctx, byte, flush);
}

static void pack_escaped(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn, hdlc_u8 byte) {
  if (byte == HDLC_FLAG || byte == HDLC_ESCAPE) {
    pack_byte(ctx, put_fn, HDLC_ESCAPE, false);
    pack_byte(ctx, put_fn, byte ^ HDLC_XOR_MASK, false);
  } else {
    pack_byte(ctx, put_fn, byte, false);
  }
}

static void pack_escaped_crc_update(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn, hdlc_u8 byte, hdlc_u16 *crc) {
  *crc = hdlc_crc_ccitt_update(*crc, byte);
  pack_escaped(ctx, put_fn, byte);
}

/* --- Core Serialization Engine --- */

hdlc_bool frame_pack_core(const hdlc_frame_t *frame, hdlc_put_byte_fn put_fn, hdlc_encode_ctx_t *enc_ctx) {
  hdlc_u16 crc = HDLC_FCS_INIT_VALUE;

  pack_byte(enc_ctx, put_fn, HDLC_FLAG, false);
  if (!enc_ctx->success) return false;

  pack_escaped_crc_update(enc_ctx, put_fn, frame->address, &crc);
  if (!enc_ctx->success) return false;

  pack_escaped_crc_update(enc_ctx, put_fn, frame->control.value, &crc);
  if (!enc_ctx->success) return false;

  if (frame->information != NULL && frame->information_len > 0) {
    for (hdlc_u16 i = 0; i < frame->information_len; i++) {
      pack_escaped_crc_update(enc_ctx, put_fn, frame->information[i], &crc);
      if (!enc_ctx->success) return false;
    }
  }

  hdlc_u8 fcs_hi = (crc >> 8) & 0xFF;
  hdlc_u8 fcs_lo = crc & 0xFF;

  pack_escaped(enc_ctx, put_fn, fcs_hi);
  if (!enc_ctx->success) return false;

  pack_escaped(enc_ctx, put_fn, fcs_lo);
  if (!enc_ctx->success) return false;

  pack_byte(enc_ctx, put_fn, HDLC_FLAG, true);
  return enc_ctx->success;
}

/* --- Public API --- */

bool hdlc_frame_pack(const hdlc_frame_t *frame, hdlc_u8 *buffer, hdlc_u32 buffer_len, hdlc_u32 *encoded_len) {
  if (frame == NULL || buffer == NULL || encoded_len == NULL) return false;

  hdlc_encode_ctx_t enc_ctx = {.ctx = NULL, .buffer = buffer, .buffer_len = buffer_len, .current_len = 0, .success = true};

  if (frame_pack_core(frame, output_byte_to_buffer, &enc_ctx)) {
    *encoded_len = enc_ctx.current_len;
    return true;
  }

  *encoded_len = 0;
  return false;
}

bool hdlc_frame_unpack(const hdlc_u8 *buffer, hdlc_u32 buffer_len, hdlc_frame_t *frame, hdlc_u8 *flat_buffer, hdlc_u32 flat_buffer_len) {
  if (buffer == NULL || frame == NULL || flat_buffer == NULL) return false;

  if (buffer_len < HDLC_MIN_FRAME_LEN + 2 * HDLC_FLAG_LEN) {
      if (buffer_len < HDLC_MIN_FRAME_LEN) return false;
  }

  hdlc_u32 write_idx = 0;
  hdlc_bool inside_frame = false;
  hdlc_bool escape_next = false;
  hdlc_bool frame_complete = false;

  for (hdlc_u32 i = 0; i < buffer_len; i++) {
    hdlc_u8 byte = buffer[i];

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

  hdlc_u16 calced_crc = HDLC_FCS_INIT_VALUE;
  hdlc_u32 data_len = write_idx - HDLC_FCS_LEN;

  for (hdlc_u32 i = 0; i < data_len; i++) {
    calced_crc = hdlc_crc_ccitt_update(calced_crc, flat_buffer[i]);
  }

  hdlc_fcs_t *fcs = (hdlc_fcs_t *)&flat_buffer[data_len];
  hdlc_u16 rx_fcs = (fcs->fcs[0] << 8) | fcs->fcs[1];

  if (calced_crc != rx_fcs) return false;

  frame->address = flat_buffer[0];
  frame->control.value = flat_buffer[1];

  hdlc_u32 header_len = HDLC_ADDRESS_LEN + HDLC_CONTROL_LEN;
  if (data_len > header_len) {
    frame->information = &flat_buffer[header_len];
    frame->information_len = (hdlc_u16)(data_len - header_len);
  } else {
    frame->information = NULL;
    frame->information_len = 0;
  }

  if ((frame->control.value & HDLC_FRAME_TYPE_MASK_I) == HDLC_FRAME_TYPE_VAL_I) {
      frame->type = HDLC_FRAME_I;
  } else if ((frame->control.value & HDLC_FRAME_TYPE_MASK_S) == HDLC_FRAME_TYPE_VAL_S) {
      frame->type = HDLC_FRAME_S;
  } else if ((frame->control.value & HDLC_FRAME_TYPE_MASK_U) == HDLC_FRAME_TYPE_VAL_U) {
      frame->type = HDLC_FRAME_U;
  } else {
      frame->type = HDLC_FRAME_INVALID;
  }

  return true;
}
