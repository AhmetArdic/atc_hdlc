/*
 * Copyright (C) 2026 Ahmet Talha ARDIC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../../inc/hdlc.h"
#include "../hdlc_private.h"
#include <string.h>

static void hdlc_fire_event(atc_hdlc_context_t *ctx, atc_hdlc_event_t event);

atc_hdlc_error_t atc_hdlc_init(atc_hdlc_context_t        *ctx,
                                  const atc_hdlc_config_t   *config,
                                  const atc_hdlc_platform_t *platform,
                                  atc_hdlc_tx_window_t      *tx_window,
                                  atc_hdlc_rx_buffer_t      *rx_buf) {
    if (ctx == NULL || config == NULL || platform == NULL || rx_buf == NULL) {
        return ATC_HDLC_ERR_INVALID_PARAM;
    }
    if (platform->on_send == NULL) {
        return ATC_HDLC_ERR_INVALID_PARAM;
    }
    if (rx_buf->buffer == NULL) {
        return ATC_HDLC_ERR_INVALID_PARAM;
    }

    if (config->mode != ATC_HDLC_MODE_ABM) {
        return ATC_HDLC_ERR_UNSUPPORTED_MODE;
    }
    if (config->use_extended) {
        return ATC_HDLC_ERR_UNSUPPORTED_MODE;
    }

    if (config->window_size < 1 || config->window_size > 7) {
        return ATC_HDLC_ERR_INVALID_PARAM;
    }

    atc_hdlc_u32 min_rx_cap = config->max_frame_size +
                               HDLC_ADDRESS_LEN + HDLC_CONTROL_LEN + HDLC_FCS_LEN;
    if (rx_buf->capacity < min_rx_cap) {
        return ATC_HDLC_ERR_INCONSISTENT_BUFFER;
    }
    if (rx_buf->capacity < HDLC_MIN_FRAME_LEN) {
        return ATC_HDLC_ERR_INCONSISTENT_BUFFER;
    }

    if (tx_window != NULL) {
        if (tx_window->slots == NULL || tx_window->slot_lens == NULL ||
            tx_window->seq_to_slot == NULL) {
            return ATC_HDLC_ERR_INCONSISTENT_BUFFER;
        }
        if (tx_window->slot_count != config->window_size) {
            return ATC_HDLC_ERR_INCONSISTENT_BUFFER;
        }
        if (tx_window->slot_capacity < config->max_frame_size) {
            return ATC_HDLC_ERR_INCONSISTENT_BUFFER;
        }
    }

    memset(ctx, 0, sizeof(atc_hdlc_context_t));

    ctx->config    = config;
    ctx->platform  = platform;
    ctx->tx_window = tx_window;
    ctx->rx_buf    = rx_buf;

    ctx->my_address = config->address;
    ctx->window_size = config->window_size;
    ctx->role = ATC_HDLC_ROLE_COMBINED;

    ctx->rx_state = HDLC_RX_STATE_HUNT;

    return ATC_HDLC_OK;
}

atc_hdlc_error_t atc_hdlc_link_setup(atc_hdlc_context_t *ctx,
                                       atc_hdlc_u8 peer_addr) {
    if (ctx == NULL) return ATC_HDLC_ERR_INVALID_PARAM;
    if (ctx->current_state != ATC_HDLC_STATE_DISCONNECTED) {
        return ATC_HDLC_ERR_INVALID_STATE;
    }

    ctx->peer_address = peer_addr;

    ATC_HDLC_LOG_DEBUG("tx: Sending SABM to peer 0x%02X", ctx->peer_address);
    hdlc_send_u_frame(ctx, ctx->peer_address, HDLC_U_CTRL(HDLC_U_SABM, 1));

    hdlc_t1_start(ctx);

    hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_CONNECTING,
                             ATC_HDLC_EVENT_LINK_SETUP_REQUEST);
    return ATC_HDLC_OK;
}

atc_hdlc_error_t atc_hdlc_disconnect(atc_hdlc_context_t *ctx) {
    if (ctx == NULL) return ATC_HDLC_ERR_INVALID_PARAM;
    if (ctx->current_state != ATC_HDLC_STATE_CONNECTED &&
        ctx->current_state != ATC_HDLC_STATE_FRMR_ERROR) {
        return ATC_HDLC_ERR_INVALID_STATE;
    }

    ATC_HDLC_LOG_DEBUG("tx: Sending DISC to peer 0x%02X", ctx->peer_address);
    hdlc_send_u_frame(ctx, ctx->peer_address, HDLC_U_CTRL(HDLC_U_DISC, 1));

    hdlc_t1_start(ctx);

    hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_DISCONNECTING,
                             ATC_HDLC_EVENT_DISCONNECT_REQUEST);
    return ATC_HDLC_OK;
}

atc_hdlc_error_t atc_hdlc_link_reset(atc_hdlc_context_t *ctx) {
    if (ctx == NULL) return ATC_HDLC_ERR_INVALID_PARAM;

    ATC_HDLC_LOG_DEBUG("state: Link reset initiated");
    hdlc_reset_connection_state(ctx);

    hdlc_send_u_frame(ctx, ctx->peer_address, HDLC_U_CTRL(HDLC_U_SABM, 1));
    hdlc_t1_start(ctx);
    hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_CONNECTING, ATC_HDLC_EVENT_RESET);

    return ATC_HDLC_OK;
}

atc_hdlc_bool atc_hdlc_is_connected(const atc_hdlc_context_t *ctx) {
    return (ctx != NULL && ctx->current_state == ATC_HDLC_STATE_CONNECTED);
}

atc_hdlc_error_t atc_hdlc_set_local_busy(atc_hdlc_context_t *ctx,
                                           atc_hdlc_bool busy) {
    if (ctx == NULL) return ATC_HDLC_ERR_INVALID_PARAM;
    if (ctx->current_state != ATC_HDLC_STATE_CONNECTED) {
        return ATC_HDLC_ERR_INVALID_STATE;
    }

    if (busy && !ctx->local_busy) {
        ctx->local_busy = true;
        HDLC_STAT_INC(ctx, local_busy_transitions);
        ATC_HDLC_LOG_DEBUG("flow: Local busy asserted");
    } else if (!busy && ctx->local_busy) {
        ctx->local_busy = false;
        hdlc_send_rr(ctx, 0);
        ATC_HDLC_LOG_DEBUG("flow: Local busy cleared, RR sent");
    }

    return ATC_HDLC_OK;
}

void atc_hdlc_t1_expired(atc_hdlc_context_t *ctx) {
    if (ctx == NULL) return;

    ctx->t1_active = false;

    const atc_hdlc_u8 max_retries = ctx->config ? ctx->config->max_retries : 0;
    ctx->retry_count++;
    HDLC_STAT_INC(ctx, timeout_count);

    if (max_retries > 0 && ctx->retry_count > max_retries) {
        ATC_HDLC_LOG_ERROR("tx: Link failure — N2 exceeded (state %d)",
                           ctx->current_state);
        hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_DISCONNECTED,
                                 ATC_HDLC_EVENT_LINK_FAILURE);
        hdlc_reset_connection_state(ctx);
        return;
    }

    switch (ctx->current_state) {
        case ATC_HDLC_STATE_CONNECTING:
            ATC_HDLC_LOG_WARN("tx: T1 expired in CONNECTING, retry SABM (%u/%u)",
                              ctx->retry_count, max_retries);
            hdlc_send_u_frame(ctx, ctx->peer_address, HDLC_U_CTRL(HDLC_U_SABM, 1));
            hdlc_t1_start(ctx);
            break;

        case ATC_HDLC_STATE_DISCONNECTING:
            ATC_HDLC_LOG_WARN("tx: T1 expired in DISCONNECTING, retry DISC (%u/%u)",
                              ctx->retry_count, max_retries);
            hdlc_send_u_frame(ctx, ctx->peer_address, HDLC_U_CTRL(HDLC_U_DISC, 1));
            hdlc_t1_start(ctx);
            break;

        case ATC_HDLC_STATE_CONNECTED:
            if (ctx->va != ctx->vs) {
                ATC_HDLC_LOG_WARN("tx: T1 expired, enquiry RR(P=1) (%u/%u)",
                                  ctx->retry_count, max_retries);
                hdlc_send_rr(ctx, 1);
                hdlc_t1_start(ctx);
            }
            break;

        default:
            break;
    }
}

void atc_hdlc_t2_expired(atc_hdlc_context_t *ctx) {
    if (ctx == NULL) return;

    ctx->t2_active = false;
    hdlc_send_rr(ctx, 0);
}

atc_hdlc_state_t atc_hdlc_get_state(const atc_hdlc_context_t *ctx) {
    if (ctx == NULL) return ATC_HDLC_STATE_DISCONNECTED;
    return ctx->current_state;
}

atc_hdlc_u8 atc_hdlc_get_window_available(const atc_hdlc_context_t *ctx) {
    if (ctx == NULL) return 0;
    atc_hdlc_u8 outstanding = (atc_hdlc_u8)((ctx->vs - ctx->va +
                               HDLC_SEQUENCE_MODULUS) % HDLC_SEQUENCE_MODULUS);
    if (outstanding >= ctx->window_size) return 0;
    return (atc_hdlc_u8)(ctx->window_size - outstanding);
}

atc_hdlc_bool atc_hdlc_has_pending_ack(const atc_hdlc_context_t *ctx) {
    if (ctx == NULL) return false;
    return ctx->t2_active;
}

void atc_hdlc_abort(atc_hdlc_context_t *ctx) {
    if (ctx == NULL) return;

    if (ctx->platform && ctx->platform->on_send) {
        ctx->platform->on_send(HDLC_FLAG, false, ctx->platform->user_ctx);
        ctx->platform->on_send(HDLC_FLAG, true,  ctx->platform->user_ctx);
    }

    hdlc_reset_connection_state(ctx);
    ctx->rx_state       = HDLC_RX_STATE_HUNT;
    ctx->current_state  = ATC_HDLC_STATE_DISCONNECTED;
}

void atc_hdlc_get_stats(const atc_hdlc_context_t *ctx, atc_hdlc_stats_t *out) {
    if (ctx == NULL || out == NULL) return;
    memcpy(out, &ctx->stats, sizeof(atc_hdlc_stats_t));
}

static void hdlc_fire_event(atc_hdlc_context_t *ctx, atc_hdlc_event_t event) {
    if (ctx->platform && ctx->platform->on_event) {
        ctx->platform->on_event(event, ctx->platform->user_ctx);
    }
}

void hdlc_set_protocol_state(atc_hdlc_context_t *ctx,
                               atc_hdlc_state_t    new_state,
                               atc_hdlc_event_t    event) {
    atc_hdlc_bool state_changed = (ctx->current_state != new_state);

    if (state_changed || event == ATC_HDLC_EVENT_INCOMING_CONNECT) {
        ATC_HDLC_LOG_DEBUG("state: %d -> %d (event: %d)",
                           ctx->current_state, new_state, event);
        ctx->current_state = new_state;

        hdlc_fire_event(ctx, event);
    }
}
