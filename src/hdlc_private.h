/**
 * @file hdlc_private.h
 * @brief Internal Definitions and State Constants.
 *
 * Contains private constants and macros used by the HDLC implementation
 * but not required for the public API contract.
 */

#ifndef HDLC_PRIVATE_H
#define HDLC_PRIVATE_H

#include "../inc/hdlc_types.h"

/**
 * @brief RX State Machine States.
 * Internal states for the byte-by-byte receive parser.
 */
typedef enum {
  HDLC_RX_HUNT = 0, /**< Searching for the Start Flag (0x7E) to sync. */
  HDLC_RX_ADDRESS,  /**< Frame detected, expecting Address Byte next. */
  HDLC_RX_DATA,     /**< Receiving Control or Payload Data. */
  HDLC_RX_ESCAPE    /**< Previous byte was 0x7D (Escape), next byte needs XORing. */
} hdlc_rx_state_t;

#endif // HDLC_PRIVATE_H
