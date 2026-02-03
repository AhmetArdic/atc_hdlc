/**
 * @file hdlc_types.h
 * @brief Core Data Types and Structures for the HDLC Library.
 *
 * This file defines the fundamental data types, enumerations, and structures
 * used throughout the HDLC protocol stack, including Frame structures,
 * Control Field definitions, and the main Context structure.
 */

#ifndef HDLC_TYPES_H
#define HDLC_TYPES_H

#include "hdlc_config.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * --------------------------------------------------------------------------
 * BASIC TYPE DEFINITIONS
 * --------------------------------------------------------------------------
 */

/** @brief 8-bit unsigned integer type. */
typedef uint8_t hdlc_u8;
/** @brief 16-bit unsigned integer type. */
typedef uint16_t hdlc_u16;
/** @brief 32-bit unsigned integer type. */
typedef uint32_t hdlc_u32;
/** @brief Boolean type. */
typedef bool hdlc_bool;

/*
 * --------------------------------------------------------------------------
 * ENUMERATIONS
 * --------------------------------------------------------------------------
 */

/**
 * @brief HDLC Frame Types.
 *
 * Categorizes frames based on the Control Field format.
 */
typedef enum {
  HDLC_FRAME_I,      /**< Information Frame (Data transfer) */
  HDLC_FRAME_S,      /**< Supervisory Frame (Flow/Error control) */
  HDLC_FRAME_U,      /**< Unnumbered Frame (Link management) */
  HDLC_FRAME_INVALID /**< Invalid or Unknown Frame format */
} hdlc_frame_type_t;

/**
 * @brief Connection State Machine.
 *
 * Represents the current state of the logical link connection.
 * Used for ABM (Asynchronous Balanced Mode) logic.
 */
typedef enum {
  HDLC_STATE_DISCONNECTED, /**< Link is down or disconnected. */
  HDLC_STATE_SETUP,        /**< SABM sent, waiting for UA. */
  HDLC_STATE_CONNECTED,    /**< Link established, ready for I-Frames. */
  HDLC_STATE_ERROR         /**< Unrecoverable error state. */
} hdlc_state_t;

/*
 * --------------------------------------------------------------------------
 * CONTROL FIELD STRUCTURES
 * --------------------------------------------------------------------------
 */

/**
 * @brief HDLC Control Field Union.
 * Bit allocations:
 * I-Frame: N(R)[3] | P/F[1] | N(S)[3] | 0[1]
 * S-Frame: N(R)[3] | P/F[1] | S[2]    | 01[2]
 * U-Frame: M[3]    | P/F[1] | M[2]    | 11[2]
 *
 * Provides accessible bit-fields for I, S, and U frame control bytes.
 * NOTE: Bit-field ordering is compiler-dependent. Use the raw `value`
 * for critical serialization if not packing explicitly.
 */
typedef union {
  hdlc_u8 value; /**< Raw 8-bit Control Byte value. */

  struct {
    hdlc_u8 bit0 : 1; /**< Frame Type ID (0=I-Frame). */
    hdlc_u8 bit1 : 1; /**< Frame Type ID. */
    hdlc_u8 p_f : 1;  /**< Poll/Final Bit. */
    hdlc_u8 oth : 5;  /**< Remaining bits (Ns, Nr, etc.). */
  } bits;
} hdlc_control_t;

/*
 * --------------------------------------------------------------------------
 * FRAME STRUCTURE
 * --------------------------------------------------------------------------
 */

/**
 * @brief HDLC Frame Structure.
 *
 * logical representation of a received or to-be-transmitted frame.
 * Contains the parsed header fields and the payload buffer.
 */
typedef struct {
  hdlc_u8 address;                /**< Address Field (usually 0xFF for broadcast or Station ID). */
  hdlc_control_t control;         /**< Control Field (Type, Seq Numbers, P/F). */
  hdlc_frame_type_t type;         /**< Resolved Frame Type (I/S/U). */
  hdlc_u8 payload[HDLC_MAX_MTU];  /**< Information Field (Payload data). */
  hdlc_u16 payload_len;           /**< Length of valid data in payload[]. */
  hdlc_bool is_command;           /**< True if Command frame, False if Response (Address logic). */
} hdlc_frame_t;

/*
 * --------------------------------------------------------------------------
 * CALLBACK DEFINITIONS
 * --------------------------------------------------------------------------
 */

/**
 * @brief Output Byte Callback.
 *
 * Function pointer type for the hardware transmission interface.
 * @param user_data Pointer to user-defined context data.
 * @param byte      The byte to send over the physical medium (UART).
 */
typedef void (*hdlc_tx_byte_cb_t)(void *user_data, hdlc_u8 byte);

/**
 * @brief Frame Received Callback.
 *
 * Function pointer type for notifying the application of a valid received
 * frame.
 * @param user_data Pointer to user-defined context data.
 * @param frame     Pointer to the fully parsed HDLC frame structure.
 */
typedef void (*hdlc_on_frame_cb_t)(void *user_data, const hdlc_frame_t *frame);

/*
 * --------------------------------------------------------------------------
 * CONTEXT STRUCTURE
 * --------------------------------------------------------------------------
 */

/**
 * @brief HDLC Protocol Context.
 *
 * Main state structure holding all runtime information for a single HDLC
 * instance.
 */
typedef struct {
  /* Configuration & Callbacks */
  hdlc_tx_byte_cb_t tx_cb;  /**< Hardware TX callback. */
  hdlc_on_frame_cb_t rx_cb; /**< Application RX callback. */
  void *user_data;          /**< User context passed to callbacks. */

  /* Protocol Logic State */
  hdlc_state_t state; /**< Connection state (ABM). */
  hdlc_u8 seq_ns;     /**< Send Sequence Number N(S). */
  hdlc_u8 seq_nr;     /**< Receive Sequence Number N(R). */

  /* Receiver Engine State */
  hdlc_u8 rx_state;  /**< Current internal parser state (enum from private). */
  hdlc_u16 rx_index; /**< Current buffer write index. */
  hdlc_u16 rx_crc;   /**< Running CRC calculation. */
  hdlc_frame_t rx_frame; /**< Buffer for the frame currently being received. */

  /* Transmitter Engine State */
  hdlc_u16 tx_crc; /**< Running CRC for streaming TX. */

  /* Statistics */
  hdlc_u32 stats_rx_frames; /**< Count of valid frames received. */
  hdlc_u32 stats_tx_frames; /**< Count of frames transmitted. */
  hdlc_u32
      stats_crc_errors; /**< Count of frames discarded due to CRC mismatch. */
} hdlc_context_t;

#ifdef __cplusplus
}
#endif

#endif // HDLC_TYPES_H
