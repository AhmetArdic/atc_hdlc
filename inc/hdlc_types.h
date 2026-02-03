#ifndef HDLC_TYPES_H
#define HDLC_TYPES_H

#include "hdlc_config.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Basic Types */
typedef uint8_t hdlc_u8;
typedef uint16_t hdlc_u16;
typedef uint32_t hdlc_u32;

/**
 * @brief HDLC Frame Types
 */
typedef enum {
  HDLC_FRAME_I,      /* Information Transfer */
  HDLC_FRAME_S,      /* Supervisory (Flow Control) */
  HDLC_FRAME_U,      /* Unnumbered (Link Control) */
  HDLC_FRAME_INVALID /* Parse Error / Invalid */
} hdlc_frame_type_t;

/**
 * @brief Protocol State (Connection Status)
 */
typedef enum {
  HDLC_STATE_DISCONNECTED,
  HDLC_STATE_CONNECTING, /* Sent SABM, waiting for UA */
  HDLC_STATE_CONNECTED,
  HDLC_STATE_DISCONNECTING /* Sent DISC, waiting for UA */
} hdlc_protocol_state_t;

/**
 * @brief Control Field Structure (8-bit Classic)
 *
 * Bit allocations:
 * I-Frame: N(R)[3] | P/F[1] | N(S)[3] | 0[1]
 * S-Frame: N(R)[3] | P/F[1] | S[2]    | 01[2]
 * U-Frame: M[3]    | P/F[1] | M[2]    | 11[2]
 */
typedef union {
  hdlc_u8 value;
  struct {
    hdlc_u8 bit0 : 1;
    hdlc_u8 bit1 : 1;
    hdlc_u8 poll_final : 1;
    hdlc_u8 other : 5;
  } bits;
} hdlc_control_t;

/**
 * @brief Generic Frame Structure
 *
 * Used for buffering constructed frames before transmission or
 * holding received frames after parsing.
 */
typedef struct {
  hdlc_u8 address;
  hdlc_control_t control;
  hdlc_u8 payload[HDLC_MAX_MTU];
  hdlc_u16 payload_len;
  hdlc_frame_type_t type; /* Metadata: Helper for higher layers */
} hdlc_frame_t;

/* Forward Declaration */
// struct hdlc_context_s;
// typedef struct hdlc_context_s hdlc_context_t;

/**
 * @brief Receiver Parse State Machine
 */
typedef enum {
  HDLC_RX_HUNT,    /* Waiting for Flag (0x7E) */
  HDLC_RX_FLAG,    /* Seen Flag, waiting for start of frame */
  HDLC_RX_ADDRESS, /* Reading Address */
  HDLC_RX_CONTROL, /* Reading Control */
  HDLC_RX_DATA,    /* Reading Payload/FCS */
  HDLC_RX_ESCAPE   /* Seen Escape (0x7D), next byte is escaped */
} hdlc_rx_state_t;

/**
 * @brief Main HDLC Context
 */
typedef struct hdlc_context_s {
  /* Configuration */
  hdlc_u8 my_address;
  void (*tx_cb)(void *user_data, hdlc_u8 byte);
  void (*rx_cb)(void *user_data, const hdlc_frame_t *frame);
  void (*state_cb)(void *user_data, hdlc_protocol_state_t new_state);
  void *user_data;

  /* Receiver State */
  hdlc_rx_state_t rx_state;
  hdlc_frame_t rx_frame; /* Being assembled */
  hdlc_u16 rx_index;     /* Current index in payload */
  hdlc_u16 rx_crc;       /* Running CRC */
  bool rx_frame_valid;

  /* Transmit State (Streaming) */
  hdlc_u16 tx_crc;

  /* Protocol State (Infrastructure) */
  hdlc_protocol_state_t state;
  hdlc_u8 seq_ns; /* Send Sequence Number V(S) */
  hdlc_u8 seq_nr; /* Receive Sequence Number V(R) */

  /* Stats */
  hdlc_u32 stats_rx_frames;
  hdlc_u32 stats_tx_frames;
  hdlc_u32 stats_crc_errors;
} hdlc_context_t;

/* Typedefs for callbacks (already defined below in original file? No, I need to
 * define them BEFORE or inside) */
/* Actually the callbacks were typedef'd AFTER the forward decl in the original
 * file. */
/* Let's check original file content first to be clean. */
/* I will just use void* function pointers in the struct above to avoid
   dependency issues or move the typedefs UP. Original file had typedefs AFTER
   the struct forward decl. I'll use the typedef names if I move the typedefs
   UP. But `replace_file_content` is a patch. I'll replace the block including
   the typedefs.
*/

/**
 * @brief Output Byte Callback
 *
 * The user must provide this function to actually send bytes to the UART.
 */
typedef void (*hdlc_tx_byte_cb_t)(void *user_data, hdlc_u8 byte);

/**
 * @brief Frame Received Callback
 *
 * Called by the library when a valid frame is fully parsed.
 */
typedef void (*hdlc_on_frame_cb_t)(void *user_data, const hdlc_frame_t *frame);

/**
 * @brief Connection Status Callback
 *
 * Called when the link state changes (e.g. Connected, Disconnected).
 */
typedef void (*hdlc_on_state_change_cb_t)(void *user_data,
                                          hdlc_protocol_state_t new_state);

#ifdef __cplusplus
}
#endif

#endif // HDLC_TYPES_H
