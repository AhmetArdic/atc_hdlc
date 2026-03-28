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

#if ATC_HDLC_LOG_LEVEL >= ATC_HDLC_LOG_LEVEL_ERR
#define LOG_ERR(fmt, ...) ATC_HDLC_LOG_IMPL("ERR", fmt, ##__VA_ARGS__)
#else
#define LOG_ERR(fmt, ...)
#endif
#if ATC_HDLC_LOG_LEVEL >= ATC_HDLC_LOG_LEVEL_WRN
#define LOG_WRN(fmt, ...) ATC_HDLC_LOG_IMPL("WRN", fmt, ##__VA_ARGS__)
#else
#define LOG_WRN(fmt, ...)
#endif
#if ATC_HDLC_LOG_LEVEL >= ATC_HDLC_LOG_LEVEL_INFO
#define LOG_INFO(fmt, ...) ATC_HDLC_LOG_IMPL("INFO", fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...)
#endif
#if ATC_HDLC_LOG_LEVEL >= ATC_HDLC_LOG_LEVEL_DBG
#define LOG_DBG(fmt, ...) ATC_HDLC_LOG_IMPL("DBG", fmt, ##__VA_ARGS__)
#else
#define LOG_DBG(fmt, ...)
#endif

void set_state(atc_hdlc_context_t* ctx, atc_hdlc_state_t new_state, atc_hdlc_event_t event);

void reset_state(atc_hdlc_context_t* ctx);

void dispatch_frame(atc_hdlc_context_t* ctx, atc_hdlc_u8 address, atc_hdlc_u8 ctrl,
                    const atc_hdlc_u8* info, atc_hdlc_u16 info_len);

static inline void t1_start(atc_hdlc_context_t* ctx) {
    if (ctx->platform->t1_start && ctx->config)
        ctx->platform->t1_start(ctx->config->t1_ms, ctx->platform->user_ctx);
    CTX_SET(ctx, HDLC_F_T1_ACTIVE);
}

static inline void t1_stop(atc_hdlc_context_t* ctx) {
    if (CTX_FLAG(ctx, HDLC_F_T1_ACTIVE) && ctx->platform->t1_stop)
        ctx->platform->t1_stop(ctx->platform->user_ctx);
    CTX_CLR(ctx, HDLC_F_T1_ACTIVE);
}

static inline void t2_start(atc_hdlc_context_t* ctx) {
    if (ctx->platform->t2_start && ctx->config)
        ctx->platform->t2_start(ctx->config->t2_ms, ctx->platform->user_ctx);
    CTX_SET(ctx, HDLC_F_T2_ACTIVE);
}

static inline void t2_stop(atc_hdlc_context_t* ctx) {
    if (CTX_FLAG(ctx, HDLC_F_T2_ACTIVE) && ctx->platform->t2_stop)
        ctx->platform->t2_stop(ctx->platform->user_ctx);
    CTX_CLR(ctx, HDLC_F_T2_ACTIVE);
}

#endif /* ATC_HDLC_PRIVATE_H */
