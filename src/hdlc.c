/**
 * @file hdlc.c
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief Main Implementation of the HDLC Protocol Stack.
 *
 * Contains the logic for Frame Transmission (Buffered & Zero-Copy),
 * Frame Reception (State Machine, Byte Stuffing removal), and
 * CRC Verification.
 */

#include "../inc/hdlc.h"
#include "hdlc_crc.h"
#include "hdlc_private.h"
#include <string.h>

/** @brief HDLC Flag Sequence (0x7E) used to delimit frames. */
#define HDLC_FLAG 0x7E
/** @brief HDLC Escape Octet (0x7D) used for transparency. */
#define HDLC_ESCAPE 0x7D
/** @brief Bit-mask (0x20) XORed with octets to be escaped. */
#define HDLC_XOR_MASK 0x20

/**
 * @brief Initialize the HDLC Context.
 * @see hdlc.h
 */
void hdlc_init(hdlc_context_t *ctx, hdlc_u8 *input_buffer, hdlc_u32 input_buffer_len,
                      hdlc_u8 *retransmit_buffer, hdlc_u32 retransmit_buffer_len,
                      hdlc_output_byte_cb_t output_cb,
                      hdlc_on_frame_cb_t on_frame_cb,
                      hdlc_on_state_change_cb_t on_state_change_cb,
                      void *user_data) {
  if (ctx == NULL || input_buffer == NULL || input_buffer_len < HDLC_MIN_FRAME_LEN) {
    return;
  }

  // Clear context
  memset(ctx, 0, sizeof(hdlc_context_t));

  // Initialize Buffer
  ctx->input_buffer = input_buffer;
  ctx->input_buffer_len = input_buffer_len;
  
  // Initialize Retransmit Buffer
  ctx->retransmit_buffer = retransmit_buffer;
  ctx->retransmit_buffer_len = retransmit_buffer_len;
  ctx->retransmit_len = 0;

  // Bind callbacks
  ctx->output_byte_cb = output_cb;
  ctx->on_frame_cb = on_frame_cb;
  ctx->on_state_change_cb = on_state_change_cb;
  ctx->user_data = user_data;

  // Initialize State
  ctx->input_state = HDLC_INPUT_STATE_HUNT;
  ctx->current_state = HDLC_PROTOCOL_STATE_DISCONNECTED;
  
  // Reliable State
  ctx->vs = 0;
  ctx->vr = 0;
  ctx->ack_pending = false;
  ctx->waiting_for_ack = false;
}

/*
 * --------------------------------------------------------------------------
 * PRIVATE HELPER DECLARATIONS
 * --------------------------------------------------------------------------
 */

// U-Frame Transmission Helpers
static inline void hdlc_send_u_frame(hdlc_context_t *ctx, hdlc_u8 address, hdlc_u8 m_lo, hdlc_u8 m_hi, hdlc_u8 pf);
static inline void hdlc_send_ua(hdlc_context_t *ctx, hdlc_u8 pf);
static inline void hdlc_send_dm(hdlc_context_t *ctx, hdlc_u8 pf);

// S-Frame Transmission Helpers
static inline void hdlc_send_s_frame(hdlc_context_t *ctx, hdlc_u8 address, hdlc_u8 s_bits, hdlc_u8 nr, hdlc_u8 pf);
static inline void hdlc_send_rr(hdlc_context_t *ctx, hdlc_u8 pf);
static inline void hdlc_send_rej(hdlc_context_t *ctx, hdlc_u8 pf);

// U-Frame Processing Helpers
static void hdlc_process_sabm(hdlc_context_t *ctx, const hdlc_frame_t *frame);
static void hdlc_process_snrm(hdlc_context_t *ctx, const hdlc_frame_t *frame);
static void hdlc_process_sarm(hdlc_context_t *ctx, const hdlc_frame_t *frame);
static void hdlc_process_disc(hdlc_context_t *ctx, const hdlc_frame_t *frame);
static void hdlc_process_ua(hdlc_context_t *ctx, const hdlc_frame_t *frame);
static void hdlc_process_dm(hdlc_context_t *ctx, const hdlc_frame_t *frame);
static void hdlc_process_frmr(hdlc_context_t *ctx, const hdlc_frame_t *frame);
static void hdlc_process_ui(hdlc_context_t *ctx, const hdlc_frame_t *frame);
static void hdlc_process_test(hdlc_context_t *ctx, const hdlc_frame_t *frame);

/*
 * --------------------------------------------------------------------------
 * SHARED ENCODING LOGIC
 * --------------------------------------------------------------------------
 */

typedef struct {
  hdlc_context_t *ctx;  // For callback-based TX
  hdlc_u8 *buffer;      // For buffer-based TX
  hdlc_u32 buffer_len;  // Max buffer length
  hdlc_u32 current_len; // Current bytes written to buffer
  hdlc_bool success;    // Used for buffer overflow check
} hdlc_encode_ctx_t;

typedef void (*hdlc_put_byte_fn)(hdlc_encode_ctx_t *enc_ctx, hdlc_u8 byte,
                                 hdlc_bool flush);

/**
 * @brief Helper to write byte to hardware callback.
 */
static void output_byte_to_callback(hdlc_encode_ctx_t *enc_ctx, hdlc_u8 byte,
                            hdlc_bool flush) {
  if (enc_ctx->ctx && enc_ctx->ctx->output_byte_cb) {
    enc_ctx->ctx->output_byte_cb(byte, flush, enc_ctx->ctx->user_data);
  }
}

/**
 * @brief Helper to write byte to memory buffer.
 */
static void output_byte_to_buffer(hdlc_encode_ctx_t *enc_ctx, hdlc_u8 byte,
                            hdlc_bool flush) {
  (void)flush;
  if (enc_ctx->current_len < enc_ctx->buffer_len) {
    enc_ctx->buffer[enc_ctx->current_len++] = byte;
  } else {
    enc_ctx->success = false;
  }
}

/**
 * @brief Helper to write a byte using the abstract put_fn.
 * @param ctx       Encoding Context.
 * @param put_fn    Function pointer to write byte.
 * @param byte      Byte to write.
 * @param flush     Flush flag.
 */
static inline void pack_byte(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn,
                               hdlc_u8 byte, hdlc_bool flush) {
  put_fn(ctx, byte, flush);
}

/**
 * @brief Write a byte with HDLC escaping if needed.
 * @param ctx       Encoding Context.
 * @param put_fn    Function pointer to write byte.
 * @param byte      Byte to write.
 */
static void pack_escaped(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn,
                           hdlc_u8 byte) {
  if (byte == HDLC_FLAG || byte == HDLC_ESCAPE) {
    pack_byte(ctx, put_fn, HDLC_ESCAPE, false);
    pack_byte(ctx, put_fn, byte ^ HDLC_XOR_MASK, false);
  } else {
    pack_byte(ctx, put_fn, byte, false);
  }
}

/**
 * @brief Update CRC and write byte (escaped) to output.
 * @param ctx       Encoding Context.
 * @param put_fn    Function pointer to write byte.
 * @param byte      Byte to write.
 * @param crc       Pointer to CRC to update.
 */
static void pack_escaped_crc_update(hdlc_encode_ctx_t *ctx,
                                      hdlc_put_byte_fn put_fn, hdlc_u8 byte,
                                      hdlc_u16 *crc) {
  *crc = hdlc_crc_ccitt_update(*crc, byte);
  pack_escaped(ctx, put_fn, byte);
}

/**
 * @brief Core logic for serializing an HDLC frame.
 * @param frame     Frame to serialize.
 * @param put_fn    Function to write bytes (to buffer or stream).
 * @param enc_ctx   Encoding Context.
 * @return true on success, false on error (e.g. buffer overflow).
 */
static hdlc_bool frame_pack_core(const hdlc_frame_t *frame,
                                  hdlc_put_byte_fn put_fn,
                                  hdlc_encode_ctx_t *enc_ctx) {
  hdlc_u16 crc = HDLC_FCS_INIT_VALUE;

  // Start Flag
  pack_byte(enc_ctx, put_fn, HDLC_FLAG, false);
  if (!enc_ctx->success)
    return false;

  // Address
  pack_escaped_crc_update(enc_ctx, put_fn, frame->address, &crc);
  if (!enc_ctx->success)
    return false;

  // Control
  pack_escaped_crc_update(enc_ctx, put_fn, frame->control.value, &crc);
  if (!enc_ctx->success)
    return false;

  // Payload
  if (frame->information != NULL && frame->information_len > 0) {
    for (hdlc_u16 i = 0; i < frame->information_len; i++) {
      pack_escaped_crc_update(enc_ctx, put_fn, frame->information[i], &crc);
      if (!enc_ctx->success)
        return false;
    }
  }

  // FCS
  hdlc_u8 fcs_hi = (crc >> 8) & 0xFF;
  hdlc_u8 fcs_lo = crc & 0xFF;

  pack_escaped(enc_ctx, put_fn, fcs_hi);
  if (!enc_ctx->success)
    return false;

  pack_escaped(enc_ctx, put_fn, fcs_lo);
  if (!enc_ctx->success)
    return false;

  // End Flag
  pack_byte(enc_ctx, put_fn, HDLC_FLAG, true);

  return enc_ctx->success;
}

/**
 * @brief Output a complete HDLC Frame.
 * @see hdlc.h
 */
void hdlc_output_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  if (ctx == NULL || frame == NULL) {
    return;
  }

  hdlc_encode_ctx_t enc_ctx = {.ctx = ctx,
                               .buffer = NULL,
                               .buffer_len = 0,
                               .current_len = 0,
                               .success = true};

  frame_pack_core(frame, output_byte_to_callback, &enc_ctx);
  ctx->stats_output_frames++;
}

/**
 * @brief Pack (Serialize) a frame into a memory buffer.
 * @see hdlc.h
 */
bool hdlc_frame_pack(const hdlc_frame_t *frame, hdlc_u8 *buffer,
                     hdlc_u32 buffer_len, hdlc_u32 *encoded_len) {
  if (frame == NULL || buffer == NULL || encoded_len == NULL) {
    return false;
  }

  hdlc_encode_ctx_t enc_ctx = {.ctx = NULL,
                               .buffer = buffer,
                               .buffer_len = buffer_len,
                               .current_len = 0,
                               .success = true};

  if (frame_pack_core(frame, output_byte_to_buffer, &enc_ctx)) {
    *encoded_len = enc_ctx.current_len;
    return true;
  }

  *encoded_len = 0;
  return false;
}

/**
 * @brief Unpack (Deserialize) a raw HDLC frame from a buffer.
 * @see hdlc.h
 */
bool hdlc_frame_unpack(const hdlc_u8 *buffer, hdlc_u32 buffer_len,
                       hdlc_frame_t *frame, hdlc_u8 *flat_buffer,
                       hdlc_u32 flat_buffer_len) {
  if (buffer == NULL || frame == NULL || flat_buffer == NULL) {
    return false;
  }

  // 1. Basic Length Check (Min: Flag + Addr + Ctrl + FCS + Flag = 6)
  // Actually, min on wire could be Flag + Addr + Ctrl + FCS + Flag = 6 bytes.
  // Or Flag + Addr + Ctrl + FCS (if implicit end).
  // Our HDLC_MIN_FRAME_LEN is 4 (bits inside).
  if (buffer_len < HDLC_MIN_FRAME_LEN + 2 * HDLC_FLAG_LEN) {
      // It might be just 1 flag if shared. Let's be safe.
     if (buffer_len < HDLC_MIN_FRAME_LEN) return false;
  }

  // 2. Parse and Unescape
  hdlc_u32 write_idx = 0;
  hdlc_bool inside_frame = false;
  hdlc_bool escape_next = false;
  hdlc_bool frame_complete = false;

  for (hdlc_u32 i = 0; i < buffer_len; i++) {
    hdlc_u8 byte = buffer[i];

    if (byte == HDLC_FLAG) {
      if (inside_frame) {
        if (write_idx >= HDLC_MIN_FRAME_LEN) {
           frame_complete = true;
           break; // Found End Flag
        } else {
           // Too short, maybe just consecutive flags or start flag
           write_idx = 0; // Reset
           continue; 
        }
      } else {
        inside_frame = true; // Found Start Flag
        write_idx = 0;
        continue;
      }
    }

    if (!inside_frame) continue;

    if (byte == HDLC_ESCAPE) {
      escape_next = true;
      continue;
    }

    if (escape_next) {
      byte ^= HDLC_XOR_MASK;
      escape_next = false;
    }

    if (write_idx < flat_buffer_len) {
      flat_buffer[write_idx++] = byte;
    } else {
      return false; // Buffer Overflow
    }
  }

  // If we didn't find an explicit End Flag but consumed valid data, 
  // we might check if the buffer itself was cut properly?
  // Usually decode assumes a full frame.
  if (!frame_complete) {
     // If the buffer didn't end with 7E, we can't be sure it's done?
     // Or maybe the user passed a buffer that *is* the frame.
     // Let's assume if we have enough data and valid CRC, it's OK?
     // But standard HDLC requires flags.
     // Let's rely on finding at least one start and one end, OR
     // if the loop finished and we satisfy min length, check CRC?
     if (write_idx < HDLC_MIN_FRAME_LEN) return false;
  }

  // 3. CRC Verification
  hdlc_u16 calced_crc = HDLC_FCS_INIT_VALUE;
  hdlc_u32 data_len = write_idx - HDLC_FCS_LEN; 

  for (hdlc_u32 i = 0; i < data_len; i++) {
    calced_crc = hdlc_crc_ccitt_update(calced_crc, flat_buffer[i]);
  }

  hdlc_fcs_t *fcs = (hdlc_fcs_t *)&flat_buffer[data_len];
  hdlc_u16 rx_fcs = (fcs->fcs[0] << 8) | fcs->fcs[1];

  if (calced_crc != rx_fcs) {
    return false;
  }

  // 4. Fill Frame Struct
  frame->address = flat_buffer[0];
  frame->control.value = flat_buffer[1];
  
  hdlc_u32 header_len = HDLC_ADDRESS_LEN + HDLC_CONTROL_LEN;
  if (data_len > header_len) {
    frame->information = &flat_buffer[header_len];
    frame->information_len = (hdlc_u16)(data_len - header_len);
  } else {
    frame->information = NULL;
    frame->information_len = 0;
  }

  // Resolve Type
  if ((frame->control.value & HDLC_FRAME_TYPE_MASK_I) == HDLC_FRAME_TYPE_VAL_I) {
      frame->type = HDLC_FRAME_I;
  } else if ((frame->control.value & HDLC_FRAME_TYPE_MASK_S) == HDLC_FRAME_TYPE_VAL_S) {
      frame->type = HDLC_FRAME_S;
  } else if ((frame->control.value & HDLC_FRAME_TYPE_MASK_U) == HDLC_FRAME_TYPE_VAL_U) {
      frame->type = HDLC_FRAME_U;
  } else {
      frame->type = HDLC_FRAME_INVALID;
  }

  return true;
}

/*
 * --------------------------------------------------------------------------
 * RECEIVE ENGINE
 * --------------------------------------------------------------------------
 */

/*
 * --------------------------------------------------------------------------
 * PROTOCOL LOGIC DISPATCHER
 * --------------------------------------------------------------------------
 */

/**
 * @brief Internal handler for Information (I) Frames.
 * @param ctx   HDLC Context.
 * @param frame Received frame.
 */
static void handle_i_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  hdlc_u8 msg_ns = (frame->control.i_frame.ns);
  hdlc_u8 msg_nr = (frame->control.i_frame.nr);
  hdlc_u8 msg_p  = (frame->control.i_frame.pf);

  // 1. Sequence Number Check (N(S) == V(R))
  if (msg_ns == ctx->vr) {
      // Correct Sequence
      ctx->vr = (ctx->vr + 1) % 8;
      ctx->ack_pending = true;
      
      // Deliver to User
      // Note: We modify the frame type to I_FRAME so user knows. 
      // It is already I_FRAME from dispatcher.
      // Dispatcher will call on_frame_cb AFTER this returns.
      // So we don't need to call it here.
  } else {
      // Sequence Error
      // Send REJ (Reject) immediately
      // Discard this frame (do not increment V(R))
      hdlc_send_rej(ctx, 0); // F=0 usually unless responding to P=1
      
      // If P=1, we must set F=1 in response.
      // But if we send REJ, that serves as response?
      return; 
  }

  // 2. Acknowledge Handling (Check Piggybacked N(R))
  // If we are waiting for an ACK for frame V(S)-1
  if (ctx->waiting_for_ack) {
      // Expected N(R) for success is V(S) (Peer expects next)
      if (msg_nr == ctx->vs) {
          // ACK Received!
          ctx->waiting_for_ack = false;
          ctx->retransmit_timer_ms = 0;
      }
  }
  
  // 3. Poll/Final Bit Handling
  if (msg_p) {
      // If P=1, we must respond with F=1.
      // We can send RR with F=1.
      hdlc_send_rr(ctx, 1);
      ctx->ack_pending = false; // Explicitly ACKed
  } 
  // Immediate ACK: Send RR when no piggybacked I-frame is pending.
  else if (ctx->ack_pending) {
       hdlc_send_rr(ctx, 0);
       ctx->ack_pending = false;
  }
}

/**
 * @brief Internal handler for Supervisory (S) Frames.
 * @param ctx   HDLC Context.
 * @param frame Received frame.
 */
static void handle_s_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  hdlc_u8 mode = (frame->control.s_frame.s);
  hdlc_u8 msg_nr = (frame->control.s_frame.nr);
  hdlc_u8 msg_pf = (frame->control.s_frame.pf);
  bool is_command = (frame->address == ctx->my_address); // Addressed to me = Command

  // 1. Process Receive Ready (RR) or Receive Not Ready (RNR)
  if (mode == 0 || mode == 1) { // RR=00, RNR=01
      // Check N(R) - Acknowledgment
      if (ctx->waiting_for_ack) {
          if (msg_nr == ctx->vs) {
               ctx->waiting_for_ack = false;
               ctx->retransmit_timer_ms = 0;
          }
      }
      // Note: RNR pause handling is not yet implemented.
  }
  // 2. Process Reject (REJ)
  else if (mode == 2) { // REJ=10
      // Peer is requesting retransmission starting from N(R).
      // If N(R) matches our unacked frame, retransmit immediately.
       if (ctx->waiting_for_ack) {
          if (msg_nr == ((ctx->vs - 1 + 8) % 8)) {
               // Force immediate retransmission via timer expiry.
               ctx->retransmit_timer_ms = 1; 
               hdlc_tick(ctx, 1);
          }
       }
  }

  // 3. Poll/Final Handling
  if (is_command && msg_pf) {
      // If Command with P=1, must respond with F=1.
      // Response with RR (if ready).
      hdlc_send_rr(ctx, 1);
  }
}

/**
 * @brief Helper to update the HDLC Protocol State and trigger the callback.
 * @param ctx       HDLC Context.
 * @param new_state New Protocol State to transition to.
 */
static void hdlc_set_protocol_state(hdlc_context_t *ctx, hdlc_protocol_state_t new_state) {
  if (ctx->current_state != new_state) {
    ctx->current_state = new_state;
    if (ctx->on_state_change_cb != NULL) {
      ctx->on_state_change_cb(new_state, ctx->user_data);
    }
  }
}

/*
 * --------------------------------------------------------------------------
 * U-FRAME HELPER IMPLEMENTATIONS
 * --------------------------------------------------------------------------
 */

/**
 * @brief Helper to send a Generic U-Frame.
 * @param ctx       HDLC Context.
 * @param address   Destination Address.
 * @param m_lo      Lower 2 bits of Modifier.
 * @param m_hi      Upper 3 bits of Modifier.
 * @param pf        Poll/Final bit.
 */
static inline void hdlc_send_u_frame(hdlc_context_t *ctx, hdlc_u8 address, hdlc_u8 m_lo, hdlc_u8 m_hi, hdlc_u8 pf) {
    hdlc_control_t ctrl = hdlc_create_u_ctrl(m_lo, m_hi, pf);
    hdlc_output_packet_start(ctx, address, ctrl.value);
    hdlc_output_packet_end(ctx);
}

/**
 * @brief Helper to send an Unnumbered Acknowledgment (UA).
 * @param ctx   HDLC Context.
 * @param pf    Final bit (matches Command's P bit).
 */
static inline void hdlc_send_ua(hdlc_context_t *ctx, hdlc_u8 pf) {
    // UA is a response, addressed from this station.
    hdlc_send_u_frame(ctx, ctx->my_address, HDLC_U_MODIFIER_LO_UA, HDLC_U_MODIFIER_HI_UA, pf);
}

/**
 * @brief Helper to send a Disconnected Mode (DM) response.
 * @param ctx   HDLC Context.
 * @param pf    Final bit (matches Command's P bit).
 */
static inline void hdlc_send_dm(hdlc_context_t *ctx, hdlc_u8 pf) {
    // Address = My Address (Response from Me)
    hdlc_send_u_frame(ctx, ctx->my_address, HDLC_U_MODIFIER_LO_DM, HDLC_U_MODIFIER_HI_DM, pf);
}


/**
 * @brief Send a Supervisory (S) Frame.
 * @param ctx     HDLC Context.
 * @param address Address field (Remote address).
 * @param s_bits  S-bits (RR, RNR, REJ).
 * @param nr      Receive Sequence Number N(R).
 * @param pf      Poll/Final bit.
 */
static inline void hdlc_send_s_frame(hdlc_context_t *ctx, hdlc_u8 address, hdlc_u8 s_bits, hdlc_u8 nr, hdlc_u8 pf) {
    hdlc_control_t ctrl = hdlc_create_s_ctrl(s_bits, nr, pf);
    hdlc_output_packet_start(ctx, address, ctrl.value);
    hdlc_output_packet_end(ctx);
}

/**
 * @brief Send a Receive Ready (RR) Frame.
 * 
 * Used to acknowledge I-frames when no I-frame is ready to piggyback (P=0/F=0)
 * or to respond to a poll (F=1).
 * 
 * @param ctx HDLC Context.
 * @param pf  Poll/Final bit.
 */
static inline void hdlc_send_rr(hdlc_context_t *ctx, hdlc_u8 pf) {
    // RR: S=00, addressed to peer.
    hdlc_send_s_frame(ctx, ctx->peer_address, 0, ctx->vr, pf);
}

/**
 * @brief Send a Reject (REJ) Frame.
 * @param ctx HDLC Context.
 * @param pf  Poll/Final bit.
 */
static inline void hdlc_send_rej(hdlc_context_t *ctx, hdlc_u8 pf) {
    // REJ: S=10 (2)
    hdlc_send_s_frame(ctx, ctx->peer_address, 2, ctx->vr, pf);
}

/**
 * @brief Process Received SABM Command.
 * @param ctx   HDLC Context.
 * @param frame The received SABM frame.
 */
static void hdlc_process_sabm(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    // Accept connection and transition to CONNECTED.
    hdlc_set_protocol_state(ctx, HDLC_PROTOCOL_STATE_CONNECTED);

    // Send UA (Response matches Command P bit with F bit)
    hdlc_send_ua(ctx, frame->control.u_frame.pf);
}

/**
 * @brief Process Received SNRM Command (Not Supported).
 * @param ctx   HDLC Context.
 * @param frame The received SNRM frame.
 */
static void hdlc_process_snrm(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    // SNRM Not Supported -> Send DM (Disconnected Mode)
    hdlc_send_dm(ctx, frame->control.u_frame.pf);
}

/**
 * @brief Process Received SARM Command (Not Supported).
 * @param ctx   HDLC Context.
 * @param frame The received SARM frame.
 */
static void hdlc_process_sarm(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    // SARM Not Supported -> Send DM (Disconnected Mode)
    hdlc_send_dm(ctx, frame->control.u_frame.pf);
}

/**
 * @brief Process Received DISC Command.
 * @param ctx   HDLC Context.
 * @param frame The received DISC frame.
 */
static void hdlc_process_disc(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    hdlc_set_protocol_state(ctx, HDLC_PROTOCOL_STATE_DISCONNECTED);

    // Send UA
    hdlc_send_ua(ctx, frame->control.u_frame.pf);
}

/**
 * @brief Process Received UA Response.
 * @param ctx   HDLC Context.
 * @param frame The received UA frame.
 */
static void hdlc_process_ua(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    (void)frame;
    if (ctx->current_state == HDLC_PROTOCOL_STATE_CONNECTING) {
        hdlc_set_protocol_state(ctx, HDLC_PROTOCOL_STATE_CONNECTED);
    } else if (ctx->current_state == HDLC_PROTOCOL_STATE_DISCONNECTING) {
        hdlc_set_protocol_state(ctx, HDLC_PROTOCOL_STATE_DISCONNECTED);
    }
}

/**
 * @brief Process Received DM Response.
 * @param ctx   HDLC Context.
 * @param frame The received DM frame.
 */
static void hdlc_process_dm(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    (void)frame;
    // Peer is disconnected. If we were trying to connect, we failed.
    hdlc_set_protocol_state(ctx, HDLC_PROTOCOL_STATE_DISCONNECTED);
}

/**
 * @brief Process Received FRMR Response.
 * @param ctx   HDLC Context.
 * @param frame The received FRMR frame.
 */
static void hdlc_process_frmr(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
   // Parsing FRMR payload for detailed error reporting
   if (frame->information_len >= HDLC_FRMR_INFO_MIN_LEN) {
       hdlc_frmr_data_t frmr_data;
       memset(&frmr_data, 0, sizeof(frmr_data));
       
       // Byte 0: Rejected Control Field
       frmr_data.rejected_control = frame->information[0];
       
       // Byte 1: 0 V(S) C/R V(R)
       hdlc_u8 byte1 = frame->information[1];
       frmr_data.v_s = (byte1 >> HDLC_FRMR_VS_SHIFT) & HDLC_FRMR_VS_MASK;
       frmr_data.cr  = (byte1 & HDLC_FRMR_CR_BIT) ? true : false;
       frmr_data.v_r = (byte1 >> HDLC_FRMR_VR_SHIFT) & HDLC_FRMR_VR_MASK;

       // Byte 2: W X Y Z V 0 0 0
       hdlc_u8 byte2 = frame->information[2];
       frmr_data.errors.w = (byte2 & HDLC_FRMR_W_BIT);
       frmr_data.errors.x = (byte2 & HDLC_FRMR_X_BIT);
       frmr_data.errors.y = (byte2 & HDLC_FRMR_Y_BIT);
       frmr_data.errors.z = (byte2 & HDLC_FRMR_Z_BIT);
       frmr_data.errors.v = (byte2 & HDLC_FRMR_V_BIT);
       
       (void)frmr_data; // Suppress unused warning
   }

   // Treat as fatal error -> Disconnect.
   hdlc_set_protocol_state(ctx, HDLC_PROTOCOL_STATE_DISCONNECTED);
}

/**
 * @brief Process Received UI Command.
 * @param ctx   HDLC Context.
 * @param frame The received UI frame.
 */
static void hdlc_process_ui(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    // UI frames are unacknowledged; delivered via on_frame_cb in process_complete_frame.
    (void)ctx;
    (void)frame;
}

/**
 * @brief Process Received TEST Command.
 *
 * Echoes back a TEST response with the same data payload.
 *
 * @param ctx   HDLC Context.
 * @param frame The received TEST frame.
 */
static void hdlc_process_test(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
    // Build TEST response: same modifier bits, F bit mirrors P bit
    hdlc_output_packet_start(ctx, ctx->my_address,
        hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_TEST, HDLC_U_MODIFIER_HI_TEST,
                           frame->control.u_frame.pf).value);

    // Echo the information field
    if (frame->information != NULL && frame->information_len > 0) {
        hdlc_output_packet_information_bytes(ctx, frame->information,
                                            frame->information_len);
    }

    hdlc_output_packet_end(ctx);
}

/**
 * @brief Internal handler for Unnumbered (U) Frames.
 * @param ctx   HDLC Context.
 * @param frame Received frame.
 */
static void handle_u_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  hdlc_u8 m_lo = frame->control.u_frame.m_lo;
  hdlc_u8 m_hi = frame->control.u_frame.m_hi;

  // 1. Handle COMMANDS -> Addressed to ME or BROADCAST
  if (frame->address == ctx->my_address || frame->address == HDLC_BROADCAST_ADDRESS) {
    
    // Optimization: Check for UI Frame first (Most common & valid for Broadcast)
    if (m_lo == HDLC_U_MODIFIER_LO_UI && m_hi == HDLC_U_MODIFIER_HI_UI) {
         hdlc_process_ui(ctx, frame);
         return;
    }

    // Filter: All other commands MUST be Unicast (Addressed to ME)
    // If this is a Broadcast frame, we ignore everything else.
    if (frame->address == HDLC_BROADCAST_ADDRESS) {
        return; 
    }

    // --- Unicast-Only Commands ---

    // SABM (Set Asynchronous Balanced Mode)
    if (m_lo == HDLC_U_MODIFIER_LO_SABM && m_hi == HDLC_U_MODIFIER_HI_SABM) {
        hdlc_process_sabm(ctx, frame);
    }
    // DISC (Disconnect)
    else if (m_lo == HDLC_U_MODIFIER_LO_DISC && m_hi == HDLC_U_MODIFIER_HI_DISC) {
        hdlc_process_disc(ctx, frame);
    }
    // SNRM (Set Normal Response Mode)
    else if (m_lo == HDLC_U_MODIFIER_LO_SNRM && m_hi == HDLC_U_MODIFIER_HI_SNRM) {
        hdlc_process_snrm(ctx, frame);
    }
    // SARM (Set Asynchronous Response Mode)
    else if (m_lo == HDLC_U_MODIFIER_LO_SARM && m_hi == HDLC_U_MODIFIER_HI_SARM) {
        hdlc_process_sarm(ctx, frame);
    }
    // TEST (Link Test)
    else if (m_lo == HDLC_U_MODIFIER_LO_TEST && m_hi == HDLC_U_MODIFIER_HI_TEST) {
        hdlc_process_test(ctx, frame);
    }
  }

  // 2. Handle RESPONSES -> Addressed to PEER (but received by ME from Peer)
  // Responses are never Broadcast.
  else if (frame->address == ctx->peer_address) {
    
    // UA (Unnumbered Acknowledgment)
    if (m_lo == HDLC_U_MODIFIER_LO_UA && m_hi == HDLC_U_MODIFIER_HI_UA) {
        hdlc_process_ua(ctx, frame);
    }
    // DM (Disconnected Mode)
    else if (m_lo == HDLC_U_MODIFIER_LO_DM && m_hi == HDLC_U_MODIFIER_HI_DM) {
        hdlc_process_dm(ctx, frame);
    }
    // FRMR (Frame Reject)
    else if (m_lo == HDLC_U_MODIFIER_LO_FRMR && m_hi == HDLC_U_MODIFIER_HI_FRMR) {
        hdlc_process_frmr(ctx, frame);
    }
    // TEST (Link Test Response)
    else if (m_lo == HDLC_U_MODIFIER_LO_TEST && m_hi == HDLC_U_MODIFIER_HI_TEST) {
        // TEST response received - no special handling needed.
        // The frame is passed to on_frame_cb by process_complete_frame.
    }
  }
}

/**
 * @brief Process a completely received and validated frame.
 *
 * Called when the State Machine detects a valid closing Flag and CRC checks
 * out. Routes the frame to the appropriate handler (I/S/U) and invokes the user
 * callback.
 *
 * @param ctx HDLC Context.
 */
static void process_complete_frame(hdlc_context_t *ctx) {
  // 1. Identify Frame Type based on Control Field
  hdlc_u8 ctrl = ctx->input_frame_buffer.control.value;

  if ((ctrl & HDLC_FRAME_TYPE_MASK_I) == HDLC_FRAME_TYPE_VAL_I) {
    ctx->input_frame_buffer.type = HDLC_FRAME_I;
    handle_i_frame(ctx, &ctx->input_frame_buffer);
  } else if ((ctrl & HDLC_FRAME_TYPE_MASK_S) == HDLC_FRAME_TYPE_VAL_S) {
    ctx->input_frame_buffer.type = HDLC_FRAME_S;
    handle_s_frame(ctx, &ctx->input_frame_buffer);
  } else if ((ctrl & HDLC_FRAME_TYPE_MASK_U) == HDLC_FRAME_TYPE_VAL_U) {
    ctx->input_frame_buffer.type = HDLC_FRAME_U;
    handle_u_frame(ctx, &ctx->input_frame_buffer);
  } else {
    ctx->input_frame_buffer.type = HDLC_FRAME_INVALID;
  }

  // 2. Notify User (Pass-through inspection)
  if (ctx->on_frame_cb != NULL) {
    ctx->on_frame_cb(&ctx->input_frame_buffer, ctx->user_data);
  }

  ctx->stats_input_frames++;
}

/**
 * @brief Input a received byte into the HDLC Parser.
 * @note **ISR UNSAFE**: Performs heavy validation (CRC) on frame end. Checks
 * for delimiters, handles byte-unstuffing, and buffers data.
 * @see hdlc.h for detailed ISR usage warnings.
 */
void hdlc_input_byte(hdlc_context_t *ctx, hdlc_u8 byte) {
  if (ctx == NULL) {
    return;
  }

  /* 1. Handle Frame Delimiters */
  if (byte == HDLC_FLAG) {
    if (ctx->input_state != HDLC_INPUT_STATE_HUNT) {
      // Minimum size: Addr(1) + Ctrl(1) + FCS(2) = 4 bytes
      if (ctx->input_index >= HDLC_MIN_FRAME_LEN) {
        // --- CRC Verification Strategy ---
        // 1. Re-calculate CRC over the "Data" portion (Addr..Payload).
        // 2. Compare calculated CRC with the received FCS bytes (last 2
        // bytes).

        hdlc_u16 calced_crc = HDLC_FCS_INIT_VALUE;
        hdlc_u32 data_len = ctx->input_index - HDLC_FCS_LEN; // Exclude FCS bytes

        for (hdlc_u32 i = 0; i < data_len; i++) {
          calced_crc =
              hdlc_crc_ccitt_update(calced_crc, ctx->input_buffer[i]);
        }

        // Extract Received FCS (Assuming MSB first order on wire -> Buffered as
        // Hi, Lo)
        hdlc_fcs_t *fcs = (hdlc_fcs_t *)&ctx->input_buffer[ctx->input_index - HDLC_FCS_LEN];
        hdlc_u16 rx_fcs = (fcs->fcs[0] << 8) | fcs->fcs[1];

        if (calced_crc == rx_fcs) {
          // Valid Frame!

          /* Construct the temporary frame descriptor (Zero-Copy) */
          ctx->input_frame_buffer.address = ctx->input_buffer[0];
          ctx->input_frame_buffer.control.value = ctx->input_buffer[1];
          
          // Information starts after Header (Addr+Ctrl), length is Total - (Header+FCS) = Total - 4
          // But only if total >= 4 (checked above)
          // Header Len = Address(1) + Control(1) = 2
          if (data_len > HDLC_ADDRESS_LEN + HDLC_CONTROL_LEN) {
             ctx->input_frame_buffer.information = &ctx->input_buffer[HDLC_ADDRESS_LEN + HDLC_CONTROL_LEN];
             ctx->input_frame_buffer.information_len = (hdlc_u16)(data_len - (HDLC_ADDRESS_LEN + HDLC_CONTROL_LEN));
          } else {
             ctx->input_frame_buffer.information = NULL;
             ctx->input_frame_buffer.information_len = 0;
          }

          process_complete_frame(ctx);
        } else {
          // CRC Error: Frame discarded silently (or logged)
          // printf("[LIB] CRC Fail: Calc=%04X Rx=%04X Len=%d\n", calced_crc,
          // rx_fcs, ctx->rx_index);
          ctx->stats_crc_errors++;
        }
      }
    }

    // Reset for next frame
    ctx->input_state = HDLC_INPUT_STATE_ADDRESS; // Expecting Address next
    ctx->input_index = 0;
    ctx->input_crc = HDLC_FCS_INIT_VALUE;
    return;
  }

  /* 2. Handle State Machine */

  if (ctx->input_state == HDLC_INPUT_STATE_HUNT) {
    return; // Ignoring noise awaiting next Flag
  }

  if (byte == HDLC_ESCAPE) {
    ctx->input_state = HDLC_INPUT_STATE_ESCAPE;
    return;
  }

  if (ctx->input_state == HDLC_INPUT_STATE_ESCAPE) {
    byte ^= HDLC_XOR_MASK;        // Un-escape
    ctx->input_state = HDLC_INPUT_STATE_DATA; // Return to normal state
  }

  /* 3. Validation & Buffering */

  if (ctx->input_index >= ctx->input_buffer_len) {
    // Overflow protection: Drop invalid large frame and hunt for next flag
    ctx->input_state = HDLC_INPUT_STATE_HUNT;
    return;
  }

  // Note: We don't update running RX CRC here anymore because we use
  // the "Recalculate over buffer" method on Frame End for robustness.

  // Store byte in buffer
  ctx->input_buffer[ctx->input_index++] = byte;
}

/**
 * @brief Input multiple received bytes into the HDLC Parser.
 * @see hdlc.h
 */
void hdlc_input_bytes(hdlc_context_t *ctx, const hdlc_u8 *data, hdlc_u32 len) {
  if (ctx == NULL || data == NULL) {
    return;
  }

  for (hdlc_u32 i = 0; i < len; ++i) {
    hdlc_input_byte(ctx, data[i]);
  }
}

/*
 * --------------------------------------------------------------------------
 * ZERO-COPY TRANSMIT ENGINE
 * --------------------------------------------------------------------------
 */

/**
 * @brief Internal helper to send a single raw byte to hardware.
 * @param ctx  HDLC Context.
 * @param byte Raw byte to transmit.
 */
static inline void output_byte_raw(hdlc_context_t *ctx, hdlc_u8 byte,
                                hdlc_bool flush) {
  if (ctx->output_byte_cb != NULL) {
    ctx->output_byte_cb(byte, flush, ctx->user_data);
  }
}

/**
 * @brief Internal helper to send a data byte with automatic escaping.
 *
 * @param ctx  HDLC Context.
 * @param byte Data byte to send.
 */
static void output_escaped(hdlc_context_t *ctx, hdlc_u8 byte) {
  if (byte == HDLC_FLAG || byte == HDLC_ESCAPE) {
    output_byte_raw(ctx, HDLC_ESCAPE, false);
    output_byte_raw(ctx, byte ^ HDLC_XOR_MASK, false);
  } else {
    output_byte_raw(ctx, byte, false);
  }
}

/**
 * @brief Internal helper to send a data byte with automatic escaping.
 *
 * Updates the running CRC and checks if the byte needs escaping
 * (i.e. if it looks like a FLAG or ESCAPE).
 *
 * @param ctx  HDLC Context.
 * @param byte Data byte to send.
 * @param crc  Pointer to the running CRC to update.
 */
static void output_escaped_crc_update(hdlc_context_t *ctx, hdlc_u8 byte,
                                       hdlc_u16 *crc) {
  *crc = hdlc_crc_ccitt_update(*crc, byte);

  output_escaped(ctx, byte);
}

/**
 * @brief Start a Packet Transmission.
 * @see hdlc.h
 */
void hdlc_output_packet_start(hdlc_context_t *ctx, hdlc_u8 address,
                                     hdlc_u8 control) {
  if (ctx == NULL) {
    return;
  }

  // Initialize CRC
  ctx->output_crc = HDLC_FCS_INIT_VALUE;

  // Send Start Flag
  output_byte_raw(ctx, HDLC_FLAG, false);

  // Send Address
  // Update CRC and Send Escaped
  output_escaped_crc_update(ctx, address, &ctx->output_crc);

  // Send Control
  // Update CRC and Send Escaped
  output_escaped_crc_update(ctx, control, &ctx->output_crc);
}

/**
 * @brief Output a Information Byte.
 * @see hdlc.h
 */
void hdlc_output_packet_information_byte(hdlc_context_t *ctx,
                                                hdlc_u8 information_byte) {
  if (ctx == NULL) {
    return;
  }

  // Update CRC and Send Escaped
  output_escaped_crc_update(ctx, information_byte, &ctx->output_crc);
}

/**
 * @brief Output a Information Bytes Array.
 * @see hdlc.h
 */
void hdlc_output_packet_information_bytes(
    hdlc_context_t *ctx, const hdlc_u8 *information_bytes, hdlc_u32 len) {
  if (ctx == NULL) {
    return;
  }

  for (hdlc_u32 i = 0; i < len; ++i) {
    // Update CRC and Send Escaped
    output_escaped_crc_update(ctx, information_bytes[i], &ctx->output_crc);
  }
}

/**
 * @brief Start a UI Packet Output.
 * @see hdlc.h
 */
void hdlc_output_packet_ui_start(hdlc_context_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  
  // UI Frame Control: 11 00 P 000 (Val=0x03 if P=0, 0x13 if P=1)
  // M_LO=0, M_HI=0
  hdlc_control_t ctrl = hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_UI, HDLC_U_MODIFIER_HI_UI, 0); // P=0 usually
  
  hdlc_output_packet_start(ctx, ctx->peer_address, ctrl.value);
}

/**
 * @brief Start a TEST Packet Output.
 * @see hdlc.h
 */
void hdlc_output_packet_test_start(hdlc_context_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  
  // TEST Frame: m_lo=0, m_hi=7, P=1
  hdlc_control_t ctrl = hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_TEST, HDLC_U_MODIFIER_HI_TEST, 1);
  
  hdlc_output_packet_start(ctx, ctx->peer_address, ctrl.value);
}

/**
 * @brief Finalize Packet Output.
 * @see hdlc.h
 */
void hdlc_output_packet_end(hdlc_context_t *ctx) {
  if (ctx == NULL) {
    return;
  }

  // Finalize CRC
  hdlc_u16 crc = ctx->output_crc;

  hdlc_u8 fcs_hi = (crc >> 8) & 0xFF;
  hdlc_u8 fcs_lo = crc & 0xFF;

  // Send FCS High
  output_escaped(ctx, fcs_hi);

  // Send FCS Low
  output_escaped(ctx, fcs_lo);

  // End Flag
  output_byte_raw(ctx, HDLC_FLAG, true);

  ctx->stats_output_frames++;
}

/*
 * --------------------------------------------------------------------------
 * CONTROL FIELD HELPERS
 * --------------------------------------------------------------------------
 */

hdlc_control_t hdlc_create_i_ctrl(hdlc_u8 ns, hdlc_u8 nr, hdlc_u8 pf) {
  hdlc_control_t ctrl = {0};
  ctrl.i_frame.frame_type_0 = 0;
  ctrl.i_frame.ns = ns;
  ctrl.i_frame.pf = pf;
  ctrl.i_frame.nr = nr;
  return ctrl;
}

hdlc_control_t hdlc_create_s_ctrl(hdlc_u8 s_bits, hdlc_u8 nr, hdlc_u8 pf) {
  hdlc_control_t ctrl = {0};
  ctrl.s_frame.frame_type_0 = 1;
  ctrl.s_frame.frame_type_1 = 0;
  ctrl.s_frame.s = s_bits;
  ctrl.s_frame.pf = pf;
  ctrl.s_frame.nr = nr;
  return ctrl;
}

hdlc_control_t hdlc_create_u_ctrl(hdlc_u8 m_lo, hdlc_u8 m_hi, hdlc_u8 pf) {
  hdlc_control_t ctrl = {0};
  ctrl.u_frame.frame_type_0 = 1;
  ctrl.u_frame.frame_type_1 = 1;
  ctrl.u_frame.m_lo = m_lo;
  ctrl.u_frame.pf = pf;
  ctrl.u_frame.m_hi = m_hi;
  return ctrl;
}

/*
 * --------------------------------------------------------------------------
 * CONNECTION MANAGEMENT
 * --------------------------------------------------------------------------
 */


void hdlc_configure_addresses(hdlc_context_t *ctx, hdlc_u8 my_addr, hdlc_u8 peer_addr) {
  if (ctx) {
    ctx->my_address = my_addr;
    ctx->peer_address = peer_addr;
  }
}

bool hdlc_connect(hdlc_context_t *ctx) {
  if (ctx == NULL) return false;

  // Send SABM
  hdlc_control_t ctrl = hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_SABM, HDLC_U_MODIFIER_HI_SABM, 1); // P=1
  hdlc_output_packet_start(ctx, ctx->peer_address, ctrl.value);
  hdlc_output_packet_end(ctx);

  hdlc_set_protocol_state(ctx, HDLC_PROTOCOL_STATE_CONNECTING);
  return true;
}

bool hdlc_disconnect(hdlc_context_t *ctx) {
  if (ctx == NULL) return false;

  // Send DISC
  hdlc_control_t ctrl = hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_DISC, HDLC_U_MODIFIER_HI_DISC, 1); // P=1
  hdlc_output_packet_start(ctx, ctx->peer_address, ctrl.value);
  hdlc_output_packet_end(ctx);

  hdlc_set_protocol_state(ctx, HDLC_PROTOCOL_STATE_DISCONNECTING);
  return true;
}

bool hdlc_is_connected(hdlc_context_t *ctx) {
  return (ctx != NULL && ctx->current_state == HDLC_PROTOCOL_STATE_CONNECTED);
}

bool hdlc_output_ui(hdlc_context_t *ctx, const hdlc_u8 *data, hdlc_u32 len) {
  if (ctx == NULL) return false;

  // Start Packet
  hdlc_output_packet_ui_start(ctx);
  
  // Send Data
  if (data != NULL && len > 0) {
      hdlc_output_packet_information_bytes(ctx, data, len);
  }

  // End Packet
  hdlc_output_packet_end(ctx);
  return true;
}

bool hdlc_output_test(hdlc_context_t *ctx, const hdlc_u8 *data, hdlc_u32 len) {
  if (ctx == NULL) return false;

  // Start Packet
  hdlc_output_packet_test_start(ctx);

  // Send Data
  if (data != NULL && len > 0) {
      hdlc_output_packet_information_bytes(ctx, data, len);
  }

  // End Packet
  hdlc_output_packet_end(ctx);
  return true;
}

/**
 * @brief Start an Information (I) Packet Output (Streaming).
 * @see hdlc.h
 */
void hdlc_output_packet_i_start(hdlc_context_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  
  // I-Frame: N(S)=VS, N(R)=VR, P=0 (Default)
  hdlc_control_t ctrl = hdlc_create_i_ctrl(ctx->vs, ctx->vr, 0);
  
  hdlc_output_packet_start(ctx, ctx->peer_address, ctrl.value);
}

/**
 * @brief Output an Information (I) frame (Reliable).
 * @see hdlc.h
 */
bool hdlc_output_i(hdlc_context_t *ctx, const hdlc_u8 *data, hdlc_u32 len) {
  if (ctx == NULL) return false;

  // Window Check (Window Size = 1)
  if (ctx->waiting_for_ack) {
      return false; // Window closed
  }
  
  // Buffer for Retransmission (if buffer is configured)
  if (ctx->retransmit_buffer != NULL) {
      if (len > 0 && data != NULL) {
          if (len > ctx->retransmit_buffer_len) {
              return false; // Data too large for retransmit buffer.
          }
          memcpy(ctx->retransmit_buffer, data, len);
      }
      ctx->retransmit_len = len;
  }

  // Start Packet
  hdlc_output_packet_i_start(ctx);
  
  // Send Data
  if (data != NULL && len > 0) {
      hdlc_output_packet_information_bytes(ctx, data, len);
  }

  // End Packet
  hdlc_output_packet_end(ctx);
  
  // Update State
  ctx->vs = (ctx->vs + 1) % 8; // Modulo 8
  ctx->waiting_for_ack = true;
  ctx->ack_pending = false; // Piggybacked ACK sent
  
  // Start Timer
  ctx->retransmit_timer_ms = 1000; // 1 second timeout (Hardcoded for now)
  
  return true;
}

/**
 * @brief Periodic Tick for Timers.
 * @see hdlc.h
 */
void hdlc_tick(hdlc_context_t *ctx, hdlc_u32 delta_ms) {
    if (ctx == NULL) return;
    
    // Retransmission Timer
    if (ctx->waiting_for_ack) {
        if (ctx->retransmit_timer_ms > 0) {
            if (ctx->retransmit_timer_ms > delta_ms) {
                ctx->retransmit_timer_ms -= delta_ms;
            } else {
                ctx->retransmit_timer_ms = 0;
            }

            if (ctx->retransmit_timer_ms == 0) {
                // Timeout! Retransmit.
                // Re-send the buffered frame.
                
                // Retransmit with the original N(S) = (V(S) - 1 + 8) % 8.
                hdlc_u8 old_ns = (ctx->vs + 7) % 8;
                
                // Send Frame
                hdlc_control_t ctrl = hdlc_create_i_ctrl(old_ns, ctx->vr, 1); // P=1 (Poll)
                hdlc_output_packet_start(ctx, ctx->peer_address, ctrl.value);
                
                if (ctx->retransmit_len > 0 && ctx->retransmit_buffer != NULL) {
                    hdlc_output_packet_information_bytes(ctx, ctx->retransmit_buffer, ctx->retransmit_len);
                }
                
                hdlc_output_packet_end(ctx);
                
                // Restart Timer
                ctx->retransmit_timer_ms = 1000;
                ctx->stats_output_frames++;
            }
        }
    }
}
