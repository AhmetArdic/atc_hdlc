/**
 * @file hdlc_output.c
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief HDLC Frame Output (Streaming & Convenience API).
 *
 * Contains the streaming frame output API (start/data/end), convenience
 * wrappers for UI/TEST/I frames, control field constructors, and the
 * low-level output helpers (escaping, CRC update).
 */

#include "../inc/hdlc.h"
#include "hdlc_crc.h"
#include "hdlc_private.h"
#include <string.h>

/*
 * --------------------------------------------------------------------------
 * INTERNAL OUTPUT HELPERS
 * --------------------------------------------------------------------------
 */

/**
 * @brief Internal helper to send a single raw byte to hardware.
 * @param ctx  HDLC Context.
 * @param byte Raw byte to transmit.
 */
static inline void output_byte_raw(hdlc_context_t *ctx, hdlc_u8 byte, hdlc_bool flush) {
  if (ctx->output_byte_cb != NULL) {
    ctx->output_byte_cb(byte, flush, ctx->user_data);
  }
}

/**
 * @brief Internal helper to send a data byte with automatic escaping.
 *
 * @param ctx  HDLC Context.
 * @param byte Data byte to send.
 */
static void output_escaped(hdlc_context_t *ctx, hdlc_u8 byte) {
  if (byte == HDLC_FLAG || byte == HDLC_ESCAPE) {
    output_byte_raw(ctx, HDLC_ESCAPE, false);
    output_byte_raw(ctx, byte ^ HDLC_XOR_MASK, false);
  } else {
    output_byte_raw(ctx, byte, false);
  }
}

/**
 * @brief Internal helper to send a data byte with automatic escaping.
 *
 * Updates the running CRC and checks if the byte needs escaping
 * (i.e. if it looks like a FLAG or ESCAPE).
 *
 * @param ctx  HDLC Context.
 * @param byte Data byte to send.
 * @param crc  Pointer to the running CRC to update.
 */
static void output_escaped_crc_update(hdlc_context_t *ctx, hdlc_u8 byte, hdlc_u16 *crc) {
  *crc = hdlc_crc_ccitt_update(*crc, byte);

  output_escaped(ctx, byte);
}

/*
 * --------------------------------------------------------------------------
 * STREAMING OUTPUT API
 * --------------------------------------------------------------------------
 */

/**
 * @brief Output a complete HDLC Frame.
 * @see hdlc.h
 */
void hdlc_output_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  if (ctx == NULL || frame == NULL) {
    return;
  }

  hdlc_encode_ctx_t enc_ctx = {.ctx = ctx,
                               .buffer = NULL,
                               .buffer_len = 0,
                               .current_len = 0,
                               .success = true};

  frame_pack_core(frame, output_byte_to_callback, &enc_ctx);
  ctx->stats_output_frames++;
}

/**
 * @brief Start a Frame Transmission.
 * @see hdlc.h
 */
void hdlc_output_frame_start(hdlc_context_t *ctx, hdlc_u8 address, hdlc_u8 control) {
  if (ctx == NULL) {
    return;
  }

  // Initializing CRC
  ctx->output_crc = HDLC_FCS_INIT_VALUE;
  
  HDLC_LOG_DEBUG("tx: Frame start (Addr: 0x%02X, Ctrl: 0x%02X)", address, control);

  // Send Start Flag
  output_byte_raw(ctx, HDLC_FLAG, false);

  // Send Address
  // Update CRC and Send Escaped
  output_escaped_crc_update(ctx, address, &ctx->output_crc);

  // Send Control
  // Update CRC and Send Escaped
  output_escaped_crc_update(ctx, control, &ctx->output_crc);
}

/**
 * @brief Output a Information Byte.
 * @see hdlc.h
 */
void hdlc_output_frame_information_byte(hdlc_context_t *ctx, hdlc_u8 information_byte) {
  if (ctx == NULL) {
    return;
  }

  // Update CRC and Send Escaped
  output_escaped_crc_update(ctx, information_byte, &ctx->output_crc);
}

/**
 * @brief Output a Information Bytes Array.
 * @see hdlc.h
 */
void hdlc_output_frame_information_bytes(
    hdlc_context_t *ctx, const hdlc_u8 *information_bytes, hdlc_u32 len) {
  if (ctx == NULL || (information_bytes == NULL && len > 0)) {
    return;
  }

  for (hdlc_u32 i = 0; i < len; ++i) {
    // Update CRC and Send Escaped
    output_escaped_crc_update(ctx, information_bytes[i], &ctx->output_crc);
  }
}

/**
 * @brief Finalize Frame Output.
 * @see hdlc.h
 */
void hdlc_output_frame_end(hdlc_context_t *ctx) {
  if (ctx == NULL) {
    return;
  }

  // Finalize CRC
  hdlc_u16 crc = ctx->output_crc;

  hdlc_u8 fcs_hi = (crc >> 8) & 0xFF;
  hdlc_u8 fcs_lo = crc & 0xFF;

  // Send FCS High
  output_escaped(ctx, fcs_hi);

  // Send FCS Low
  output_escaped(ctx, fcs_lo);

  // End Flag
  output_byte_raw(ctx, HDLC_FLAG, true);

  ctx->stats_output_frames++;
}

/*
 * --------------------------------------------------------------------------
 * CONVENIENCE FRAME STARTERS
 * --------------------------------------------------------------------------
 */

/**
 * @brief Start a UI Frame Output.
 * @see hdlc.h
 */
void hdlc_output_frame_start_ui(hdlc_context_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  
  // UI Frame Control: 11 00 P 000 (Val=0x03 if P=0, 0x13 if P=1)
  // M_LO=0, M_HI=0
  hdlc_control_t ctrl = hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_UI, HDLC_U_MODIFIER_HI_UI, 0); // P=0 usually
  
  hdlc_output_frame_start(ctx, ctx->peer_address, ctrl.value);
}

/**
 * @brief Start a TEST Frame Output.
 * @see hdlc.h
 */
void hdlc_output_frame_start_test(hdlc_context_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  
  // TEST Frame: m_lo=0, m_hi=7, P=1
  hdlc_control_t ctrl = hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_TEST, HDLC_U_MODIFIER_HI_TEST, 1);
  
  hdlc_output_frame_start(ctx, ctx->peer_address, ctrl.value);
}

/**
 * @brief Start an Information (I) Frame Output (Streaming).
 * @see hdlc.h
 */
void hdlc_output_frame_start_i(hdlc_context_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  
  // I-Frame: N(S)=VS, N(R)=VR, P=0 (Default)
  hdlc_control_t ctrl = hdlc_create_i_ctrl(ctx->vs, ctx->vr, 0);
  
  hdlc_output_frame_start(ctx, ctx->peer_address, ctrl.value);
}

/*
 * --------------------------------------------------------------------------
 * CONVENIENCE FRAME WRAPPERS (Complete Output)
 * --------------------------------------------------------------------------
 */

bool hdlc_output_frame_ui(hdlc_context_t *ctx, const hdlc_u8 *data, hdlc_u32 len) {
  if (ctx == NULL) return false;

  // Start Frame
  hdlc_output_frame_start_ui(ctx);
  
  // Send Data
  if (data != NULL && len > 0) {
      hdlc_output_frame_information_bytes(ctx, data, len);
  }

  // End Frame
  hdlc_output_frame_end(ctx);
  return true;
}

bool hdlc_output_frame_test(hdlc_context_t *ctx, const hdlc_u8 *data, hdlc_u32 len) {
  if (ctx == NULL) return false;

  // Start Frame
  hdlc_output_frame_start_test(ctx);

  // Send Data
  if (data != NULL && len > 0) {
      hdlc_output_frame_information_bytes(ctx, data, len);
  }

  // End Frame
  hdlc_output_frame_end(ctx);
  return true;
}

/**
 * @brief Output an Information (I) frame (Reliable).
 * @see hdlc.h
 */
bool hdlc_output_frame_i(hdlc_context_t *ctx, const hdlc_u8 *data, hdlc_u32 len) {
  if (ctx == NULL) return false;

  // Window Check (Go-Back-N)
  hdlc_u8 outstanding = (ctx->vs - ctx->va + HDLC_SEQUENCE_MODULUS) % HDLC_SEQUENCE_MODULUS;
  if (outstanding >= ctx->window_size) {
      return false; // Window full
  }
  
  // Buffer for Retransmission using dynamic slot allocation
  if (ctx->retransmit_buffer != NULL && ctx->retransmit_slot_size > 0) {
      hdlc_u8 slot = ctx->next_tx_slot;
      ctx->tx_seq_to_slot[ctx->vs] = slot;
      ctx->next_tx_slot = (ctx->next_tx_slot + 1) % ctx->window_size;
      
      if (len > 0 && data != NULL) {
          if (len > ctx->retransmit_slot_size) {
              return false; // Data too large for retransmit slot.
          }
          memcpy(ctx->retransmit_buffer + (slot * ctx->retransmit_slot_size), data, len);
      }
      ctx->retransmit_lens[slot] = len;
  }

  // Start Frame
  HDLC_LOG_DEBUG("tx: I-Frame V(S)=%u, Len=%lu", ctx->vs, (unsigned long)len);
  hdlc_output_frame_start_i(ctx);
  
  // Send Data
  if (data != NULL && len > 0) {
      hdlc_output_frame_information_bytes(ctx, data, len);
  }

  // End Frame
  hdlc_output_frame_end(ctx);
  
  // Update State
  ctx->vs = (ctx->vs + 1) % HDLC_SEQUENCE_MODULUS;
  ctx->ack_pending = false; // Piggybacked ACK sent via N(R) in I-frame
  
  // Start Timer (only if this is the first outstanding frame)
  if (outstanding == 0) {
      ctx->retransmit_timer_ms = ctx->retransmit_timeout_ms;
  }
  
  return true;
}
