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
static inline void output_byte_raw(atc_hdlc_context_t *ctx, atc_hdlc_u8 byte, atc_hdlc_bool flush) {
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
static void output_escaped(atc_hdlc_context_t *ctx, atc_hdlc_u8 byte) {
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
static void output_escaped_crc_update(atc_hdlc_context_t *ctx, atc_hdlc_u8 byte, atc_hdlc_u16 *crc) {
  *crc = atc_hdlc_crc_ccitt_update(*crc, byte);

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
void atc_hdlc_output_frame(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
  if (ctx == NULL || frame == NULL) {
    return;
  }

  hdlc_encode_ctx_t enc_ctx = {.ctx = ctx,
                               .buffer = NULL,
                               .buffer_len = 0,
                               .current_len = 0,
                               .success = true};

  (void)frame_pack_core(frame, output_byte_to_callback, &enc_ctx);
  ctx->stats_output_frames++;
}

/**
 * @brief Start a Frame Transmission.
 * @see hdlc.h
 */
void atc_hdlc_output_frame_start(atc_hdlc_context_t *ctx, atc_hdlc_u8 address, atc_hdlc_u8 control) {
  if (ctx == NULL) {
    return;
  }

  // Initializing CRC
  ctx->output_crc = ATC_HDLC_FCS_INIT_VALUE;
  
  ATC_HDLC_LOG_DEBUG("tx: Frame start (Addr: 0x%02X, Ctrl: 0x%02X)", address, control);

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
void atc_hdlc_output_frame_information_byte(atc_hdlc_context_t *ctx, atc_hdlc_u8 information_byte) {
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
void atc_hdlc_output_frame_information_bytes(
    atc_hdlc_context_t *ctx, const atc_hdlc_u8 *information_bytes, atc_hdlc_u32 len) {
  if (ctx == NULL || (information_bytes == NULL && len > 0)) {
    return;
  }

  for (atc_hdlc_u32 i = 0; i < len; ++i) {
    // Update CRC and Send Escaped
    output_escaped_crc_update(ctx, information_bytes[i], &ctx->output_crc);
  }
}

/**
 * @brief Finalize Frame Output.
 * @see hdlc.h
 */
void atc_hdlc_output_frame_end(atc_hdlc_context_t *ctx) {
  if (ctx == NULL) {
    return;
  }

  // Finalize CRC
  atc_hdlc_u16 crc = ctx->output_crc;

  atc_hdlc_u8 fcs_hi = (crc >> 8) & 0xFF;
  atc_hdlc_u8 fcs_lo = crc & 0xFF;

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
void atc_hdlc_output_frame_start_ui(atc_hdlc_context_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  
  // UI Frame Control: 11 00 P 000 (Val=0x03 if P=0, 0x13 if P=1)
  // M_LO=0, M_HI=0
  atc_hdlc_control_t ctrl = atc_hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_UI, HDLC_U_MODIFIER_HI_UI, 0); // P=0 usually
  
  atc_hdlc_output_frame_start(ctx, ctx->peer_address, ctrl.value);
}

/**
 * @brief Start a TEST Frame Output.
 * @see hdlc.h
 */
void atc_hdlc_output_frame_start_test(atc_hdlc_context_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  
  // TEST Frame: m_lo=0, m_hi=7, P=1
  atc_hdlc_control_t ctrl = atc_hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_TEST, HDLC_U_MODIFIER_HI_TEST, 1);
  
  atc_hdlc_output_frame_start(ctx, ctx->peer_address, ctrl.value);
}

/**
 * @brief Start an Information (I) Frame Output (Streaming).
 * @see hdlc.h
 */
void atc_hdlc_output_frame_start_i(atc_hdlc_context_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  
  // I-Frame: N(S)=VS, N(R)=VR, P=0 (Default)
  atc_hdlc_control_t ctrl = atc_hdlc_create_i_ctrl(ctx->vs, ctx->vr, 0);
  
  atc_hdlc_output_frame_start(ctx, ctx->peer_address, ctrl.value);
}

/*
 * --------------------------------------------------------------------------
 * CONVENIENCE FRAME WRAPPERS (Complete Output)
 * --------------------------------------------------------------------------
 */

atc_hdlc_bool atc_hdlc_output_frame_ui(atc_hdlc_context_t *ctx, const atc_hdlc_u8 *data, atc_hdlc_u32 len) {
  if (ctx == NULL) return false;

  // Start Frame
  atc_hdlc_output_frame_start_ui(ctx);
  
  // Send Data
  if (data != NULL && len > 0) {
      atc_hdlc_output_frame_information_bytes(ctx, data, len);
  }

  // End Frame
  atc_hdlc_output_frame_end(ctx);
  return true;
}

atc_hdlc_bool atc_hdlc_output_frame_test(atc_hdlc_context_t *ctx, const atc_hdlc_u8 *data, atc_hdlc_u32 len) {
  if (ctx == NULL) return false;

  // Start Frame
  atc_hdlc_output_frame_start_test(ctx);

  // Send Data
  if (data != NULL && len > 0) {
      atc_hdlc_output_frame_information_bytes(ctx, data, len);
  }

  // End Frame
  atc_hdlc_output_frame_end(ctx);
  return true;
}

/**
 * @brief Output an Information (I) frame (Reliable).
 * @see hdlc.h
 */
atc_hdlc_bool atc_hdlc_output_frame_i(atc_hdlc_context_t *ctx, const atc_hdlc_u8 *data, atc_hdlc_u32 len) {
  if (ctx == NULL) return false;

  // Window Check (Go-Back-N)
  atc_hdlc_u8 outstanding = (ctx->vs - ctx->va + HDLC_SEQUENCE_MODULUS) % HDLC_SEQUENCE_MODULUS;
  if (outstanding >= ctx->window_size) {
      return false; // Window full
  }
  
  // Buffer for Retransmission using dynamic slot allocation
  if (ctx->retransmit_buffer != NULL && ctx->retransmit_slot_size > 0) {
      atc_hdlc_u8 slot = ctx->next_tx_slot;
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
  ATC_HDLC_LOG_DEBUG("tx: I-Frame V(S)=%u, Len=%lu", ctx->vs, (unsigned long)len);
  atc_hdlc_output_frame_start_i(ctx);
  
  // Send Data
  if (data != NULL && len > 0) {
      atc_hdlc_output_frame_information_bytes(ctx, data, len);
  }

  // End Frame
  atc_hdlc_output_frame_end(ctx);
  
  // Update State
  ctx->vs = (ctx->vs + 1) % HDLC_SEQUENCE_MODULUS;
  ctx->ack_timer = 0; // Piggybacked ACK sent via N(R) in I-frame
  
  // Start Timer (only if this is the first outstanding frame)
  if (outstanding == 0) {
      ctx->retransmit_timer = ctx->retransmit_timeout;
  }
  
  return true;
}
