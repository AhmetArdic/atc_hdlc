/*
 * Copyright (C) 2026 Ahmet Talha ARDIC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef ATC_HDLC_PRIVATE_H
#define ATC_HDLC_PRIVATE_H

#include "../inc/hdlc_types.h"
#include "frame/hdlc_crc.h"

#if ATC_HDLC_ENABLE_STATS
#   define HDLC_STAT_INC(ctx, field)     ((ctx)->stats.field++)
#   define HDLC_STAT_ADD(ctx, field, n)  ((ctx)->stats.field += (n))
#else
#   define HDLC_STAT_INC(ctx, field)     ((void)0)
#   define HDLC_STAT_ADD(ctx, field, n)  ((void)0)
#endif

#if ATC_HDLC_ENABLE_ASSERT
#   include <assert.h>
#   define HDLC_ASSERT(cond) assert(cond)
#else
#   define HDLC_ASSERT(cond) ((void)0)
#endif

#if ATC_HDLC_ENABLE_DEBUG_LOGS
#   define ATC_HDLC_LOG_DEBUG(fmt, ...) ATC_HDLC_LOG_IMPL("DEBUG", fmt, ##__VA_ARGS__)
#   define ATC_HDLC_LOG_WARN(fmt, ...)  ATC_HDLC_LOG_IMPL("WARN",  fmt, ##__VA_ARGS__)
#   define ATC_HDLC_LOG_ERROR(fmt, ...) ATC_HDLC_LOG_IMPL("ERROR", fmt, ##__VA_ARGS__)
#else
#   define ATC_HDLC_LOG_DEBUG(fmt, ...)
#   define ATC_HDLC_LOG_WARN(fmt, ...)
#   define ATC_HDLC_LOG_ERROR(fmt, ...)
#endif

#define HDLC_FLAG 0x7E
#define HDLC_ESCAPE 0x7D
#define HDLC_XOR_MASK 0x20

#define HDLC_FLAG_LEN               (1)
#define HDLC_ADDRESS_LEN            (1)
#define HDLC_CONTROL_LEN            (1)
#define HDLC_FCS_LEN                (2)

#define HDLC_MIN_FRAME_LEN          (HDLC_ADDRESS_LEN + HDLC_CONTROL_LEN + HDLC_FCS_LEN)

#define HDLC_FRAME_TYPE_MASK_I      (0x01)
#define HDLC_FRAME_TYPE_VAL_I       (0x00)

#define HDLC_FRAME_TYPE_MASK_S      (0x03)
#define HDLC_FRAME_TYPE_VAL_S       (0x01)

#define HDLC_FRAME_TYPE_MASK_U      (0x03)
#define HDLC_FRAME_TYPE_VAL_U       (0x03)

#define HDLC_CTRL_PF(ctrl)      (((ctrl) >> 4) & 0x01)
#define HDLC_CTRL_NR(ctrl)      (((ctrl) >> 5) & 0x07)
#define HDLC_CTRL_I_NS(ctrl)    (((ctrl) >> 1) & 0x07)
#define HDLC_CTRL_S_BITS(ctrl)  (((ctrl) >> 2) & 0x03)

#define HDLC_I_CTRL(ns, nr, pf) ((atc_hdlc_u8)(((ns) & 0x07) << 1 | ((pf) & 0x01) << 4 | ((nr) & 0x07) << 5))
#define HDLC_S_CTRL(s, nr, pf)  ((atc_hdlc_u8)(0x01 | ((s) & 0x03) << 2 | ((pf) & 0x01) << 4 | ((nr) & 0x07) << 5))
#define HDLC_U_CTRL(cmd, pf)    ((atc_hdlc_u8)((cmd) | ((pf) ? HDLC_PF_BIT : 0)))

#define HDLC_SEQUENCE_MODULUS   8

#define HDLC_S_RR   0
#define HDLC_S_RNR  1
#define HDLC_S_REJ  2

/* U-frame command/response codes (ctrl & ~HDLC_PF_BIT strips the P/F bit) */
#define HDLC_U_SABM   0x2F
#define HDLC_U_DISC   0x43
#define HDLC_U_UA     0x63
#define HDLC_U_DM     0x0F
#define HDLC_U_FRMR   0x87
#define HDLC_U_UI     0x03
#define HDLC_U_TEST   0xE3
#define HDLC_U_SNRM   0x83
#define HDLC_U_SABME  0x6F
#define HDLC_U_SNRME  0xCF
#define HDLC_U_SARME  0x4F
#define HDLC_PF_BIT   0x10

#define HDLC_FRMR_W_BIT         0x01
#define HDLC_FRMR_X_BIT         0x02
#define HDLC_FRMR_Y_BIT         0x04
#define HDLC_FRMR_Z_BIT         0x08
#define HDLC_FRMR_V_BIT         0x10

typedef enum {
    HDLC_RX_STATE_HUNT = 0,
    HDLC_RX_STATE_ADDRESS,
    HDLC_RX_STATE_DATA,
    HDLC_RX_STATE_ESCAPE
} hdlc_rx_state_t;

typedef struct {
    atc_hdlc_u16 rejected_control;
    atc_hdlc_u8 v_s;
    atc_hdlc_u8 v_r;
    atc_hdlc_bool cr;
    struct {
        atc_hdlc_bool w;
        atc_hdlc_bool x;
        atc_hdlc_bool y;
        atc_hdlc_bool z;
        atc_hdlc_bool v;
    } errors;
} atc_hdlc_frmr_data_t;

typedef struct {
  atc_hdlc_context_t *ctx;
  atc_hdlc_u8 *buffer;
  atc_hdlc_u32 buffer_len;
  atc_hdlc_u32 current_len;
  atc_hdlc_bool success;
} hdlc_encode_ctx_t;

typedef void (*hdlc_put_byte_fn)(hdlc_encode_ctx_t *enc_ctx, atc_hdlc_u8 byte, atc_hdlc_bool flush);

void hdlc_set_protocol_state(atc_hdlc_context_t *ctx, atc_hdlc_state_t new_state, atc_hdlc_event_t event);

void hdlc_pack_escaped(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn, atc_hdlc_u8 byte);
void hdlc_pack_escaped_crc(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn, atc_hdlc_u8 byte, atc_hdlc_u16 *crc);
atc_hdlc_bool hdlc_frame_pack_core(const atc_hdlc_frame_t *frame, hdlc_put_byte_fn put_fn, hdlc_encode_ctx_t *enc_ctx);

void hdlc_process_complete_frame(atc_hdlc_context_t *ctx);

void hdlc_reset_connection_state(atc_hdlc_context_t *ctx);

static inline void hdlc_t1_start(atc_hdlc_context_t *ctx) {
    if (ctx->platform && ctx->platform->t1_start && ctx->config) {
        ctx->platform->t1_start(ctx->config->t1_ms, ctx->platform->user_ctx);
    }
    ctx->t1_active = true;
}

static inline void hdlc_t1_stop(atc_hdlc_context_t *ctx) {
    if (ctx->t1_active && ctx->platform && ctx->platform->t1_stop) {
        ctx->platform->t1_stop(ctx->platform->user_ctx);
    }
    ctx->t1_active = false;
}

static inline void hdlc_t2_start(atc_hdlc_context_t *ctx) {
    if (ctx->platform && ctx->platform->t2_start && ctx->config) {
        ctx->platform->t2_start(ctx->config->t2_ms, ctx->platform->user_ctx);
    }
    ctx->t2_active = true;
}

static inline void hdlc_t2_stop(atc_hdlc_context_t *ctx) {
    if (ctx->t2_active && ctx->platform && ctx->platform->t2_stop) {
        ctx->platform->t2_stop(ctx->platform->user_ctx);
    }
    ctx->t2_active = false;
}

static inline void hdlc_write_byte(hdlc_encode_ctx_t *enc_ctx, atc_hdlc_u8 byte, atc_hdlc_bool flush) {
  if (enc_ctx->ctx && enc_ctx->ctx->platform && enc_ctx->ctx->platform->on_send) {
    enc_ctx->ctx->platform->on_send(byte, flush, enc_ctx->ctx->platform->user_ctx);
  }
}

static inline void hdlc_transmit_start(atc_hdlc_context_t *ctx, atc_hdlc_u8 address, atc_hdlc_u8 control) {
  if (ctx == NULL) {
    return;
  }

  ctx->tx_crc = ATC_HDLC_FCS_INIT_VALUE;

  hdlc_encode_ctx_t enc = {.ctx = ctx, .success = true};

  ATC_HDLC_LOG_DEBUG("tx: Frame start (Addr: 0x%02X, Ctrl: 0x%02X)", address, control);

  hdlc_write_byte(&enc, HDLC_FLAG, false);

  hdlc_pack_escaped_crc(&enc, hdlc_write_byte, address, &ctx->tx_crc);
  hdlc_pack_escaped_crc(&enc, hdlc_write_byte, control, &ctx->tx_crc);
}

static inline void hdlc_transmit_frame(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
  if (ctx == NULL || frame == NULL) {
    return;
  }

  hdlc_encode_ctx_t enc_ctx = {.ctx = ctx,
                               .buffer = NULL,
                               .buffer_len = 0,
                               .current_len = 0,
                               .success = true};

  (void)hdlc_frame_pack_core(frame, hdlc_write_byte, &enc_ctx);
}

static inline atc_hdlc_frame_type_t hdlc_resolve_frame_type(atc_hdlc_u8 ctrl) {
    if ((ctrl & HDLC_FRAME_TYPE_MASK_I) == HDLC_FRAME_TYPE_VAL_I) return ATC_HDLC_FRAME_I;
    if ((ctrl & HDLC_FRAME_TYPE_MASK_S) == HDLC_FRAME_TYPE_VAL_S) return ATC_HDLC_FRAME_S;
    if ((ctrl & HDLC_FRAME_TYPE_MASK_U) == HDLC_FRAME_TYPE_VAL_U) return ATC_HDLC_FRAME_U;
    return ATC_HDLC_FRAME_INVALID;
}

static inline int hdlc_is_i_frame(atc_hdlc_u8 ctrl) {
    return (ctrl & HDLC_FRAME_TYPE_MASK_I) == HDLC_FRAME_TYPE_VAL_I;
}

static inline int hdlc_is_s_frame(atc_hdlc_u8 ctrl) {
    return (ctrl & HDLC_FRAME_TYPE_MASK_S) == HDLC_FRAME_TYPE_VAL_S;
}

static inline int hdlc_is_u_frame(atc_hdlc_u8 ctrl) {
    return (ctrl & HDLC_FRAME_TYPE_MASK_U) == HDLC_FRAME_TYPE_VAL_U;
}

static inline int hdlc_is_cmd(const atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame) {
    return frame->address == ctx->my_address;
}

static inline void hdlc_send_u_frame(atc_hdlc_context_t *ctx, atc_hdlc_u8 address, atc_hdlc_u8 ctrl) {
    hdlc_transmit_start(ctx, address, ctrl);
    atc_hdlc_transmit_end(ctx);
}

static inline void hdlc_send_ua(atc_hdlc_context_t *ctx, atc_hdlc_u8 pf) {
    hdlc_send_u_frame(ctx, ctx->my_address, HDLC_U_CTRL(HDLC_U_UA, pf));
}

static inline void hdlc_send_dm(atc_hdlc_context_t *ctx, atc_hdlc_u8 pf) {
    hdlc_send_u_frame(ctx, ctx->my_address, HDLC_U_CTRL(HDLC_U_DM, pf));
}

static inline void hdlc_send_s_frame(atc_hdlc_context_t *ctx, atc_hdlc_u8 address, atc_hdlc_u8 s_bits, atc_hdlc_u8 nr, atc_hdlc_u8 pf) {
    hdlc_transmit_start(ctx, address, HDLC_S_CTRL(s_bits, nr, pf));
    atc_hdlc_transmit_end(ctx);
}

static inline void hdlc_send_rr(atc_hdlc_context_t *ctx, atc_hdlc_u8 pf) {
    hdlc_send_s_frame(ctx, ctx->peer_address, HDLC_S_RR, ctx->vr, pf);
}

static inline void hdlc_send_response_rr(atc_hdlc_context_t *ctx, atc_hdlc_u8 pf) {
    hdlc_send_s_frame(ctx, ctx->my_address, HDLC_S_RR, ctx->vr, pf);
}

static inline void hdlc_send_rnr(atc_hdlc_context_t *ctx, atc_hdlc_u8 pf) {
    hdlc_send_s_frame(ctx, ctx->my_address, HDLC_S_RNR, ctx->vr, pf);
}

static inline void hdlc_send_rej(atc_hdlc_context_t *ctx, atc_hdlc_u8 pf) {
    hdlc_send_s_frame(ctx, ctx->peer_address, HDLC_S_REJ, ctx->vr, pf);
}

static inline void hdlc_send_frmr(atc_hdlc_context_t *ctx,
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

    atc_hdlc_frame_t frame = {
        .address = ctx->my_address,
        .control = HDLC_U_CTRL(HDLC_U_FRMR, 0),
        .information = info,
        .information_len = sizeof(info)
    };

    hdlc_transmit_frame(ctx, &frame);

    HDLC_STAT_INC(ctx, frmr_count);

    hdlc_t1_stop(ctx);
    hdlc_t2_stop(ctx);
    hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_FRMR_ERROR, ATC_HDLC_EVENT_PROTOCOL_ERROR);
}

#endif /* ATC_HDLC_PRIVATE_H */
