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
 * @file hdlc_types.h
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief Core Data Types and Structures for the HDLC Library.
 *
 * This file defines the fundamental data types, enumerations, and structures
 * used throughout the HDLC protocol stack, including Frame structures,
 * Control Field definitions, and the main Context structure.
 */

#ifndef ATC_HDLC_TYPES_H
#define ATC_HDLC_TYPES_H

#include "hdlc_config.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * --------------------------------------------------------------------------
 * DEFINITIONS
 * --------------------------------------------------------------------------
 */
/**
 * @brief HDLC Broadcast Address.
 *
 * Frames sent to this address are processed by all stations but
 * never generate a response (ACK/UA).
 */
#ifndef ATC_HDLC_BROADCAST_ADDRESS
#define ATC_HDLC_BROADCAST_ADDRESS 0xFF
#endif






/*
 * --------------------------------------------------------------------------
 * BASIC TYPE DEFINITIONS
 * --------------------------------------------------------------------------
 */

/** @brief 8-bit unsigned integer type. */
typedef uint_least8_t atc_hdlc_u8;
/** @brief 16-bit unsigned integer type. */
typedef uint_least16_t atc_hdlc_u16;
/** @brief 32-bit unsigned integer type. */
typedef uint_least32_t atc_hdlc_u32;
/** @brief Boolean type. */
typedef bool atc_hdlc_bool;

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
    ATC_HDLC_FRAME_I,      /**< Information Frame (Data transfer) */
    ATC_HDLC_FRAME_S,      /**< Supervisory Frame (Flow/Error control) */
    ATC_HDLC_FRAME_U,      /**< Unnumbered Frame (Link management) */
    ATC_HDLC_FRAME_INVALID /**< Invalid or Unknown Frame format */
} atc_hdlc_frame_type_t;

/**
 * @brief HDLC Supervisory (S) Frame Sub-Types.
 */
typedef enum {
    ATC_HDLC_S_FRAME_TYPE_RR,      /**< Receive Ready */
    ATC_HDLC_S_FRAME_TYPE_RNR,     /**< Receive Not Ready */
    ATC_HDLC_S_FRAME_TYPE_REJ,     /**< Reject */
    ATC_HDLC_S_FRAME_TYPE_UNKNOWN  /**< Unknown or Invalid S-Frame */
} atc_hdlc_s_frame_sub_type_t;

/**
 * @brief HDLC Unnumbered (U) Frame Sub-Types.
 */
typedef enum {
    ATC_HDLC_U_FRAME_TYPE_SABM,    /**< Set Asynchronous Balanced Mode */
    ATC_HDLC_U_FRAME_TYPE_SNRM,    /**< Set Normal Response Mode */
    ATC_HDLC_U_FRAME_TYPE_SARM,    /**< Set Asynchronous Response Mode (DM command) */
    ATC_HDLC_U_FRAME_TYPE_SABME,   /**< Set Asynchronous Balanced Mode Extended */
    ATC_HDLC_U_FRAME_TYPE_SNRME,   /**< Set Normal Response Mode Extended */
    ATC_HDLC_U_FRAME_TYPE_SARME,   /**< Set Asynchronous Response Mode Extended */
    ATC_HDLC_U_FRAME_TYPE_DISC,    /**< Disconnect */
    ATC_HDLC_U_FRAME_TYPE_UA,      /**< Unnumbered Acknowledgment */
    ATC_HDLC_U_FRAME_TYPE_DM,      /**< Disconnect Mode */
    ATC_HDLC_U_FRAME_TYPE_FRMR,    /**< Frame Reject */
    ATC_HDLC_U_FRAME_TYPE_UI,      /**< Unnumbered Information */
    ATC_HDLC_U_FRAME_TYPE_TEST,    /**< Test */
    ATC_HDLC_U_FRAME_TYPE_UNKNOWN  /**< Unknown or Invalid U-Frame */
} atc_hdlc_u_frame_sub_type_t;

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
    atc_hdlc_u8 value; /**< Raw 8-bit Control Byte value. */

    /** Information Frame (I-Frame) [0 N(S) P/F N(R)] */
    struct {
        atc_hdlc_u8 frame_type_0 : 1; /**< Frame Type ID (Must be 0). */
        atc_hdlc_u8 ns           : 3; /**< Send Sequence Number N(S). */
        atc_hdlc_u8 pf           : 1; /**< Poll/Final Bit. */
        atc_hdlc_u8 nr           : 3; /**< Receive Sequence Number N(R). */
    } i_frame;

    /** Supervisory Frame (S-Frame) [1 0 S S P/F N(R)] */
    struct {
        atc_hdlc_u8 frame_type_0 : 1; /**< Frame Type ID (Must be 1). */
        atc_hdlc_u8 frame_type_1 : 1; /**< Frame Type ID (Must be 0). */
        atc_hdlc_u8 s            : 2; /**< Supervisory function bits. */
        atc_hdlc_u8 pf           : 1; /**< Poll/Final Bit. */
        atc_hdlc_u8 nr           : 3; /**< Receive Sequence Number N(R). */
    } s_frame;

    /** Unnumbered Frame (U-Frame) [1 1 M M P/F M M M] */
    struct {
        atc_hdlc_u8 frame_type_0 : 1; /**< Frame Type ID (Must be 1). */
        atc_hdlc_u8 frame_type_1 : 1; /**< Frame Type ID (Must be 1). */
        atc_hdlc_u8 m_lo         : 2; /**< Modifier function bits (low). */
        atc_hdlc_u8 pf           : 1; /**< Poll/Final Bit. */
        atc_hdlc_u8 m_hi         : 3; /**< Modifier function bits (high). */
    } u_frame;

} atc_hdlc_control_t;



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
    atc_hdlc_u8 address;                /**< Address Field. */
    atc_hdlc_control_t control;         /**< Control Field. */
    atc_hdlc_u8 *information;           /**< Pointer to Information Field (Payload). */
    atc_hdlc_u16 information_len;       /**< Length of valid data in information. */
    atc_hdlc_frame_type_t type;         /**< Resolved Frame Type (I/S/U). */
} atc_hdlc_frame_t;



/*
 * --------------------------------------------------------------------------
 * PROTOCOL STATES
 * --------------------------------------------------------------------------
 */

/**
 * @brief HDLC Protocol States.
 *
 * Defines the connection state of the HDLC station.
 */
typedef enum {
    ATC_HDLC_PROTOCOL_STATE_DISCONNECTED, /**< No logical connection. Messages ignored except SABM. */
    ATC_HDLC_PROTOCOL_STATE_CONNECTING,   /**< SABM sent, waiting for UA. */
    ATC_HDLC_PROTOCOL_STATE_CONNECTED,    /**< Logical connection established. Ready for I-frames. */
    ATC_HDLC_PROTOCOL_STATE_DISCONNECTING /**< DISC sent, waiting for UA. */
} atc_hdlc_protocol_state_t;

/**
 * @brief HDLC State Change Event Types.
 *
 * Provides the reason/cause for a protocol state transition,
 * allowing the application to distinguish between different
 * scenarios that lead to the same state.
 */
typedef enum {
    /* Connection Events */
    ATC_HDLC_EVENT_CONNECT_REQUEST,    /**< Local: atc_hdlc_connect() was called. */
    ATC_HDLC_EVENT_CONNECT_ACCEPTED,   /**< UA received in response to our SABM. */
    ATC_HDLC_EVENT_INCOMING_CONNECT,   /**< Peer sent SABM — passive open. */

    /* Disconnection Events */
    ATC_HDLC_EVENT_DISCONNECT_REQUEST, /**< Local: atc_hdlc_disconnect() was called. */
    ATC_HDLC_EVENT_DISCONNECT_COMPLETE,/**< UA received in response to our DISC. */
    ATC_HDLC_EVENT_PEER_DISCONNECT,    /**< Peer sent DISC. */
    ATC_HDLC_EVENT_PEER_REJECT,        /**< Peer sent DM — connection rejected. */
    ATC_HDLC_EVENT_PROTOCOL_ERROR,     /**< Peer sent FRMR — irrecoverable protocol violation. */
    ATC_HDLC_EVENT_LINK_FAILURE,       /**< Max retry count (N2) exceeded — link timeout. */
} atc_hdlc_event_t;

/*
 * --------------------------------------------------------------------------
 * CALLBACK DEFINITIONS
 * --------------------------------------------------------------------------
 */

/**
 * @brief Output Byte Callback.
 *
 * Function pointer type for the hardware transmission interface.
 * @param byte      The byte to send over the physical medium (UART).
 * @param flush     Indicates if this is the last byte of the frame (End Flag).
 *                  Can be used to trigger hardware buffer flush.
 * @param user_data Pointer to user-defined context data.
 */
typedef void (*atc_hdlc_output_byte_cb_t)(atc_hdlc_u8 byte, atc_hdlc_bool flush, void *user_data);

/**
 * @brief Frame Received Callback.
 *
 * Function pointer type for notifying the application of a valid received
 * frame.
 * @param frame     Pointer to the fully parsed HDLC frame structure.
 * @param user_data Pointer to user-defined context data.
 */
typedef void (*atc_hdlc_on_frame_cb_t)(const atc_hdlc_frame_t* frame, void *user_data);

/**
 * @brief Connection State Change Callback.
 *
 * Notifies the application when the logical connection state changes
 * (e.g., Connected, Disconnected) along with the event that caused
 * the transition.
 *
 * @param state     The new state of the connection.
 * @param event     The event/reason that triggered this state change.
 * @param user_data Pointer to user-defined context data.
 */
typedef void (*atc_hdlc_on_state_change_cb_t)(atc_hdlc_protocol_state_t state, atc_hdlc_event_t event, void *user_data);

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
    atc_hdlc_output_byte_cb_t output_byte_cb;   /**< Hardware TX callback. */
    atc_hdlc_on_frame_cb_t on_frame_cb;         /**< Application RX callback. */
    atc_hdlc_on_state_change_cb_t on_state_change_cb; /**< State change callback. */
    void *user_data;                        /**< User context passed to callbacks. */

    /* Protocol Logic State */
    volatile atc_hdlc_protocol_state_t current_state; /**< Current connection state. */
    atc_hdlc_u8 my_address;                           /**< Local station address. */
    atc_hdlc_u8 peer_address;                         /**< Remote station address. */

    /* Reliable Transmission State (Go-Back-N) */
    atc_hdlc_u8 vs;                 /**< Send State Variable V(S). Sequence number of next I-frame to send. */
    atc_hdlc_u8 vr;                 /**< Receive State Variable V(R). Sequence number of next expected I-frame. */
    atc_hdlc_u8 va;                 /**< Acknowledge State Variable V(A). Oldest unacknowledged sequence number. */
    atc_hdlc_u8 window_size;        /**< Transmit window size (1..7). */
    atc_hdlc_u32 ack_timer;         /**< Timer for delayed ACK (counts down in ticks). 0 means no ACK pending. */
    atc_hdlc_u32 ack_delay_timeout; /**< Configurable ACK delay timeout period in ticks. */
    atc_hdlc_bool rej_exception;    /**< REJ exception condition. Prevents duplicate REJ retransmission. */
    
    /* Connection Management State */
    atc_hdlc_u32 contention_timer;  /**< Timer for SABM contention resolution delay. */
    
    /* Retransmission Buffer (Go-Back-N, slotted) */
    atc_hdlc_u8 *retransmit_buffer; /**< User-supplied buffer, divided into window_size equal slots. */
    atc_hdlc_u32 retransmit_buffer_len; /**< Total length of the retransmit buffer. */
    atc_hdlc_u32 retransmit_slot_size;  /**< Max payload per slot (retransmit_buffer_len / window_size). */
    atc_hdlc_u32 retransmit_lens[8];    /**< Payload length stored per slot. */
    atc_hdlc_u8 tx_seq_to_slot[8];      /**< Dynamic mapping: Sequence number V(S) to physical buffer slot index. */
    atc_hdlc_u8 next_tx_slot;           /**< The next available physical slot index (0 to window_size-1). */
    atc_hdlc_u32 retransmit_timer; /**< Timer for retransmission (counts down in ticks). */
    atc_hdlc_u32 retransmit_timeout; /**< Configurable retransmission timeout period in ticks. */
    atc_hdlc_u8 max_retry_count;      /**< Maximum number of retransmissions before link failure (N2). */
    atc_hdlc_u8 retry_count;          /**< Current retransmission count. */

    /* Receiver Engine State */
    atc_hdlc_u8 input_state;        /**< Current internal parser state. */
    atc_hdlc_u8 *input_buffer;      /**< Pointer to the user-supplied RX buffer. */
    atc_hdlc_u32 input_buffer_len;  /**< Length of the user-supplied RX buffer. */
    atc_hdlc_u32 input_index;       /**< Current write index in rx_buffer. */

    atc_hdlc_frame_t input_frame_buffer;   /**< Temporary frame descriptor passed to callback. */

    /* Transmitter Engine State */
    atc_hdlc_u16 output_crc; /**< Running CRC for streaming TX. */

    /* Statistics */
    atc_hdlc_u32 stats_input_frames; /**< Count of valid frames received. */
    atc_hdlc_u32 stats_output_frames; /**< Count of frames transmitted. */
    atc_hdlc_u32 stats_crc_errors; /**< Count of frames discarded due to CRC mismatch. */
} atc_hdlc_context_t;

#ifdef __cplusplus
}
#endif

#endif // ATC_HDLC_TYPES_H
