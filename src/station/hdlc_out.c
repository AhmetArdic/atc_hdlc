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
 * @file hdlc_out.c
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief HDLC TX path — streaming transmit API and convenience wrappers.
 *
 * Contains the streaming frame transmit API (start/data/end) and convenience
 * wrappers for UI/TEST/I frames.
 */

#include "../../inc/hdlc.h"
#include "../frame/hdlc_crc.h"
#include "../hdlc_private.h"
#include <string.h>

/*
 * --------------------------------------------------------------------------
 * INTERNAL — Complete frame transmit (used by frame_handlers)
 * --------------------------------------------------------------------------
 */

/**
 * @brief Transmit a complete HDLC frame (internal helper).
 */
void hdlc_transmit_frame(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
  if (ctx == NULL || frame == NULL) {
    return;
  }

  hdlc_encode_ctx_t enc_ctx = {.ctx = ctx,
                               .buffer = NULL,
                               .buffer_len = 0,
                               .current_len = 0,
                               .success = true};

  (void)hdlc_frame_pack_core(frame, hdlc_write_byte, &enc_ctx);

  /* Stats: frame_pack_core does not call atc_hdlc_transmit_end(), increment here. */
  HDLC_STAT_INC(ctx, tx_i_frames);
}

/*
 * --------------------------------------------------------------------------
 * STREAMING TRANSMIT API
 * --------------------------------------------------------------------------
 */

/**
 * @brief Begin streaming a frame.
 * @see hdlc.h
 */
void atc_hdlc_transmit_start(atc_hdlc_context_t *ctx, atc_hdlc_u8 address, atc_hdlc_u8 control) {
  if (ctx == NULL) {
    return;
  }

  ctx->tx_crc = ATC_HDLC_FCS_INIT_VALUE;

  hdlc_encode_ctx_t enc = {.ctx = ctx, .success = true};

  ATC_HDLC_LOG_DEBUG("tx: Frame start (Addr: 0x%02X, Ctrl: 0x%02X)", address, control);

  /* Opening flag (raw, no escaping) */
  hdlc_write_byte(&enc, HDLC_FLAG, false);

  /* Address & Control (escaped + CRC update) */
  hdlc_pack_escaped_crc(&enc, hdlc_write_byte, address, &ctx->tx_crc);
  hdlc_pack_escaped_crc(&enc, hdlc_write_byte, control, &ctx->tx_crc);
}

/**
 * @brief Append a single data octet to the streamed frame.
 * @see hdlc.h
 */
void atc_hdlc_transmit_data_byte(atc_hdlc_context_t *ctx, atc_hdlc_u8 byte) {
  if (ctx == NULL) {
    return;
  }

  hdlc_encode_ctx_t enc = {.ctx = ctx, .success = true};
  hdlc_pack_escaped_crc(&enc, hdlc_write_byte, byte, &ctx->tx_crc);
}

/**
 * @brief Append an array of data octets to the streamed frame.
 * @see hdlc.h
 */
void atc_hdlc_transmit_data_bytes(atc_hdlc_context_t *ctx,
                                   const atc_hdlc_u8  *data,
                                   atc_hdlc_u32        len) {
  if (ctx == NULL || (data == NULL && len > 0)) {
    return;
  }

  hdlc_encode_ctx_t enc = {.ctx = ctx, .success = true};
  for (atc_hdlc_u32 i = 0; i < len; ++i) {
    hdlc_pack_escaped_crc(&enc, hdlc_write_byte, data[i], &ctx->tx_crc);
  }
}

/**
 * @brief Finalise the streamed frame — emit FCS and closing flag.
 * @see hdlc.h
 */
void atc_hdlc_transmit_end(atc_hdlc_context_t *ctx) {
  if (ctx == NULL) {
    return;
  }

  hdlc_encode_ctx_t enc = {.ctx = ctx, .success = true};

  atc_hdlc_u16 crc = ctx->tx_crc;
  atc_hdlc_u8 fcs_hi = (crc >> 8) & 0xFF;
  atc_hdlc_u8 fcs_lo = crc & 0xFF;

  /* FCS (escaped, no CRC update) */
  hdlc_pack_escaped(&enc, hdlc_write_byte, fcs_hi);
  hdlc_pack_escaped(&enc, hdlc_write_byte, fcs_lo);

  /* Closing flag (raw) */
  hdlc_write_byte(&enc, HDLC_FLAG, true);

  HDLC_STAT_INC(ctx, tx_i_frames);
}

/*
 * --------------------------------------------------------------------------
 * CONVENIENCE FRAME STARTERS
 * --------------------------------------------------------------------------
 */

/**
 * @brief Begin streaming a UI frame.
 * @see hdlc.h
 */
void atc_hdlc_transmit_start_ui(atc_hdlc_context_t *ctx, atc_hdlc_u8 address) {
  if (ctx == NULL) {
    return;
  }
  atc_hdlc_u8 ctrl = hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_UI, HDLC_U_MODIFIER_HI_UI, 0);
  atc_hdlc_transmit_start(ctx, address, ctrl);
}

/**
 * @brief Begin streaming a TEST frame.
 * @see hdlc.h
 */
void atc_hdlc_transmit_start_test(atc_hdlc_context_t *ctx, atc_hdlc_u8 address) {
  if (ctx == NULL) {
    return;
  }
  atc_hdlc_u8 ctrl = hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_TEST, HDLC_U_MODIFIER_HI_TEST, 1);
  atc_hdlc_transmit_start(ctx, address, ctrl);
}

/*
 * --------------------------------------------------------------------------
 * CONVENIENCE COMPLETE-FRAME WRAPPERS
 * --------------------------------------------------------------------------
 */

/**
 * @brief Transmit an Unnumbered Information (UI) frame.
 * @see hdlc.h
 */
atc_hdlc_error_t atc_hdlc_transmit_ui(atc_hdlc_context_t *ctx,
                                        atc_hdlc_u8         address,
                                        const atc_hdlc_u8  *data,
                                        atc_hdlc_u32        len) {
    if (ctx == NULL) return ATC_HDLC_ERR_INVALID_PARAM;
    if (ctx->config && len > ctx->config->max_frame_size) {
        return ATC_HDLC_ERR_FRAME_TOO_LARGE;
    }

    atc_hdlc_transmit_start_ui(ctx, address);
    if (data != NULL && len > 0) {
        atc_hdlc_transmit_data_bytes(ctx, data, len);
    }
    atc_hdlc_transmit_end(ctx);
    return ATC_HDLC_OK;
}

/**
 * @brief Transmit a TEST frame and await the echo.
 * @see hdlc.h
 */
atc_hdlc_error_t atc_hdlc_transmit_test(atc_hdlc_context_t *ctx,
                                          atc_hdlc_u8         address,
                                          const atc_hdlc_u8  *data,
                                          atc_hdlc_u32        len) {
    if (ctx == NULL) return ATC_HDLC_ERR_INVALID_PARAM;
    if (ctx->test_pending) return ATC_HDLC_ERR_TEST_PENDING;
    if (ctx->config && len > ctx->config->max_frame_size) {
        return ATC_HDLC_ERR_FRAME_TOO_LARGE;
    }

    ctx->test_pattern     = data;
    ctx->test_pattern_len = (atc_hdlc_u16)len;
    ctx->test_pending     = true;
    ctx->stats.test_sent++;

    atc_hdlc_transmit_start_test(ctx, address);
    if (data != NULL && len > 0) {
        atc_hdlc_transmit_data_bytes(ctx, data, len);
    }
    atc_hdlc_transmit_end(ctx);

    /* Start T1 to detect timeout */
    hdlc_t1_start(ctx);

    return ATC_HDLC_OK;
}

/**
 * @brief Transmit a reliable Information (I) frame.
 * @see hdlc.h
 */
atc_hdlc_error_t atc_hdlc_transmit_i(atc_hdlc_context_t *ctx,
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
    atc_hdlc_u8 ctrl = hdlc_create_i_ctrl(ctx->vs, ctx->vr, 0);
    atc_hdlc_transmit_start(ctx, ctx->peer_address, ctrl);
    if (data != NULL && len > 0) {
        atc_hdlc_transmit_data_bytes(ctx, data, len);
    }
    atc_hdlc_transmit_end(ctx);

    /* Advance V(S), cancel T2 (ACK piggybacked in N(R)) */
    ctx->vs = (atc_hdlc_u8)((ctx->vs + 1) % HDLC_SEQUENCE_MODULUS);
    hdlc_t2_stop(ctx);

    /* Start T1 only for the first outstanding frame */
    if (outstanding == 0) {
        hdlc_t1_start(ctx);
    }

    HDLC_STAT_INC(ctx, tx_i_frames);
    HDLC_STAT_ADD(ctx, tx_bytes, len);

    return ATC_HDLC_OK;
}
