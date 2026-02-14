/**
 * @file hdlc.c
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief Main Implementation of the HDLC Protocol Stack.
 *
 * Contains the logic for Frame Transmission (Buffered & Streaming),
 * Frame Reception (State Machine, Byte Stuffing removal), and
 * CRC Verification.
 */

#include "../inc/hdlc.h"
#include "hdlc_crc.h"
#include "hdlc_private.h"
#include <stdio.h>
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
void hdlc_stream_init(hdlc_context_t *ctx, hdlc_output_byte_cb_t output_cb,
                      hdlc_on_frame_cb_t on_frame_cb, hdlc_u8 *buffer,
                      hdlc_u32 buffer_len, void *user_data) {
  if (ctx == NULL || buffer == NULL || buffer_len < HDLC_MIN_FRAME_LEN) {
    return;
  }

  // Clear context
  memset(ctx, 0, sizeof(hdlc_context_t));

  // Initialize Buffer
  ctx->input_buffer = buffer;
  ctx->input_buffer_len = buffer_len;

  // Bind callbacks
  ctx->output_byte_cb = output_cb;
  ctx->on_frame_cb = on_frame_cb;
  ctx->user_data = user_data;

  // Initialize State
  ctx->input_state = HDLC_INPUT_STATE_HUNT;
}

/*
 * --------------------------------------------------------------------------
 * TRANSMIT ENGINE
 * --------------------------------------------------------------------------
 */

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

static inline void pack_byte(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn,
                               hdlc_u8 byte, hdlc_bool flush) {
  put_fn(ctx, byte, flush);
}

static void pack_escaped(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn,
                           hdlc_u8 byte) {
  if (byte == HDLC_FLAG || byte == HDLC_ESCAPE) {
    pack_byte(ctx, put_fn, HDLC_ESCAPE, false);
    pack_byte(ctx, put_fn, byte ^ HDLC_XOR_MASK, false);
  } else {
    pack_byte(ctx, put_fn, byte, false);
  }
}

static void pack_escaped_crc_update(hdlc_encode_ctx_t *ctx,
                                      hdlc_put_byte_fn put_fn, hdlc_u8 byte,
                                      hdlc_u16 *crc) {
  *crc = hdlc_crc_ccitt_update(*crc, byte);
  pack_escaped(ctx, put_fn, byte);
}

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
 * @brief Output a complete HDLC Frame (Streaming).
 * @see hdlc.h
 */
void hdlc_stream_output_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
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
static void stream_handle_i_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  // TODO: Implement I-Frame Logic
  // - Check Sequence Number N(S) against V(R)
  // - Check Piggybacked Ack N(R) against V(S)
  // - Deliver payload to user
  // - Send Ack (RR) if needed
  (void)ctx;
  (void)frame;
}

/**
 * @brief Internal handler for Supervisory (S) Frames.
 * @param ctx   HDLC Context.
 * @param frame Received frame.
 */
static void stream_handle_s_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  // TODO: Implement S-Frame Logic
  // - Handle RR, RNR, REJ
  (void)ctx;
  (void)frame;
}

/**
 * @brief Internal handler for Unnumbered (U) Frames.
 * @param ctx   HDLC Context.
 * @param frame Received frame.
 */
static void stream_handle_u_frame(hdlc_context_t *ctx, const hdlc_frame_t *frame) {
  // TODO: Implement U-Frame Logic
  // - SABM: Send UA, Reset State
  // - DISC: Send UA, Go to Disconnected
  // - UA: Connection Established
  (void)ctx;
  (void)frame;
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
static void stream_process_complete_frame(hdlc_context_t *ctx) {
  // 1. Identify Frame Type based on Control Field
  hdlc_u8 ctrl = ctx->input_frame_buffer.control.value;

  if ((ctrl & HDLC_FRAME_TYPE_MASK_I) == HDLC_FRAME_TYPE_VAL_I) {
    ctx->input_frame_buffer.type = HDLC_FRAME_I;
    stream_handle_i_frame(ctx, &ctx->input_frame_buffer);
  } else if ((ctrl & HDLC_FRAME_TYPE_MASK_S) == HDLC_FRAME_TYPE_VAL_S) {
    ctx->input_frame_buffer.type = HDLC_FRAME_S;
    stream_handle_s_frame(ctx, &ctx->input_frame_buffer);
  } else if ((ctrl & HDLC_FRAME_TYPE_MASK_U) == HDLC_FRAME_TYPE_VAL_U) {
    ctx->input_frame_buffer.type = HDLC_FRAME_U;
    stream_handle_u_frame(ctx, &ctx->input_frame_buffer);
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
void hdlc_stream_input_byte(hdlc_context_t *ctx, hdlc_u8 byte) {
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

          stream_process_complete_frame(ctx);
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
void hdlc_stream_input_bytes(hdlc_context_t *ctx, const hdlc_u8 *data, hdlc_u32 len) {
  if (ctx == NULL || data == NULL) {
    return;
  }

  for (hdlc_u32 i = 0; i < len; ++i) {
    hdlc_stream_input_byte(ctx, data[i]);
  }
}

/*
 * --------------------------------------------------------------------------
 * STREAMING TRANSMIT ENGINE
 * --------------------------------------------------------------------------
 */

/**
 * @brief Internal helper to send a single raw byte to hardware.
 * @param ctx  HDLC Context.
 * @param byte Raw byte to transmit.
 */
static inline void stream_output_byte_raw(hdlc_context_t *ctx, hdlc_u8 byte,
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
static void stream_output_escaped(hdlc_context_t *ctx, hdlc_u8 byte) {
  if (byte == HDLC_FLAG || byte == HDLC_ESCAPE) {
    stream_output_byte_raw(ctx, HDLC_ESCAPE, false);
    stream_output_byte_raw(ctx, byte ^ HDLC_XOR_MASK, false);
  } else {
    stream_output_byte_raw(ctx, byte, false);
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
static void stream_output_escaped_crc_update(hdlc_context_t *ctx, hdlc_u8 byte,
                                       hdlc_u16 *crc) {
  *crc = hdlc_crc_ccitt_update(*crc, byte);

  stream_output_escaped(ctx, byte);
}

/**
 * @brief Start a Streaming Packet Transmission.
 * @see hdlc.h
 */
void hdlc_stream_output_packet_start(hdlc_context_t *ctx, hdlc_u8 address,
                                     hdlc_u8 control) {
  if (ctx == NULL) {
    return;
  }

  // Initialize CRC
  ctx->output_crc = HDLC_FCS_INIT_VALUE;

  // Send Start Flag
  stream_output_byte_raw(ctx, HDLC_FLAG, false);

  // Send Address
  // Update CRC and Send Escaped
  stream_output_escaped_crc_update(ctx, address, &ctx->output_crc);

  // Send Control
  // Update CRC and Send Escaped
  stream_output_escaped_crc_update(ctx, control, &ctx->output_crc);
}

/**
 * @brief Output a Information Byte in Streaming Mode.
 * @see hdlc.h
 */
void hdlc_stream_output_packet_information_byte(hdlc_context_t *ctx,
                                                hdlc_u8 information_byte) {
  if (ctx == NULL) {
    return;
  }

  // Update CRC and Send Escaped
  stream_output_escaped_crc_update(ctx, information_byte, &ctx->output_crc);
}

/**
 * @brief Output a Information Bytes Array in Streaming Mode.
 * @see hdlc.h
 */
void hdlc_stream_output_packet_information_bytes(
    hdlc_context_t *ctx, const hdlc_u8 *information_bytes, hdlc_u32 len) {
  if (ctx == NULL) {
    return;
  }

  for (hdlc_u32 i = 0; i < len; ++i) {
    // Update CRC and Send Escaped
    stream_output_escaped_crc_update(ctx, information_bytes[i], &ctx->output_crc);
  }
}

/**
 * @brief Finalize Streaming Packet Output.
 * @see hdlc.h
 */
void hdlc_stream_output_packet_end(hdlc_context_t *ctx) {
  if (ctx == NULL) {
    return;
  }

  // Finalize CRC
  hdlc_u16 crc = ctx->output_crc;

  hdlc_u8 fcs_hi = (crc >> 8) & 0xFF;
  hdlc_u8 fcs_lo = crc & 0xFF;

  // Send FCS High
  stream_output_escaped(ctx, fcs_hi);

  // Send FCS Low
  stream_output_escaped(ctx, fcs_lo);

  // End Flag
  stream_output_byte_raw(ctx, HDLC_FLAG, true);

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
