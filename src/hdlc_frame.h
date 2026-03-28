/*
 * Copyright (C) 2026 Ahmet Talha ARDIC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef ATC_HDLC_FRAME_H
#define ATC_HDLC_FRAME_H

#include "hdlc_crc.h"
#include "hdlc_private.h"

#define FLAG     (0x7E)
#define ESC      (0x7D)
#define XOR_MASK (0x20)

#define ADDR_LEN (1)
#define CTRL_LEN (1)
#define FCS_LEN  (2)

#define MIN_FRAME_LEN (ADDR_LEN + CTRL_LEN + FCS_LEN)

#define CTRL_PF(ctrl) (((ctrl) >> 4) & 0x01)
#define CTRL_NR(ctrl) (((ctrl) >> 5) & 0x07)
#define CTRL_NS(ctrl) (((ctrl) >> 1) & 0x07)
#define CTRL_S(ctrl)  (((ctrl) >> 2) & 0x03)

#define I_CTRL(ns, nr, pf)                                                                         \
    ((atc_hdlc_u8)(((ns) & 0x07) << 1 | ((pf) & 0x01) << 4 | ((nr) & 0x07) << 5))
#define S_CTRL(s, nr, pf)                                                                          \
    ((atc_hdlc_u8)(0x01 | ((s) & 0x03) << 2 | ((pf) & 0x01) << 4 | ((nr) & 0x07) << 5))
#define U_CTRL(cmd, pf) ((atc_hdlc_u8)((cmd) | ((pf) ? PF_BIT : 0)))

#define MOD8 (8)

#define S_RR  (0)
#define S_RNR (1)
#define S_REJ (2)

/**
 * @note use ctrl & ~PF_BIT to strip the P/F bit
 */
#define U_SABM  (0x2F)
#define U_DISC  (0x43)
#define U_UA    (0x63)
#define U_DM    (0x0F)
#define U_FRMR  (0x87)
#define U_UI    (0x03)
#define U_TEST  (0xE3)
#define U_SNRM  (0x83)
#define U_SABME (0x6F)
#define U_SNRME (0xCF)
#define U_SARME (0x4F)
#define PF_BIT  (0x10)

#define FRMR_W (0x01)
#define FRMR_X (0x02)
#define FRMR_Y (0x04)
#define FRMR_Z (0x08)
#define FRMR_V (0x10)

typedef enum { RX_HUNT = 0, RX_ADDR, RX_DATA, RX_ESC } rx_state_t;

static inline int is_iframe(atc_hdlc_u8 ctrl) {
    return (ctrl & 0x01) == 0;
}
static inline int is_sframe(atc_hdlc_u8 ctrl) {
    return (ctrl & 0x03) == 0x01;
}
static inline int is_uframe(atc_hdlc_u8 ctrl) {
    return (ctrl & 0x03) == 0x03;
}

static inline int is_cmd(const atc_hdlc_context_t* ctx, atc_hdlc_u8 address) {
    return address == ctx->my_address;
}

static inline void put_raw(atc_hdlc_context_t* ctx, atc_hdlc_u8 byte, bool flush) {
    if (ctx->platform->on_send)
        ctx->platform->on_send(byte, flush, ctx->platform->user_ctx);
}

static inline void put_escaped(atc_hdlc_context_t* ctx, atc_hdlc_u8 byte) {
    if (byte == FLAG || byte == ESC) {
        put_raw(ctx, ESC, false);
        put_raw(ctx, (atc_hdlc_u8)(byte ^ XOR_MASK), false);
    } else {
        put_raw(ctx, byte, false);
    }
}

static inline void emit(atc_hdlc_context_t* ctx, atc_hdlc_u8 byte) {
    ctx->tx_crc = ctx->crc->compute(ctx->tx_crc, &byte, 1);
    put_escaped(ctx, byte);
}

static inline void frame_begin(atc_hdlc_context_t* ctx, atc_hdlc_u8 address, atc_hdlc_u8 control) {
    if (!ctx)
        return;
    ctx->tx_crc = ATC_HDLC_FCS_INIT_VALUE;
    put_raw(ctx, FLAG, false);
    emit(ctx, address);
    emit(ctx, control);
    LOG_DBG("tx: Frame start (Addr: 0x%02X, Ctrl: 0x%02X)", address, control);
}

static inline void frame_end(atc_hdlc_context_t* ctx) {
    put_escaped(ctx, (atc_hdlc_u8)(ctx->tx_crc & 0xFF));
    put_escaped(ctx, (atc_hdlc_u8)(ctx->tx_crc >> 8));
    put_raw(ctx, FLAG, true);
}

static inline void frame_send(atc_hdlc_context_t* ctx, atc_hdlc_u8 address, atc_hdlc_u8 ctrl,
                              const atc_hdlc_u8* data, atc_hdlc_u32 len) {
    frame_begin(ctx, address, ctrl);
    for (atc_hdlc_u32 i = 0; i < len; i++)
        emit(ctx, data[i]);
    frame_end(ctx);
}

static inline void send_u(atc_hdlc_context_t* ctx, atc_hdlc_u8 address, atc_hdlc_u8 ctrl) {
    frame_begin(ctx, address, ctrl);
    frame_end(ctx);
}

static inline void send_ua(atc_hdlc_context_t* ctx, atc_hdlc_u8 pf) {
    send_u(ctx, ctx->my_address, U_CTRL(U_UA, pf));
}

static inline void send_dm(atc_hdlc_context_t* ctx, atc_hdlc_u8 pf) {
    send_u(ctx, ctx->my_address, U_CTRL(U_DM, pf));
}

static inline void send_s(atc_hdlc_context_t* ctx, atc_hdlc_u8 address, atc_hdlc_u8 s,
                          atc_hdlc_u8 nr, atc_hdlc_u8 pf) {
    frame_begin(ctx, address, S_CTRL(s, nr, pf));
    frame_end(ctx);
}

static inline void send_rr(atc_hdlc_context_t* ctx, atc_hdlc_u8 pf) {
    send_s(ctx, ctx->peer_address, S_RR, ctx->vr, pf);
}

static inline void send_rr_resp(atc_hdlc_context_t* ctx, atc_hdlc_u8 pf) {
    send_s(ctx, ctx->my_address, S_RR, ctx->vr, pf);
}

static inline void send_rnr(atc_hdlc_context_t* ctx, atc_hdlc_u8 pf) {
    send_s(ctx, ctx->my_address, S_RNR, ctx->vr, pf);
}

static inline void send_rej(atc_hdlc_context_t* ctx, atc_hdlc_u8 pf) {
    send_s(ctx, ctx->peer_address, S_REJ, ctx->vr, pf);
}

static inline void retransmit_frmr(atc_hdlc_context_t* ctx) {
    atc_hdlc_u8 status = (atc_hdlc_u8)(((ctx->vr & 0x07) << 5) | ((ctx->vs & 0x07) << 1));
    frame_begin(ctx, ctx->my_address, U_CTRL(U_FRMR, 0));
    emit(ctx, ctx->frmr_ctrl);
    emit(ctx, status);
    emit(ctx, ctx->frmr_flags);
    frame_end(ctx);
}

static inline void send_frmr(atc_hdlc_context_t* ctx, atc_hdlc_u8 rejected_ctrl, bool w, bool x,
                             bool y, bool z) {
    ctx->frmr_ctrl = rejected_ctrl;
    ctx->frmr_flags =
        (atc_hdlc_u8)((w ? FRMR_W : 0) | (x ? FRMR_X : 0) | (y ? FRMR_Y : 0) | (z ? FRMR_Z : 0));

    LOG_ERR("tx: FRMR ctrl=0x%02X W=%u X=%u Y=%u Z=%u", rejected_ctrl, (unsigned)w, (unsigned)x,
            (unsigned)y, (unsigned)z);

    t2_stop(ctx);
    retransmit_frmr(ctx);
    t1_start(ctx);
}

#endif /* ATC_HDLC_FRAME_H */
