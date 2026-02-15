/**
 * @file hdlc_private.h
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief Internal Definitions and State Constants.
 *
 * Contains private constants and macros used by the HDLC implementation
 * but not required for the public API contract.
 */

#ifndef HDLC_PRIVATE_H
#define HDLC_PRIVATE_H

#include "../inc/hdlc_types.h"

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

#endif // HDLC_PRIVATE_H
