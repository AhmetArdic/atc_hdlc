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
 * @file hdlc_private.h
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief Internal Definitions and State Constants.
 *
 * Contains private constants, macros, types, and function prototypes
 * shared across HDLC implementation modules but not part of the public API.
 */

#ifndef ATC_HDLC_PRIVATE_H
#define ATC_HDLC_PRIVATE_H

#include "../inc/hdlc_types.h"

/*
 * --------------------------------------------------------------------------
 * FRAME LENGTH CONSTANTS (Internal)
 * --------------------------------------------------------------------------
 */
#define ATC_HDLC_FLAG_LEN               (1)     /**< Flag. */
#define ATC_HDLC_ADDRESS_LEN            (1)     /**< Address Field. */
#define ATC_HDLC_CONTROL_LEN            (1)     /**< Control Field. */
#define ATC_HDLC_FCS_LEN                (2)     /**< FCS Field. */

/** Minimum frame length: Address(1) + Control(1) + FCS(2). */
#define ATC_HDLC_MIN_FRAME_LEN          (ATC_HDLC_ADDRESS_LEN + ATC_HDLC_CONTROL_LEN + ATC_HDLC_FCS_LEN)

/* Frame Type Masks & Values */
#define ATC_HDLC_FRAME_TYPE_MASK_I      (0x01)
#define ATC_HDLC_FRAME_TYPE_VAL_I       (0x00)

#define ATC_HDLC_FRAME_TYPE_MASK_S      (0x03)
#define ATC_HDLC_FRAME_TYPE_VAL_S       (0x01)

#define ATC_HDLC_FRAME_TYPE_MASK_U      (0x03)
#define ATC_HDLC_FRAME_TYPE_VAL_U       (0x03)

/*
 * Control Field Properties Extraction/Creation Macros
 * Assuming structure:
 * I-Frame: [b0: 0] [b1-3: N(S)] [b4: P/F] [b5-7: N(R)]
 * S-Frame: [b0: 1] [b1: 0] [b2-3: S] [b4: P/F] [b5-7: N(R)]
 * U-Frame: [b0: 1] [b1: 1] [b2-3: M_lo] [b4: P/F] [b5-7: M_hi]
 */
#define HDLC_CTRL_PF(ctrl)         (((ctrl) >> 4) & 0x01)
#define HDLC_CTRL_NR(ctrl)         (((ctrl) >> 5) & 0x07)
#define HDLC_CTRL_I_NS(ctrl)       (((ctrl) >> 1) & 0x07)
#define HDLC_CTRL_S_BITS(ctrl)     (((ctrl) >> 2) & 0x03)
#define HDLC_CTRL_U_M_LO(ctrl)     (((ctrl) >> 2) & 0x03)
#define HDLC_CTRL_U_M_HI(ctrl)     (((ctrl) >> 5) & 0x07)

/**
 * @brief Input State Machine States.
 * Internal states for the byte-by-byte receive parser.
 */
typedef enum {
    HDLC_INPUT_STATE_HUNT = 0, /**< Searching for the Start Flag (0x7E) to sync. */
    HDLC_INPUT_STATE_ADDRESS,  /**< Frame detected, expecting Address Byte next. */
    HDLC_INPUT_STATE_DATA,     /**< Receiving Control or Payload Data. */
    HDLC_INPUT_STATE_ESCAPE    /**< Previous byte was 0x7D (Escape), next byte needs XORing. */
} hdlc_input_state_t;

/*
 * --------------------------------------------------------------------------
 * ESCAPE SEQUENCE CONSTANTS
 * --------------------------------------------------------------------------
 */
/** @brief HDLC Flag Sequence (0x7E) used to delimit frames. */
#define HDLC_FLAG 0x7E
/** @brief HDLC Escape Octet (0x7D) used for transparency. */
#define HDLC_ESCAPE 0x7D
/** @brief Bit-mask (0x20) XORed with octets to be escaped. */
#define HDLC_XOR_MASK 0x20

/*
 * --------------------------------------------------------------------------
 * U-FRAME MODIFIER VALUES
 * --------------------------------------------------------------------------
 * M-bits split into m_lo (2 bits) and m_hi (3 bits)
 */
 
/* SABM: 001 11 -> m_hi=1, m_lo=3 */
#define HDLC_U_MODIFIER_LO_SABM 3
#define HDLC_U_MODIFIER_HI_SABM 1

/* SNRM: 100 11 -> m_hi=4, m_lo=0 */
#define HDLC_U_MODIFIER_LO_SNRM 0
#define HDLC_U_MODIFIER_HI_SNRM 4

/* SARM: 000 11 -> m_hi=0, m_lo=3 (Same as DM, distinguished by Command/Response) */
#define HDLC_U_MODIFIER_LO_SARM 3
#define HDLC_U_MODIFIER_HI_SARM 0

/* SABME: 011 11 -> m_hi=3, m_lo=3 */
#define HDLC_U_MODIFIER_LO_SABME 3
#define HDLC_U_MODIFIER_HI_SABME 3

/* SNRME: 110 11 -> m_hi=6, m_lo=3 */
#define HDLC_U_MODIFIER_LO_SNRME 3
#define HDLC_U_MODIFIER_HI_SNRME 6

/* SARME: 010 11 -> m_hi=2, m_lo=3 */
#define HDLC_U_MODIFIER_LO_SARME 3
#define HDLC_U_MODIFIER_HI_SARME 2

/* DISC: 010 00 -> m_hi=2, m_lo=0 */
#define HDLC_U_MODIFIER_LO_DISC 0
#define HDLC_U_MODIFIER_HI_DISC 2

/* UA: 011 00 -> m_hi=3, m_lo=0 */
#define HDLC_U_MODIFIER_LO_UA   0
#define HDLC_U_MODIFIER_HI_UA   3

/* DM: 000 11 -> m_hi=0, m_lo=3 */
#define HDLC_U_MODIFIER_LO_DM   3
#define HDLC_U_MODIFIER_HI_DM   0

/* FRMR: 100 01 -> m_hi=4, m_lo=1 */
#define HDLC_U_MODIFIER_LO_FRMR 1
#define HDLC_U_MODIFIER_HI_FRMR 4

/* UI: 000 00 -> m_hi=0, m_lo=0 */
#define HDLC_U_MODIFIER_LO_UI   0
#define HDLC_U_MODIFIER_HI_UI   0

/* TEST: 111 00 -> m_hi=7, m_lo=0 */
#define HDLC_U_MODIFIER_LO_TEST 0
#define HDLC_U_MODIFIER_HI_TEST 7

/* FRMR Information Field Constants */
#define HDLC_FRMR_INFO_MIN_LEN  3

/* Byte 1: 0 V(S) C/R V(R) -> 0 sss c rrr */
#define HDLC_FRMR_VS_SHIFT      1
#define HDLC_FRMR_VS_MASK       0x07
#define HDLC_FRMR_CR_BIT        0x10
#define HDLC_FRMR_VR_SHIFT      5
#define HDLC_FRMR_VR_MASK       0x07

/* Byte 2: W X Y Z V 0 0 0 */
#define HDLC_FRMR_W_BIT         0x01
#define HDLC_FRMR_X_BIT         0x02
#define HDLC_FRMR_Y_BIT         0x04
#define HDLC_FRMR_Z_BIT         0x08
#define HDLC_FRMR_V_BIT         0x10
/**
 * @brief Frame Reject (FRMR) Information Fields.
 * Standard format for the information field of an FRMR response.
 * Internal only — used by hdlc_frame_handlers.c for FRMR parsing.
 */
typedef struct {
    atc_hdlc_u16 rejected_control; /**< Copy of the rejected control field. */
    atc_hdlc_u8 v_s;               /**< Current Send Sequence Number V(S). */
    atc_hdlc_u8 v_r;               /**< Current Receive Sequence Number V(R). */
    atc_hdlc_bool cr;              /**< Command/Response flag. */
    struct {
        atc_hdlc_bool w; /**< Control field undefined/unimplemented. */
        atc_hdlc_bool x; /**< Info field not allowed with this frame. */
        atc_hdlc_bool y; /**< Info field too long. */
        atc_hdlc_bool z; /**< Invalid N(R). */
        atc_hdlc_bool v; /**< Invalid N(S). */
    } errors;
} atc_hdlc_frmr_data_t;

/*
 * --------------------------------------------------------------------------
 * S-FRAME SUPERVISORY FUNCTION BITS
 * --------------------------------------------------------------------------
 * The S-bits (2 bits) in an S-Frame control field.
 */
#define HDLC_S_RR   0   /**< Receive Ready (RR). */
#define HDLC_S_RNR  1   /**< Receive Not Ready (RNR). */
#define HDLC_S_REJ  2   /**< Reject (REJ). */

/*
 * --------------------------------------------------------------------------
 * SEQUENCE NUMBER CONSTANTS
 * --------------------------------------------------------------------------
 */
/** @brief Modulus for sequence numbers (V(S), V(R), N(S), N(R)). 3 bits => mod 8. */
#define HDLC_SEQUENCE_MODULUS   8

/*
 * --------------------------------------------------------------------------
 * INTERNAL TYPES (Shared across modules)
 * --------------------------------------------------------------------------
 */

/** @brief Encoding context used by frame serialization. */
typedef struct {
  atc_hdlc_context_t *ctx;  /**< For callback-based TX. */
  atc_hdlc_u8 *buffer;      /**< For buffer-based TX. */
  atc_hdlc_u32 buffer_len;  /**< Max buffer length. */
  atc_hdlc_u32 current_len; /**< Current bytes written to buffer. */
  atc_hdlc_bool success;    /**< Used for buffer overflow check. */
} hdlc_encode_ctx_t;

/** @brief Function pointer for writing a byte during encoding. */
typedef void (*hdlc_put_byte_fn)(hdlc_encode_ctx_t *enc_ctx, atc_hdlc_u8 byte, atc_hdlc_bool flush);

/*
 * --------------------------------------------------------------------------
 * INTERNAL FUNCTION PROTOTYPES (Cross-module)
 * --------------------------------------------------------------------------
 */

/* hdlc.c — State management */
void hdlc_set_protocol_state(atc_hdlc_context_t *ctx, atc_hdlc_protocol_state_t new_state, atc_hdlc_event_t event);

/* hdlc_output.c — Output helpers used by multiple modules */
void atc_hdlc_output_frame_start(atc_hdlc_context_t *ctx, atc_hdlc_u8 address, atc_hdlc_u8 control);
void atc_hdlc_output_frame_information_bytes(atc_hdlc_context_t *ctx, const atc_hdlc_u8 *information_bytes, atc_hdlc_u32 len);
void atc_hdlc_output_frame_end(atc_hdlc_context_t *ctx);

/* hdlc_frame.c — Encoding helpers used by hdlc_output.c */
void output_byte_to_callback(hdlc_encode_ctx_t *enc_ctx, atc_hdlc_u8 byte, atc_hdlc_bool flush);
void pack_escaped(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn, atc_hdlc_u8 byte);
void pack_escaped_crc_update(hdlc_encode_ctx_t *ctx, hdlc_put_byte_fn put_fn, atc_hdlc_u8 byte, atc_hdlc_u16 *crc);
atc_hdlc_bool frame_pack_core(const atc_hdlc_frame_t *frame, hdlc_put_byte_fn put_fn, hdlc_encode_ctx_t *enc_ctx);

/* hdlc_input.c — Process complete frame (called from input parser) */
void process_complete_frame(atc_hdlc_context_t *ctx);

/* hdlc_frame_handlers.c — Connection state reset */
void hdlc_reset_connection_state(atc_hdlc_context_t *ctx);

/*
 * --------------------------------------------------------------------------
 * INTERNAL FRAME SEND HELPERS
 * --------------------------------------------------------------------------
 */

/* hdlc_output.c — Complete frame output (internal) */
void atc_hdlc_output_frame(atc_hdlc_context_t *ctx, const atc_hdlc_frame_t *frame);

/* hdlc_frame.c — Control field constructors (internal) */
atc_hdlc_u8 atc_hdlc_create_i_ctrl(atc_hdlc_u8 ns, atc_hdlc_u8 nr, atc_hdlc_u8 pf);
atc_hdlc_u8 atc_hdlc_create_s_ctrl(atc_hdlc_u8 s_bits, atc_hdlc_u8 nr, atc_hdlc_u8 pf);
atc_hdlc_u8 atc_hdlc_create_u_ctrl(atc_hdlc_u8 m_lo, atc_hdlc_u8 m_hi, atc_hdlc_u8 pf);

/* Shared frame type resolver */
static inline atc_hdlc_frame_type_t hdlc_resolve_frame_type(atc_hdlc_u8 ctrl) {
    if ((ctrl & ATC_HDLC_FRAME_TYPE_MASK_I) == ATC_HDLC_FRAME_TYPE_VAL_I) return ATC_HDLC_FRAME_I;
    if ((ctrl & ATC_HDLC_FRAME_TYPE_MASK_S) == ATC_HDLC_FRAME_TYPE_VAL_S) return ATC_HDLC_FRAME_S;
    if ((ctrl & ATC_HDLC_FRAME_TYPE_MASK_U) == ATC_HDLC_FRAME_TYPE_VAL_U) return ATC_HDLC_FRAME_U;
    return ATC_HDLC_FRAME_INVALID;
}

static inline void hdlc_send_u_frame(atc_hdlc_context_t *ctx, atc_hdlc_u8 address, atc_hdlc_u8 m_lo, atc_hdlc_u8 m_hi, atc_hdlc_u8 pf) {
    atc_hdlc_u8 ctrl = atc_hdlc_create_u_ctrl(m_lo, m_hi, pf);
    atc_hdlc_output_frame_start(ctx, address, ctrl);
    atc_hdlc_output_frame_end(ctx);
}

static inline void hdlc_send_ua(atc_hdlc_context_t *ctx, atc_hdlc_u8 pf) {
    hdlc_send_u_frame(ctx, ctx->my_address, HDLC_U_MODIFIER_LO_UA, HDLC_U_MODIFIER_HI_UA, pf);
}

static inline void hdlc_send_dm(atc_hdlc_context_t *ctx, atc_hdlc_u8 pf) {
    hdlc_send_u_frame(ctx, ctx->my_address, HDLC_U_MODIFIER_LO_DM, HDLC_U_MODIFIER_HI_DM, pf);
}

static inline void hdlc_send_s_frame(atc_hdlc_context_t *ctx, atc_hdlc_u8 address, atc_hdlc_u8 s_bits, atc_hdlc_u8 nr, atc_hdlc_u8 pf) {
    atc_hdlc_u8 ctrl = atc_hdlc_create_s_ctrl(s_bits, nr, pf);
    atc_hdlc_output_frame_start(ctx, address, ctrl);
    atc_hdlc_output_frame_end(ctx);
}

static inline void hdlc_send_rr(atc_hdlc_context_t *ctx, atc_hdlc_u8 pf) {
    hdlc_send_s_frame(ctx, ctx->peer_address, HDLC_S_RR, ctx->vr, pf); // Command
}

static inline void hdlc_send_response_rr(atc_hdlc_context_t *ctx, atc_hdlc_u8 pf) {
    hdlc_send_s_frame(ctx, ctx->my_address, HDLC_S_RR, ctx->vr, pf); // Response (Address = Sender's own address)
}

static inline void hdlc_send_rej(atc_hdlc_context_t *ctx, atc_hdlc_u8 pf) {
    hdlc_send_s_frame(ctx, ctx->peer_address, HDLC_S_REJ, ctx->vr, pf);
}

#endif // ATC_HDLC_PRIVATE_H
