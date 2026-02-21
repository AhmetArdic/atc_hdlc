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
void hdlc_init(hdlc_context_t *ctx, hdlc_u8 *input_buffer, hdlc_u32 input_buffer_len,
                      hdlc_u8 *retransmit_buffer, hdlc_u32 retransmit_buffer_len,
                      hdlc_u32 retransmit_timeout_ms,
                      hdlc_u8 window_size,
                      hdlc_output_byte_cb_t output_cb,
                      hdlc_on_frame_cb_t on_frame_cb,
                      hdlc_on_state_change_cb_t on_state_change_cb,
                      void *user_data) {
  if (ctx == NULL || input_buffer == NULL || input_buffer_len < HDLC_MIN_FRAME_LEN) {
    return;
  }

  // Clamp window_size to valid range [1, 7]
  if (window_size < 1) window_size = 1;
  if (window_size > 7) window_size = 7;

  // Clear context
  memset(ctx, 0, sizeof(hdlc_context_t));

  // Initialize Buffer
  ctx->input_buffer = input_buffer;
  ctx->input_buffer_len = input_buffer_len;
  
  // Initialize Retransmit Buffer (slotted for Go-Back-N)
  ctx->retransmit_buffer = retransmit_buffer;
  ctx->retransmit_buffer_len = retransmit_buffer_len;
  ctx->window_size = window_size;
  if (retransmit_buffer != NULL && retransmit_buffer_len > 0 && window_size > 0) {
      ctx->retransmit_slot_size = retransmit_buffer_len / window_size;
  }

  // Bind callbacks
  ctx->output_byte_cb = output_cb;
  ctx->on_frame_cb = on_frame_cb;
  ctx->on_state_change_cb = on_state_change_cb;
  ctx->user_data = user_data;

  // Initialize State
  ctx->input_state = HDLC_INPUT_STATE_HUNT;
  ctx->current_state = HDLC_PROTOCOL_STATE_DISCONNECTED;
  
  // Reliable State (Go-Back-N)
  ctx->vs = 0;
  ctx->vr = 0;
  ctx->va = 0;
  ctx->ack_pending = false;
  ctx->retransmit_timeout_ms = retransmit_timeout_ms;
}

/**
 * @brief Configure the local and peer addresses.
 * @see hdlc.h
 */
void hdlc_configure_addresses(hdlc_context_t *ctx, hdlc_u8 my_addr, hdlc_u8 peer_addr) {
  if (ctx) {
    ctx->my_address = my_addr;
    ctx->peer_address = peer_addr;
  }
}

/**
 * @brief Initiate a Logical Connection (SABM).
 * @see hdlc.h
 */
bool hdlc_connect(hdlc_context_t *ctx) {
  if (ctx == NULL) return false;

  // Send SABM
  HDLC_LOG_DEBUG("tx: Sending SABM to peer 0x%02X", ctx->peer_address);
  hdlc_control_t ctrl = hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_SABM, HDLC_U_MODIFIER_HI_SABM, 1); // P=1
  hdlc_output_frame_start(ctx, ctx->peer_address, ctrl.value);
  hdlc_output_frame_end(ctx);

  hdlc_set_protocol_state(ctx, HDLC_PROTOCOL_STATE_CONNECTING);
  return true;
}

/**
 * @brief Terminate a Logical Connection (DISC).
 * @see hdlc.h
 */
bool hdlc_disconnect(hdlc_context_t *ctx) {
  if (ctx == NULL) return false;

  // Send DISC
  HDLC_LOG_DEBUG("tx: Sending DISC to peer 0x%02X", ctx->peer_address);
  hdlc_control_t ctrl = hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_DISC, HDLC_U_MODIFIER_HI_DISC, 1); // P=1
  hdlc_output_frame_start(ctx, ctx->peer_address, ctrl.value);
  hdlc_output_frame_end(ctx);

  hdlc_set_protocol_state(ctx, HDLC_PROTOCOL_STATE_DISCONNECTING);
  return true;
}

/**
 * @brief Check if Connected.
 * @see hdlc.h
 */
bool hdlc_is_connected(hdlc_context_t *ctx) {
  return (ctx != NULL && ctx->current_state == HDLC_PROTOCOL_STATE_CONNECTED);
}

/**
 * @brief Periodic Tick for Timers.
 * @see hdlc.h
 */
void hdlc_tick(hdlc_context_t *ctx, hdlc_u32 delta_ms) {
    if (ctx == NULL) return;
    
    // Retransmission Timer (only if frames are outstanding)
    if (ctx->va != ctx->vs) {
        if (ctx->retransmit_timer_ms > 0) {
            if (ctx->retransmit_timer_ms > delta_ms) {
                ctx->retransmit_timer_ms -= delta_ms;
            } else {
                ctx->retransmit_timer_ms = 0;
            }

            if (ctx->retransmit_timer_ms == 0) {
                // Timeout! Go-Back-N: Retransmit ALL frames from V(A) to V(S)-1.
                HDLC_LOG_WARN("tx: Retransmit Timeout! Go-Back-N from V(A)=%u to V(S)=%u", ctx->va, ctx->vs);
                hdlc_u8 seq = ctx->va;
                while (seq != ctx->vs) {
                    hdlc_u8 slot = seq % ctx->window_size;
                    hdlc_u8 pf = (seq == ctx->va) ? 1 : 0; // P=1 on first frame
                    hdlc_control_t ctrl = hdlc_create_i_ctrl(seq, ctx->vr, pf);
                    hdlc_output_frame_start(ctx, ctx->peer_address, ctrl.value);
                    
                    if (ctx->retransmit_buffer != NULL && ctx->retransmit_lens[slot] > 0) {
                        hdlc_output_frame_information_bytes(ctx,
                            ctx->retransmit_buffer + (slot * ctx->retransmit_slot_size),
                            ctx->retransmit_lens[slot]);
                    }
                    
                    hdlc_output_frame_end(ctx);
                    seq = (seq + 1) % HDLC_SEQUENCE_MODULUS;
                }
                
                // Restart Timer
                ctx->retransmit_timer_ms = ctx->retransmit_timeout_ms;
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
void hdlc_set_protocol_state(hdlc_context_t *ctx, hdlc_protocol_state_t new_state) {
  if (ctx->current_state != new_state) {
    HDLC_LOG_DEBUG("state: changed %d -> %d", ctx->current_state, new_state);
    ctx->current_state = new_state;
    if (ctx->on_state_change_cb != NULL) {
      ctx->on_state_change_cb(new_state, ctx->user_data);
    }
  }
}
