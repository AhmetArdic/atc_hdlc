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
 * @file hdlc.c
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief HDLC Core — Initialization, Configuration, and Timers.
 *
 * Contains the library entry points: context initialization,
 * address configuration, connection management, and the periodic
 * tick handler for retransmission timers.
 *
 * Frame I/O, serialization, and processing are in separate modules:
 *   - hdlc_input.c          (Receive parser)
 *   - hdlc_output.c         (Frame output / streaming API)
 *   - hdlc_frame.c          (Pack / Unpack)
 *   - hdlc_frame_handlers.c (I/S/U frame processing)
 */

#include "../inc/hdlc.h"
#include "hdlc_private.h"
#include <string.h>

/*
 * --------------------------------------------------------------------------
 * PUBLIC API
 * --------------------------------------------------------------------------
 */

/**
 * @brief Initialize the HDLC Context.
 * @see hdlc.h
 */
void atc_hdlc_init(atc_hdlc_context_t *ctx, atc_hdlc_u8 *input_buffer, atc_hdlc_u32 input_buffer_len,
                      atc_hdlc_u8 *retransmit_buffer, atc_hdlc_u32 retransmit_buffer_len,
                      atc_hdlc_u32 retransmit_timeout,
                      atc_hdlc_u32 ack_delay_timeout,
                      atc_hdlc_u8 window_size,
                      atc_hdlc_u8 max_retry_count,
                      atc_hdlc_output_byte_cb_t output_cb,
                      atc_hdlc_on_frame_cb_t on_frame_cb,
                      atc_hdlc_on_state_change_cb_t on_state_change_cb,
                      void *user_data) {
  if (ctx == NULL || input_buffer == NULL || input_buffer_len < ATC_HDLC_MIN_FRAME_LEN) {
    return;
  }

  // Clamp window_size to valid range [1, 7]
  if (window_size < 1) window_size = 1;
  if (window_size > 7) window_size = 7;

  // Clear context
  memset(ctx, 0, sizeof(atc_hdlc_context_t));

  // Bind callbacks
  ctx->output_byte_cb = output_cb;
  ctx->on_frame_cb = on_frame_cb;
  ctx->on_state_change_cb = on_state_change_cb;
  ctx->user_data = user_data;

  // Initialize Buffer
  ctx->input_buffer = input_buffer;
  ctx->input_buffer_len = input_buffer_len;
  
  // Initialize Retransmit Buffer (slotted for Go-Back-N)
  ctx->retransmit_buffer = retransmit_buffer;
  ctx->retransmit_buffer_len = retransmit_buffer_len;
  ctx->window_size = window_size;
  if (retransmit_buffer != NULL && retransmit_buffer_len > 0) {
      ctx->retransmit_slot_size = retransmit_buffer_len / window_size;
  }

  // Initialize Non-Zero Configuration
  ctx->input_state = HDLC_INPUT_STATE_HUNT;
  ctx->ack_delay_timeout = ack_delay_timeout;
  ctx->retransmit_timeout = retransmit_timeout;
  ctx->max_retry_count = max_retry_count;
}

/**
 * @brief Configure the local and peer addresses.
 * @see hdlc.h
 */
void atc_hdlc_configure_station(atc_hdlc_context_t *ctx, atc_hdlc_station_role_t role, atc_hdlc_link_mode_t mode, atc_hdlc_u8 my_addr, atc_hdlc_u8 peer_addr) {
    if (ctx) {
        ctx->role = role;
        ctx->mode = mode;
        ctx->my_address = my_addr;
        ctx->peer_address = peer_addr;
    }
}

/**
 * @brief Initiate a Logical Connection (SABM).
 * @see hdlc.h
 */
atc_hdlc_bool atc_hdlc_link_setup(atc_hdlc_context_t *ctx) {
  if (ctx == NULL) return false;

  // Currently, we only support ABM (Asynchronous Balanced Mode) via SABM
  if (ctx->mode != ATC_HDLC_MODE_ABM) {
      return false; // NRM/ARM not yet implemented
  }

  // Send SABM
  ATC_HDLC_LOG_DEBUG("tx: Sending SABM to peer 0x%02X", ctx->peer_address);
  hdlc_send_u_frame(ctx, ctx->peer_address, HDLC_U_MODIFIER_LO_SABM, HDLC_U_MODIFIER_HI_SABM, 1); // P=1

  hdlc_set_protocol_state(ctx, ATC_HDLC_PROTOCOL_STATE_CONNECTING, ATC_HDLC_EVENT_LINK_SETUP_REQUEST);
  return true;
}

/**
 * @brief Terminate a Logical Connection (DISC).
 * @see hdlc.h
 */
atc_hdlc_bool atc_hdlc_disconnect(atc_hdlc_context_t *ctx) {
  if (ctx == NULL) return false;

  // Send DISC
  ATC_HDLC_LOG_DEBUG("tx: Sending DISC to peer 0x%02X", ctx->peer_address);
  hdlc_send_u_frame(ctx, ctx->peer_address, HDLC_U_MODIFIER_LO_DISC, HDLC_U_MODIFIER_HI_DISC, 1); // P=1

  hdlc_set_protocol_state(ctx, ATC_HDLC_PROTOCOL_STATE_DISCONNECTING, ATC_HDLC_EVENT_DISCONNECT_REQUEST);
  return true;
}

/**
 * @brief Check if Connected.
 * @see hdlc.h
 */
atc_hdlc_bool atc_hdlc_is_connected(atc_hdlc_context_t *ctx) {
  return (ctx != NULL && ctx->current_state == ATC_HDLC_PROTOCOL_STATE_CONNECTED);
}

/**
 * @brief Periodic Tick for Timers.
 * @see hdlc.h
 */
void atc_hdlc_tick(atc_hdlc_context_t *ctx) {
    if (ctx == NULL) return;

    // Contention Timer for SABM collision resolution
    if (ctx->current_state == ATC_HDLC_PROTOCOL_STATE_CONNECTING) {
        if (ctx->contention_timer > 0) {
            ctx->contention_timer--;
            if (ctx->contention_timer == 0) {
                ATC_HDLC_LOG_DEBUG("tx: Contention resolved (timer expired). Retrying SABM.");
                atc_hdlc_link_setup(ctx);
            }
        }
    }

    // Delayed ACK: Flush pending acknowledgement after T2 expires
    if (ctx->ack_timer > 0) {
        ctx->ack_timer--;
        if (ctx->ack_timer == 0) {
            hdlc_send_rr(ctx, 0);
        }
    }

    // Retransmission Timer (only if frames are outstanding)
    if (ctx->va != ctx->vs) {
        if (ctx->retransmit_timer > 0) {
            ctx->retransmit_timer--;

            if (ctx->retransmit_timer == 0) {
                ctx->retry_count++;
                
                if (ctx->max_retry_count > 0 && ctx->retry_count > ctx->max_retry_count) {
                    ATC_HDLC_LOG_ERROR("tx: Link Failure! Max retransmission limit reached.");
                    ATC_HDLC_LOG_DEBUG("tx: State before reset -> V(S)=%u, V(R)=%u, V(A)=%u", ctx->vs, ctx->vr, ctx->va);
                    
                    /* 1. Veri aktarimi durumu durdurulmali */
                    hdlc_set_protocol_state(ctx, ATC_HDLC_PROTOCOL_STATE_DISCONNECTED, ATC_HDLC_EVENT_LINK_FAILURE);
                    
                    /* 2. Gonderim, alim degiskenleri ve tamponlar sifirlanmali */
                    hdlc_reset_connection_state(ctx);
                } else {
                    /* Timeout! Send Enquiry (RR with P=1) to poll receiver status */
                    ATC_HDLC_LOG_WARN("tx: Retransmit Timeout! Sending Enquiry RR(P=1) (Retry %u/%u)", ctx->retry_count, ctx->max_retry_count);
                    
                    hdlc_send_rr(ctx, 1);
                    
                    /* Restart Timer expecting a response (F=1) */
                    ctx->retransmit_timer = ctx->retransmit_timeout;
                }
            }
        }
    }
}

/*
 * --------------------------------------------------------------------------
 * INTERNAL HELPERS
 * --------------------------------------------------------------------------
 */

/**
 * @brief Helper to update the HDLC Protocol State and trigger the callback.
 * @param ctx       HDLC Context.
 * @param new_state New Protocol State to transition to.
 */
void hdlc_set_protocol_state(atc_hdlc_context_t *ctx, atc_hdlc_protocol_state_t new_state, atc_hdlc_event_t event) {
  bool state_changed = (ctx->current_state != new_state);
  
  /* Always notify if the state actually changes, OR if the link is being reset 
     while already connected (e.g., peer restarted and sent a new SABM). */
  if (state_changed || event == ATC_HDLC_EVENT_INCOMING_CONNECT) {
    ATC_HDLC_LOG_DEBUG("state: changed %d -> %d (event: %d)", ctx->current_state, new_state, event);
    ctx->current_state = new_state;
    if (ctx->on_state_change_cb != NULL) {
      ctx->on_state_change_cb(new_state, event, ctx->user_data);
    }
  }
}
