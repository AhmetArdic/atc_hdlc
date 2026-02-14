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

#endif // HDLC_PRIVATE_H
