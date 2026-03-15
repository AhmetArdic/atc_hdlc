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
 * Contains the streaming frame output API (start/data/end) and convenience
 * wrappers for UI/TEST/I frames.
 */

#include "../../inc/hdlc.h"
#include "../frame/hdlc_crc.h"
#include "../hdlc_private.h"
#include <string.h>

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

  /* Stats: This path uses frame_pack_core which does NOT call
     atc_hdlc_output_frame_end(), so we increment here. */
  ctx->stats.tx_i_frames++;
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

  hdlc_encode_ctx_t enc = {.ctx = ctx, .success = true};
  
  ATC_HDLC_LOG_DEBUG("tx: Frame start (Addr: 0x%02X, Ctrl: 0x%02X)", address, control);

  // Send Start Flag (raw, no escaping)
  output_byte_to_callback(&enc, HDLC_FLAG, false);

  // Send Address & Control (escaped + CRC update)
  pack_escaped_crc_update(&enc, output_byte_to_callback, address, &ctx->output_crc);
  pack_escaped_crc_update(&enc, output_byte_to_callback, control, &ctx->output_crc);
}

/**
 * @brief Output a Information Byte.
 * @see hdlc.h
 */
void atc_hdlc_output_frame_information_byte(atc_hdlc_context_t *ctx, atc_hdlc_u8 information_byte) {
  if (ctx == NULL) {
    return;
  }

  hdlc_encode_ctx_t enc = {.ctx = ctx, .success = true};
  pack_escaped_crc_update(&enc, output_byte_to_callback, information_byte, &ctx->output_crc);
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

  hdlc_encode_ctx_t enc = {.ctx = ctx, .success = true};
  for (atc_hdlc_u32 i = 0; i < len; ++i) {
    pack_escaped_crc_update(&enc, output_byte_to_callback, information_bytes[i], &ctx->output_crc);
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

  hdlc_encode_ctx_t enc = {.ctx = ctx, .success = true};

  // Finalize CRC
  atc_hdlc_u16 crc = ctx->output_crc;

  atc_hdlc_u8 fcs_hi = (crc >> 8) & 0xFF;
  atc_hdlc_u8 fcs_lo = crc & 0xFF;

  // Send FCS (escaped, no CRC update)
  pack_escaped(&enc, output_byte_to_callback, fcs_hi);
  pack_escaped(&enc, output_byte_to_callback, fcs_lo);

  // End Flag (raw)
  output_byte_to_callback(&enc, HDLC_FLAG, true);

  /* Stats: This path is for streaming API (start/data/end).
     atc_hdlc_output_frame() has its own separate increment. */
  ctx->stats.tx_i_frames++;
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
void atc_hdlc_output_frame_start_ui(atc_hdlc_context_t *ctx, atc_hdlc_u8 address) {
  if (ctx == NULL) {
    return;
  }
  
  // UI Frame Control: 11 00 P 000 (Val=0x03 if P=0, 0x13 if P=1)
  // M_LO=0, M_HI=0
  atc_hdlc_u8 ctrl = atc_hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_UI, HDLC_U_MODIFIER_HI_UI, 0); // P=0 usually
  
  atc_hdlc_output_frame_start(ctx, address, ctrl);
}

/**
 * @brief Start a TEST Frame Output.
 * @see hdlc.h
 */
void atc_hdlc_output_frame_start_test(atc_hdlc_context_t *ctx, atc_hdlc_u8 address) {
  if (ctx == NULL) {
    return;
  }
  
  // TEST Frame: m_lo=0, m_hi=7, P=1
  atc_hdlc_u8 ctrl = atc_hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_TEST, HDLC_U_MODIFIER_HI_TEST, 1);
  
  atc_hdlc_output_frame_start(ctx, address, ctrl);
}



/*
 * --------------------------------------------------------------------------
 * CONVENIENCE FRAME WRAPPERS (Complete Output)
 * --------------------------------------------------------------------------
 */

/**
 * @brief Transmit an Unnumbered Information (UI) frame.
 * @see hdlc.h
 */
atc_hdlc_error_t atc_hdlc_output_frame_ui(atc_hdlc_context_t *ctx,
                                            atc_hdlc_u8         address,
                                            const atc_hdlc_u8  *data,
                                            atc_hdlc_u32        len) {
    if (ctx == NULL) return ATC_HDLC_ERR_INVALID_PARAM;
    if (ctx->config && len > ctx->config->max_frame_size) {
        return ATC_HDLC_ERR_FRAME_TOO_LARGE;
    }

    atc_hdlc_output_frame_start_ui(ctx, address);
    if (data != NULL && len > 0) {
        atc_hdlc_output_frame_information_bytes(ctx, data, len);
    }
    atc_hdlc_output_frame_end(ctx);
    return ATC_HDLC_OK;
}

/**
 * @brief Transmit a TEST frame and await the echo.
 * @see hdlc.h
 */
atc_hdlc_error_t atc_hdlc_output_frame_test(atc_hdlc_context_t *ctx,
                                              atc_hdlc_u8         address,
                                              const atc_hdlc_u8  *data,
                                              atc_hdlc_u32        len) {
    if (ctx == NULL) return ATC_HDLC_ERR_INVALID_PARAM;
    if (ctx->test_pending) return ATC_HDLC_ERR_TEST_PENDING;
    if (ctx->config && len > ctx->config->max_frame_size) {
        return ATC_HDLC_ERR_FRAME_TOO_LARGE;
    }

    /* Store pattern for later comparison */
    ctx->test_pattern     = data;
    ctx->test_pattern_len = (atc_hdlc_u16)len;
    ctx->test_pending     = true;
    ctx->stats.test_sent++;

    atc_hdlc_output_frame_start_test(ctx, address);
    if (data != NULL && len > 0) {
        atc_hdlc_output_frame_information_bytes(ctx, data, len);
    }
    atc_hdlc_output_frame_end(ctx);

    /* Start T1 to detect timeout */
    if (ctx->config) {
        ctx->retransmit_timer = ctx->config->t1_ms;
    }

    return ATC_HDLC_OK;
}

/**
 * @brief Transmit a reliable Information (I) frame.
 * @see hdlc.h
 */
atc_hdlc_error_t atc_hdlc_output_frame_i(atc_hdlc_context_t *ctx,
                                           const atc_hdlc_u8  *data,
                                           atc_hdlc_u32        len) {
    if (ctx == NULL) return ATC_HDLC_ERR_INVALID_PARAM;
    if (ctx->current_state != ATC_HDLC_STATE_CONNECTED) {
        return ATC_HDLC_ERR_INVALID_STATE;
    }
    if (ctx->remote_busy) return ATC_HDLC_ERR_REMOTE_BUSY;
    if (ctx->tx_window == NULL) return ATC_HDLC_ERR_NO_BUFFER;
    if (ctx->config && len > ctx->config->max_frame_size) {
        return ATC_HDLC_ERR_FRAME_TOO_LARGE;
    }

    /* Window check */
    atc_hdlc_u8 outstanding = (atc_hdlc_u8)((ctx->vs - ctx->va +
                               HDLC_SEQUENCE_MODULUS) % HDLC_SEQUENCE_MODULUS);
    if (outstanding >= ctx->window_size) {
        return ATC_HDLC_ERR_WINDOW_FULL;
    }

    /* Copy payload into retransmit slot */
    atc_hdlc_u8 slot = ctx->next_tx_slot;
    ctx->tx_window->seq_to_slot[ctx->vs] = slot;
    ctx->next_tx_slot = (atc_hdlc_u8)((ctx->next_tx_slot + 1) % ctx->window_size);

    if (len > 0 && data != NULL) {
        if (len > ctx->tx_window->slot_capacity) {
            return ATC_HDLC_ERR_FRAME_TOO_LARGE;
        }
        memcpy(ctx->tx_window->slots + (slot * ctx->tx_window->slot_capacity),
               data, len);
    }
    ctx->tx_window->slot_lens[slot] = len;

    /* Build and send frame */
    ATC_HDLC_LOG_DEBUG("tx: I-Frame V(S)=%u, Len=%lu", ctx->vs, (unsigned long)len);
    atc_hdlc_u8 ctrl = atc_hdlc_create_i_ctrl(ctx->vs, ctx->vr, 0);
    atc_hdlc_output_frame_start(ctx, ctx->peer_address, ctrl);
    if (data != NULL && len > 0) {
        atc_hdlc_output_frame_information_bytes(ctx, data, len);
    }
    atc_hdlc_output_frame_end(ctx);

    /* Advance V(S), cancel T2 (ACK piggybacked in N(R)) */
    ctx->vs = (atc_hdlc_u8)((ctx->vs + 1) % HDLC_SEQUENCE_MODULUS);
    ctx->ack_timer = 0;

    /* Start T1 only for the first outstanding frame */
    if (outstanding == 0) {
        ctx->retransmit_timer = ctx->config->t1_ms;
    }

    ctx->stats.tx_i_frames++;
    ctx->stats.tx_bytes += len;

    return ATC_HDLC_OK;
}
