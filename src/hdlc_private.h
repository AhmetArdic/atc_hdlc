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

#if ATC_HDLC_ENABLE_DEBUG_LOGS
#define ATC_HDLC_LOG_DEBUG(fmt, ...)                                           \
  ATC_HDLC_LOG_IMPL("DEBUG", fmt, ##__VA_ARGS__)
#define ATC_HDLC_LOG_WARN(fmt, ...)                                            \
  ATC_HDLC_LOG_IMPL("WARN", fmt, ##__VA_ARGS__)
#define ATC_HDLC_LOG_ERROR(fmt, ...)                                           \
  ATC_HDLC_LOG_IMPL("ERROR", fmt, ##__VA_ARGS__)
#else
#define ATC_HDLC_LOG_DEBUG(fmt, ...)
#define ATC_HDLC_LOG_WARN(fmt, ...)
#define ATC_HDLC_LOG_ERROR(fmt, ...)
#endif

/* --- Framing constants --- */
#define HDLC_FLAG     0x7E
#define HDLC_ESCAPE   0x7D
#define HDLC_XOR_MASK 0x20

#define HDLC_FLAG_LEN    (1)
#define HDLC_ADDRESS_LEN (1)
#define HDLC_CONTROL_LEN (1)
#define HDLC_FCS_LEN     (2)

#define HDLC_MIN_FRAME_LEN (HDLC_ADDRESS_LEN + HDLC_CONTROL_LEN + HDLC_FCS_LEN)

/* --- Control byte field accessors --- */
#define HDLC_CTRL_PF(ctrl)     (((ctrl) >> 4) & 0x01)
#define HDLC_CTRL_NR(ctrl)     (((ctrl) >> 5) & 0x07)
#define HDLC_CTRL_I_NS(ctrl)   (((ctrl) >> 1) & 0x07)
#define HDLC_CTRL_S_BITS(ctrl) (((ctrl) >> 2) & 0x03)

/* --- Control byte constructors --- */
#define HDLC_I_CTRL(ns, nr, pf) \
  ((atc_hdlc_u8)(((ns) & 0x07) << 1 | ((pf) & 0x01) << 4 | ((nr) & 0x07) << 5))
#define HDLC_S_CTRL(s, nr, pf) \
  ((atc_hdlc_u8)(0x01 | ((s) & 0x03) << 2 | ((pf) & 0x01) << 4 | ((nr) & 0x07) << 5))
#define HDLC_U_CTRL(cmd, pf) ((atc_hdlc_u8)((cmd) | ((pf) ? HDLC_PF_BIT : 0)))

#define HDLC_SEQUENCE_MODULUS 8

/* --- S-frame supervisory bits --- */
#define HDLC_S_RR  0
#define HDLC_S_RNR 1
#define HDLC_S_REJ 2

/* --- U-frame command/response codes (ctrl & ~HDLC_PF_BIT strips the P/F bit) --- */
#define HDLC_U_SABM  0x2F
#define HDLC_U_DISC  0x43
#define HDLC_U_UA    0x63
#define HDLC_U_DM    0x0F
#define HDLC_U_FRMR  0x87
#define HDLC_U_UI    0x03
#define HDLC_U_TEST  0xE3
#define HDLC_U_SNRM  0x83
#define HDLC_U_SABME 0x6F
#define HDLC_U_SNRME 0xCF
#define HDLC_U_SARME 0x4F
#define HDLC_PF_BIT  0x10

/* --- FRMR reason bits --- */
#define HDLC_FRMR_W_BIT 0x01
#define HDLC_FRMR_X_BIT 0x02
#define HDLC_FRMR_Y_BIT 0x04
#define HDLC_FRMR_Z_BIT 0x08
#define HDLC_FRMR_V_BIT 0x10

typedef enum {
  HDLC_RX_STATE_HUNT = 0,
  HDLC_RX_STATE_ADDRESS,
  HDLC_RX_STATE_DATA,
  HDLC_RX_STATE_ESCAPE
} hdlc_rx_state_t;

/* --- Frame type predicates --- */
static inline int hdlc_is_i_frame(atc_hdlc_u8 ctrl) { return (ctrl & 0x01) == 0; }
static inline int hdlc_is_s_frame(atc_hdlc_u8 ctrl) { return (ctrl & 0x03) == 0x01; }
static inline int hdlc_is_u_frame(atc_hdlc_u8 ctrl) { return (ctrl & 0x03) == 0x03; }

static inline int hdlc_is_cmd(const atc_hdlc_context_t *ctx, atc_hdlc_u8 address) {
  return address == ctx->my_address;
}

/* --- Internal function declarations --- */
void hdlc_set_protocol_state(atc_hdlc_context_t *ctx,
                             atc_hdlc_state_t new_state,
                             atc_hdlc_event_t event);

void hdlc_reset_connection_state(atc_hdlc_context_t *ctx);

void hdlc_process_complete_frame(atc_hdlc_context_t *ctx,
                                 atc_hdlc_u8 address, atc_hdlc_u8 ctrl,
                                 const atc_hdlc_u8 *info, atc_hdlc_u16 info_len);

/* --- Timer helpers --- */
static inline void hdlc_t1_start(atc_hdlc_context_t *ctx) {
  if (ctx->platform && ctx->platform->t1_start && ctx->config)
    ctx->platform->t1_start(ctx->config->t1_ms, ctx->platform->user_ctx);
  ctx->t1_active = true;
}

static inline void hdlc_t1_stop(atc_hdlc_context_t *ctx) {
  if (ctx->t1_active && ctx->platform && ctx->platform->t1_stop)
    ctx->platform->t1_stop(ctx->platform->user_ctx);
  ctx->t1_active = false;
}

static inline void hdlc_t2_start(atc_hdlc_context_t *ctx) {
  if (ctx->platform && ctx->platform->t2_start && ctx->config)
    ctx->platform->t2_start(ctx->config->t2_ms, ctx->platform->user_ctx);
  ctx->t2_active = true;
}

static inline void hdlc_t2_stop(atc_hdlc_context_t *ctx) {
  if (ctx->t2_active && ctx->platform && ctx->platform->t2_stop)
    ctx->platform->t2_stop(ctx->platform->user_ctx);
  ctx->t2_active = false;
}

/* --- TX byte-level primitives --- */
static inline void hdlc_put_raw(atc_hdlc_context_t *ctx, atc_hdlc_u8 byte, atc_hdlc_bool flush) {
  if (ctx->platform && ctx->platform->on_send)
    ctx->platform->on_send(byte, flush, ctx->platform->user_ctx);
}

static inline void hdlc_put_escaped(atc_hdlc_context_t *ctx, atc_hdlc_u8 byte) {
  if (byte == HDLC_FLAG || byte == HDLC_ESCAPE) {
    hdlc_put_raw(ctx, HDLC_ESCAPE, false);
    hdlc_put_raw(ctx, (atc_hdlc_u8)(byte ^ HDLC_XOR_MASK), false);
  } else {
    hdlc_put_raw(ctx, byte, false);
  }
}

/* Updates ctx->tx_crc, then escapes and sends byte. */
static inline void hdlc_put_escaped_crc(atc_hdlc_context_t *ctx, atc_hdlc_u8 byte) {
  ctx->tx_crc = atc_hdlc_crc_ccitt_update(ctx->tx_crc, byte);
  hdlc_put_escaped(ctx, byte);
}

/* Begin a frame: FLAG + address + control (with CRC tracking). */
static inline void hdlc_transmit_start(atc_hdlc_context_t *ctx,
                                       atc_hdlc_u8 address, atc_hdlc_u8 control) {
  if (!ctx) return;
  ctx->tx_crc = ATC_HDLC_FCS_INIT_VALUE;
  hdlc_put_raw(ctx, HDLC_FLAG, false);
  hdlc_put_escaped_crc(ctx, address);
  hdlc_put_escaped_crc(ctx, control);
  ATC_HDLC_LOG_DEBUG("tx: Frame start (Addr: 0x%02X, Ctrl: 0x%02X)", address, control);
}

/* End a frame: FCS bytes + closing FLAG. */
static inline void hdlc_finish_frame(atc_hdlc_context_t *ctx) {
  hdlc_put_escaped(ctx, (atc_hdlc_u8)(ctx->tx_crc >> 8));
  hdlc_put_escaped(ctx, (atc_hdlc_u8)(ctx->tx_crc & 0xFF));
  hdlc_put_raw(ctx, HDLC_FLAG, true);
}

/* --- Frame-level send helpers --- */
static inline void hdlc_send_u_frame(atc_hdlc_context_t *ctx,
                                     atc_hdlc_u8 address, atc_hdlc_u8 ctrl) {
  hdlc_transmit_start(ctx, address, ctrl);
  hdlc_finish_frame(ctx);
}

static inline void hdlc_send_ua(atc_hdlc_context_t *ctx, atc_hdlc_u8 pf) {
  hdlc_send_u_frame(ctx, ctx->my_address, HDLC_U_CTRL(HDLC_U_UA, pf));
}

static inline void hdlc_send_dm(atc_hdlc_context_t *ctx, atc_hdlc_u8 pf) {
  hdlc_send_u_frame(ctx, ctx->my_address, HDLC_U_CTRL(HDLC_U_DM, pf));
}

static inline void hdlc_send_s_frame(atc_hdlc_context_t *ctx,
                                     atc_hdlc_u8 address, atc_hdlc_u8 s_bits,
                                     atc_hdlc_u8 nr, atc_hdlc_u8 pf) {
  hdlc_transmit_start(ctx, address, HDLC_S_CTRL(s_bits, nr, pf));
  hdlc_finish_frame(ctx);
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
                                  atc_hdlc_u8 rejected_ctrl, atc_hdlc_bool w,
                                  atc_hdlc_bool x, atc_hdlc_bool y, atc_hdlc_bool z) {
  atc_hdlc_u8 info[3];
  info[0] = rejected_ctrl;
  info[1] = (atc_hdlc_u8)(((ctx->vr & 0x07) << 5) | ((ctx->vs & 0x07) << 1));
  info[2] = (atc_hdlc_u8)((w ? HDLC_FRMR_W_BIT : 0) | (x ? HDLC_FRMR_X_BIT : 0) |
                           (y ? HDLC_FRMR_Y_BIT : 0) | (z ? HDLC_FRMR_Z_BIT : 0));

  ATC_HDLC_LOG_ERROR("tx: FRMR ctrl=0x%02X W=%u X=%u Y=%u Z=%u", rejected_ctrl,
                     (unsigned)w, (unsigned)x, (unsigned)y, (unsigned)z);

  hdlc_transmit_start(ctx, ctx->my_address, HDLC_U_CTRL(HDLC_U_FRMR, 0));
  hdlc_put_escaped_crc(ctx, info[0]);
  hdlc_put_escaped_crc(ctx, info[1]);
  hdlc_put_escaped_crc(ctx, info[2]);
  hdlc_finish_frame(ctx);

  hdlc_t1_stop(ctx);
  hdlc_t2_stop(ctx);
  hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_FRMR_ERROR, ATC_HDLC_EVENT_PROTOCOL_ERROR);
}

#endif /* ATC_HDLC_PRIVATE_H */
