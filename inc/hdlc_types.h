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
 * DEFINITIONS
 * --------------------------------------------------------------------------
 */
/**
 * @brief HDLC Broadcast Address.
 *
 * Frames sent to this address are processed by all stations but
 * never generate a response (ACK/UA).
 */
#ifndef HDLC_BROADCAST_ADDRESS
#define HDLC_BROADCAST_ADDRESS 0xFF
#endif

#define HDLC_FLAG_LEN               (1)     /**< Flag. */
#define HDLC_ADDRESS_LEN            (1)     /**< Address Field. */
#define HDLC_CONTROL_LEN            (1)     /**< Control Field. */
#define HDLC_FCS_LEN                (2)     /**< FCS Field. */

/** Minimum frame length: Address(1) + Control(1) + FCS(2). */
#define HDLC_MIN_FRAME_LEN          (HDLC_ADDRESS_LEN + HDLC_CONTROL_LEN + HDLC_FCS_LEN)

/* Frame Type Masks & Values */
#define HDLC_FRAME_TYPE_MASK_I      (0x01)
#define HDLC_FRAME_TYPE_VAL_I       (0x00)

#define HDLC_FRAME_TYPE_MASK_S      (0x03)
#define HDLC_FRAME_TYPE_VAL_S       (0x01)

#define HDLC_FRAME_TYPE_MASK_U      (0x03)
#define HDLC_FRAME_TYPE_VAL_U       (0x03)


/*
 * --------------------------------------------------------------------------
 * BASIC TYPE DEFINITIONS
 * --------------------------------------------------------------------------
 */

/** @brief 8-bit unsigned integer type. */
typedef uint_least8_t hdlc_u8;
/** @brief 16-bit unsigned integer type. */
typedef uint_least16_t hdlc_u16;
/** @brief 32-bit unsigned integer type. */
typedef uint_least32_t hdlc_u32;
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

    /** Information Frame (I-Frame) [0 N(S) P/F N(R)] */
    struct {
        hdlc_u8 frame_type_0 : 1; /**< Frame Type ID (Must be 0). */
        hdlc_u8 ns           : 3; /**< Send Sequence Number N(S). */
        hdlc_u8 pf           : 1; /**< Poll/Final Bit. */
        hdlc_u8 nr           : 3; /**< Receive Sequence Number N(R). */
    } i_frame;

    /** Supervisory Frame (S-Frame) [1 0 S S P/F N(R)] */
    struct {
        hdlc_u8 frame_type_0 : 1; /**< Frame Type ID (Must be 1). */
        hdlc_u8 frame_type_1 : 1; /**< Frame Type ID (Must be 0). */
        hdlc_u8 s            : 2; /**< Supervisory function bits. */
        hdlc_u8 pf           : 1; /**< Poll/Final Bit. */
        hdlc_u8 nr           : 3; /**< Receive Sequence Number N(R). */
    } s_frame;

    /** Unnumbered Frame (U-Frame) [1 1 M M P/F M M M] */
    struct {
        hdlc_u8 frame_type_0 : 1; /**< Frame Type ID (Must be 1). */
        hdlc_u8 frame_type_1 : 1; /**< Frame Type ID (Must be 1). */
        hdlc_u8 m_lo         : 2; /**< Modifier function bits (low). */
        hdlc_u8 pf           : 1; /**< Poll/Final Bit. */
        hdlc_u8 m_hi         : 3; /**< Modifier function bits (high). */
    } u_frame;

} hdlc_control_t;

typedef union {
    hdlc_u8 fcs[2];
} hdlc_fcs_t;

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
    hdlc_u8 address;                /**< Address Field. */
    hdlc_control_t control;         /**< Control Field. */
    hdlc_u8 *information;           /**< Pointer to Information Field (Payload). */
    hdlc_u16 information_len;       /**< Length of valid data in information. */
    hdlc_frame_type_t type;         /**< Resolved Frame Type (I/S/U). */
} hdlc_frame_t;

/**
 * @brief Frame Reject (FRMR) Information Fields.
 * Standard format for the information field of an FRMR response.
 */
typedef struct {
    hdlc_u16 rejected_control; /**< Copy of the rejected control field. */
    hdlc_u8 v_s;               /**< Current Send Sequence Number V(S). */
    hdlc_u8 v_r;               /**< Current Receive Sequence Number V(R). */
    hdlc_bool cr;              /**< Command/Response flag. */
    struct {
        hdlc_bool w; /**< Control field undefined/unimplemented. */
        hdlc_bool x; /**< Info field not allowed with this frame. */
        hdlc_bool y; /**< Info field too long. */
        hdlc_bool z; /**< Invalid N(R). */
        hdlc_bool v; /**< Invalid N(S). */
    } errors;
} hdlc_frmr_data_t;

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
    HDLC_PROTOCOL_STATE_DISCONNECTED, /**< No logical connection. Messages ignored except SABM. */
    HDLC_PROTOCOL_STATE_CONNECTING,   /**< SABM sent, waiting for UA. */
    HDLC_PROTOCOL_STATE_CONNECTED,    /**< Logical connection established. Ready for I-frames. */
    HDLC_PROTOCOL_STATE_DISCONNECTING /**< DISC sent, waiting for UA. */
} hdlc_protocol_state_t;

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
typedef void (*hdlc_output_byte_cb_t)(hdlc_u8 byte, hdlc_bool flush, void *user_data);

/**
 * @brief Frame Received Callback.
 *
 * Function pointer type for notifying the application of a valid received
 * frame.
 * @param frame     Pointer to the fully parsed HDLC frame structure.
 * @param user_data Pointer to user-defined context data.
 */
typedef void (*hdlc_on_frame_cb_t)(const hdlc_frame_t* frame, void *user_data);

/**
 * @brief Connection State Change Callback.
 *
 * Notifies the application when the logical connection state changes
 * (e.g., Connected, Disconnected).
 *
 * @param state     The new state of the connection.
 * @param user_data Pointer to user-defined context data.
 */
typedef void (*hdlc_on_state_change_cb_t)(hdlc_protocol_state_t state, void *user_data);

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
    hdlc_output_byte_cb_t output_byte_cb;   /**< Hardware TX callback. */
    hdlc_on_frame_cb_t on_frame_cb;         /**< Application RX callback. */
    hdlc_on_state_change_cb_t on_state_change_cb; /**< State change callback. */
    void *user_data;                        /**< User context passed to callbacks. */

    /* Protocol Logic State */
    volatile hdlc_protocol_state_t current_state; /**< Current connection state. */
    hdlc_u8 my_address;                           /**< Local station address. */
    hdlc_u8 peer_address;                         /**< Remote station address. */

    /* Reliable Transmission State (Window Size = 1) */
    hdlc_u8 vs;                 /**< Send State Variable V(S). Sequence number of next I-frame to send. */
    hdlc_u8 vr;                 /**< Receive State Variable V(R). Sequence number of next expected I-frame. */
    hdlc_bool ack_pending;      /**< Flag indicating an acknowledgement is pending. */
    
    /* Retransmission Buffer (Window Size = 1) */
    hdlc_u8 *retransmit_buffer; /**< Buffer holding the last sent I-frame payload for retransmission. */
    hdlc_u32 retransmit_buffer_len; /**< Maximum length of the retransmit buffer. */
    hdlc_u32 retransmit_len;    /**< Length of the data in retransmit_buffer. */
    hdlc_bool waiting_for_ack;  /**< True if we are waiting for an ACK for the buffered frame. */
    hdlc_u32 retransmit_timer_ms; /**< Timer for retransmission (counts down). */

    /* Receiver Engine State */
    hdlc_u8 input_state;        /**< Current internal parser state. */
    hdlc_u8 *input_buffer;      /**< Pointer to the user-supplied RX buffer. */
    hdlc_u32 input_buffer_len;  /**< Length of the user-supplied RX buffer. */
    hdlc_u32 input_index;       /**< Current write index in rx_buffer. */
    hdlc_u16 input_crc;         /**< Running RX CRC. */
    hdlc_frame_t input_frame_buffer;   /**< Temporary frame descriptor passed to callback. */

    /* Transmitter Engine State */
    hdlc_u16 output_crc; /**< Running CRC for streaming TX. */

    /* Statistics */
    hdlc_u32 stats_input_frames; /**< Count of valid frames received. */
    hdlc_u32 stats_output_frames; /**< Count of frames transmitted. */
    hdlc_u32 stats_crc_errors; /**< Count of frames discarded due to CRC mismatch. */
} hdlc_context_t;

#ifdef __cplusplus
}
#endif

#endif // HDLC_TYPES_H
