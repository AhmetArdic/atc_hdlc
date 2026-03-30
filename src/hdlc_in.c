/*
 * Copyright (C) 2026 Ahmet Talha ARDIC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../inc/atc_hdlc/hdlc.h"
#include "hdlc_crc.h"
#include "hdlc_frame.h"

static void retransmit_outstanding(atc_hdlc_ctx_t* ctx) {
    CTX_CLR(ctx, HDLC_F_RETRANSMIT_PENDING);
    atc_hdlc_u8 end_vs = ctx->vs;
    atc_hdlc_u8 w = ctx->tx_window->slot_count;
    /* Slot of oldest frame = (tx_next_slot - outstanding + w) % w. */
    atc_hdlc_u8 outstanding = (atc_hdlc_u8)((end_vs - ctx->retransmit_from + MOD8) % MOD8);
    atc_hdlc_u8 slot_idx = (atc_hdlc_u8)((ctx->tx_next_slot + w - outstanding) % w);
    ctx->vs = ctx->retransmit_from;
    while (ctx->vs != end_vs) {
        const atc_hdlc_u8* sd = ctx->tx_window->slots + (slot_idx * ctx->tx_window->slot_capacity);
        atc_hdlc_u32 slen = ctx->tx_window->slot_lens[slot_idx];
        frame_send(ctx, ctx->peer_address, I_CTRL(ctx->vs, ctx->vr, 0), sd, slen);
        ctx->vs = (atc_hdlc_u8)((ctx->vs + 1) % MOD8);
        slot_idx = (atc_hdlc_u8)((slot_idx + 1u) % w);
    }
    t1_start(ctx);
}

static void handle_flag(atc_hdlc_ctx_t* ctx) {
    if (ctx->rx_state == RX_HUNT || ctx->rx_index < MIN_FRAME_LEN)
        goto reset;

    atc_hdlc_u32 dlen = ctx->rx_index - FCS_LEN;
    atc_hdlc_u16 rx_fcs =
        (atc_hdlc_u16)(ctx->rx_buf->buffer[dlen] | ((atc_hdlc_u16)ctx->rx_buf->buffer[dlen + 1] << 8));

    if (ctx->rx_crc != rx_fcs) {
        LOG_WRN("rx: CRC Error! Calc: 0x%04X, RX: 0x%04X", ctx->rx_crc, rx_fcs);
        goto reset;
    }

    atc_hdlc_u8 address = ctx->rx_buf->buffer[0];
    atc_hdlc_u8 ctrl = ctx->rx_buf->buffer[1];
    const atc_hdlc_u8* info = NULL;
    atc_hdlc_u16 info_len = 0;

    LOG_DBG("rx: Valid frame (Addr: 0x%02X, Ctrl: 0x%02X, Len: %u)", address, ctrl, dlen);

    if (dlen > ADDR_LEN + CTRL_LEN) {
        info = &ctx->rx_buf->buffer[ADDR_LEN + CTRL_LEN];
        info_len = (atc_hdlc_u16)(dlen - (ADDR_LEN + CTRL_LEN));
    }
    dispatch_frame(ctx, address, ctrl, info, info_len);

reset:
    ctx->rx_state = RX_ADDR;
    ctx->rx_index = 0;
    ctx->rx_crc = ATC_HDLC_FCS_INIT_VALUE;
}

static void rx_byte(atc_hdlc_ctx_t* ctx, atc_hdlc_u8 byte) {
    if (byte == FLAG) {
        handle_flag(ctx);
        return;
    }
    if (ctx->rx_state == RX_HUNT)
        return;
    if (byte == ESC) {
        ctx->rx_state = RX_ESC;
        return;
    }
    if (ctx->rx_state == RX_ESC) {
        byte = (atc_hdlc_u8)(byte ^ XOR_MASK);
        ctx->rx_state = RX_DATA;
    }

    if (ctx->rx_index >= ctx->rx_buf->capacity) {
        LOG_WRN("rx: Buffer overflow! Max %u bytes. Discarding.", ctx->rx_buf->capacity);
        goto discard;
    }

    ctx->rx_buf->buffer[ctx->rx_index] = byte;
    if (ctx->rx_index >= FCS_LEN)
        ctx->rx_crc = ctx->crc->compute(ctx->rx_crc, &ctx->rx_buf->buffer[ctx->rx_index - FCS_LEN], 1);
    ctx->rx_index++;

    if (ctx->rx_index == 1) {
        if (byte != ctx->my_address && byte != ctx->peer_address && byte != ATC_HDLC_BROADCAST_ADDRESS) {
            LOG_WRN("rx: Invalid Address 0x%02X. Frame discarded.", byte);
            goto discard;
        }
        ctx->rx_state = RX_DATA;
    }
    return;

discard:
    ctx->rx_state = RX_HUNT;
    ctx->rx_index = 0;
    ctx->rx_crc = ATC_HDLC_FCS_INIT_VALUE;
}

void atc_hdlc_data_in(atc_hdlc_ctx_t* ctx, const atc_hdlc_u8* data, atc_hdlc_u32 len) {
    if (!ctx || !data)
        return;
    for (atc_hdlc_u32 i = 0; i < len; i++)
        rx_byte(ctx, data[i]);
    if (CTX_FLAG(ctx, HDLC_F_RETRANSMIT_PENDING))
        retransmit_outstanding(ctx);
}
