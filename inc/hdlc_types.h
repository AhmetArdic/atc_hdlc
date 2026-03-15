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
 * @brief Core data types and structures for the HDLC library.
 *
 * Defines all fundamental types, enumerations, and structures used
 * throughout the HDLC protocol stack: primitive types, frame descriptors,
 * error codes, configuration, platform integration, buffer descriptors,
 * statistics, and the main station context.
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
    atc_hdlc_u8 *information;           /**< Pointer to Information Field (Payload). */
    atc_hdlc_frame_type_t type;         /**< Resolved Frame Type (I/S/U). */
    atc_hdlc_u16 information_len;       /**< Length of valid data in information. */
    atc_hdlc_u8 address;                /**< Address Field. */
    atc_hdlc_u8 control;                /**< Control Field. */
} atc_hdlc_frame_t;



/*
 * --------------------------------------------------------------------------
 * PROTOCOL STATES
 * --------------------------------------------------------------------------
 */

/**
 * @brief HDLC station state machine states (ISO/IEC 13239 §6).
 *
 * Five primary states reflecting the true connection lifecycle. Transient
 * conditions that occur *within* CONNECTED (peer busy, local busy,
 * reject-recovery) are modelled as boolean flags in the context struct
 * (@ref atc_hdlc_context_t::remote_busy, ::local_busy, ::rej_exception)
 * rather than separate states, which matches the standard more faithfully.
 *
 * | State          | Meaning                                                    |
 * |----------------|------------------------------------------------------------|
 * | DISCONNECTED   | No logical connection; only SABM/UI/TEST are processed.   |
 * | CONNECTING     | SABM sent, awaiting UA; T1 running.                       |
 * | CONNECTED      | Active session; all I/S/U frames processed normally.       |
 * | FRMR_ERROR     | Irrecoverable protocol error; only reset/disconnect valid. |
 * | DISCONNECTING  | DISC sent, awaiting UA; T1 running.                       |
 *
 * Sub-conditions inside CONNECTED are tracked by context flags:
 *  - @ref atc_hdlc_context_t::remote_busy  — peer sent RNR, TX suspended.
 *  - @ref atc_hdlc_context_t::local_busy   — local RNR sent, peer TX throttled.
 *  - @ref atc_hdlc_context_t::rej_exception — REJ sent, Go-Back-N in progress.
 */
typedef enum {
    ATC_HDLC_STATE_DISCONNECTED,   /**< No logical connection. */
    ATC_HDLC_STATE_CONNECTING,     /**< SABM sent, awaiting UA; T1 running. */
    ATC_HDLC_STATE_CONNECTED,      /**< Active data transfer. */
    ATC_HDLC_STATE_FRMR_ERROR,     /**< Irrecoverable error; only reset or disconnect allowed. */
    ATC_HDLC_STATE_DISCONNECTING,  /**< DISC sent, awaiting UA; T1 running. */
} atc_hdlc_state_t;


/**
 * @brief HDLC asynchronous event types.
 *
 * Delivered via @ref atc_hdlc_platform_t::on_event to notify the upper layer
 * of connection lifecycle changes, flow control transitions, and diagnostic
 * results. The @c on_event callback receives one of these values every time a
 * noteworthy condition occurs inside the station.
 */
typedef enum {
    /* Connection lifecycle */
    ATC_HDLC_EVENT_LINK_SETUP_REQUEST, /**< Local: atc_hdlc_link_setup() called; SABM sent. */
    ATC_HDLC_EVENT_CONNECT_ACCEPTED,   /**< UA received in response to our SABM. */
    ATC_HDLC_EVENT_INCOMING_CONNECT,   /**< Peer sent SABM — passive open accepted. */
    ATC_HDLC_EVENT_RESET,              /**< Link reset initiated (new SABM sent after internal reset). */
    ATC_HDLC_EVENT_DISCONNECT_REQUEST, /**< Local: atc_hdlc_disconnect() called; DISC sent. */
    ATC_HDLC_EVENT_DISCONNECT_COMPLETE,/**< UA received in response to our DISC. */
    ATC_HDLC_EVENT_PEER_DISCONNECT,    /**< Peer sent DISC — connection closed by remote. */
    ATC_HDLC_EVENT_PEER_REJECT,        /**< Peer sent DM — connection rejected. */

    /* Error events */
    ATC_HDLC_EVENT_PROTOCOL_ERROR,     /**< Peer sent FRMR — irrecoverable protocol violation. */
    ATC_HDLC_EVENT_LINK_FAILURE,       /**< N2 retry limit exceeded — link declared failed. */

    /* Flow control events */
    ATC_HDLC_EVENT_REMOTE_BUSY_ON,     /**< Peer sent RNR; outgoing I-frame transmission suspended. */
    ATC_HDLC_EVENT_REMOTE_BUSY_OFF,    /**< Peer sent RR after RNR; transmission resumed. */
    ATC_HDLC_EVENT_WINDOW_OPEN,        /**< TX window slot freed; application may send again. */

    /* Diagnostic events */
    ATC_HDLC_EVENT_TEST_RESULT,        /**< TEST frame round-trip complete; check test_result field. */
} atc_hdlc_event_t;

/*
 * --------------------------------------------------------------------------
 * ERROR CODES
 * --------------------------------------------------------------------------
 */

/**
 * @brief HDLC library error codes.
 *
 * Returned by all public API functions. Zero indicates success; all error
 * values are negative so callers can test with a simple @c < 0 check.
 *
 * @note Granularity is intentionally coarse at the public boundary. Internal
 *       helpers may use additional sentinel values that are never exposed.
 */
typedef enum {
    ATC_HDLC_OK                       =  0, /**< Operation completed successfully. */

    /* Frame errors (-1 .. -2) */
    ATC_HDLC_ERR_FCS                  = -1, /**< FCS mismatch — frame discarded. */
    ATC_HDLC_ERR_SHORT_FRAME          = -2, /**< Frame too short to be valid. */

    /* Resource errors (-3 .. -4) */
    ATC_HDLC_ERR_BUFFER_FULL          = -3, /**< Destination buffer is full. */
    ATC_HDLC_ERR_NO_BUFFER            = -4, /**< Required buffer not provided. */

    /* Protocol errors (-5 .. -7) */
    ATC_HDLC_ERR_SEQUENCE             = -5, /**< Sequence number out of range. */
    ATC_HDLC_ERR_INVALID_COMMAND      = -6, /**< Received unsupported/invalid command. */
    ATC_HDLC_ERR_FRMR                 = -7, /**< Irrecoverable protocol error (FRMR). */

    /* State errors (-8 .. -9) */
    ATC_HDLC_ERR_INVALID_STATE        = -8, /**< Operation not permitted in current state. */
    ATC_HDLC_ERR_UNSUPPORTED_MODE     = -9, /**< Requested mode not implemented. */

    /* Timing errors (-10) */
    ATC_HDLC_ERR_MAX_RETRY            = -10, /**< N2 retry limit exceeded — link failed. */

    /* Parameter errors (-11 .. -12) */
    ATC_HDLC_ERR_INVALID_PARAM        = -11, /**< NULL or out-of-range parameter. */
    ATC_HDLC_ERR_INCONSISTENT_BUFFER  = -12, /**< Buffer geometry violates constraints. */

    /* Flow control errors (-13 .. -16) */
    ATC_HDLC_ERR_REMOTE_BUSY          = -13, /**< Peer is busy (RNR received); TX suspended. */
    ATC_HDLC_ERR_WINDOW_FULL          = -14, /**< TX window full; no free slots. */
    ATC_HDLC_ERR_FRAME_TOO_LARGE      = -15, /**< Payload exceeds max_frame_size (MRU). */
    ATC_HDLC_ERR_TEST_PENDING         = -16, /**< A TEST frame is already awaiting response. */
} atc_hdlc_error_t;

/*
 * --------------------------------------------------------------------------
 * LINK MODES & STATION ROLES (HDLC Operational Models)
 * --------------------------------------------------------------------------
 */

/**
 * @brief HDLC Operational Link Modes.
 * Specifies the connection and polling behavior.
 */
typedef enum {
    ATC_HDLC_MODE_ABM = 0, /**< Asynchronous Balanced Mode (SABM). Combined stations. */
    ATC_HDLC_MODE_NRM = 1, /**< Normal Response Mode (SNRM). Primary polls Secondary. */
    ATC_HDLC_MODE_ARM = 2  /**< Asynchronous Response Mode (SARM). */
} atc_hdlc_link_mode_t;

/**
 * @brief HDLC Station Roles.
 * Dictates command/response behavior and contention priorities.
 */
typedef enum {
    ATC_HDLC_ROLE_COMBINED = 0, /**< Equal rights (used in ABM). */
    ATC_HDLC_ROLE_PRIMARY  = 1, /**< Master node (sends Commands, receives Responses). */
    ATC_HDLC_ROLE_SECONDARY= 2  /**< Slave node (receives Commands, sends Responses). */
} atc_hdlc_station_role_t;

/*
 * --------------------------------------------------------------------------
 * CONFIGURATION
 * --------------------------------------------------------------------------
 */

/**
 * @brief HDLC station protocol configuration.
 *
 * Passed to @ref atc_hdlc_init() and kept alive (by the caller) for the
 * entire lifetime of the station context. The library stores a pointer to
 * this struct — it does not copy it.
 *
 * All timer values are in milliseconds. The library converts them to internal
 * tick counts using the tick period implied by the call rate of
 * @ref atc_hdlc_tick().
 */
typedef struct {
    atc_hdlc_link_mode_t mode;        /**< Operating mode. First release: @c ATC_HDLC_MODE_ABM only. */
    atc_hdlc_u8          address;     /**< Local station address (1 octet). */
    atc_hdlc_u8          window_size; /**< Sliding window size, 1–7 (mod-8). */
    atc_hdlc_u32         max_frame_size; /**< Maximum information field size in octets (MRU). */
    atc_hdlc_u8          max_retries; /**< N2: maximum retransmission attempts before link failure. */
    atc_hdlc_u32         t1_ms;       /**< T1 retransmission timer in ms (typical 200–3000). */
    atc_hdlc_u32         t2_ms;       /**< T2 acknowledgement delay timer in ms (must be < t1_ms). */
    atc_hdlc_u32         t3_ms;       /**< T3 idle/keep-alive timer in ms (typical 10000–60000). */
    atc_hdlc_bool        use_extended;/**< Extended (mod-128) mode flag. Must be false in v1. */
} atc_hdlc_config_t;

/*
 * --------------------------------------------------------------------------
 * PLATFORM INTEGRATION
 * --------------------------------------------------------------------------
 */

/**
 * @brief Byte-output callback with error return.
 *
 * Called for every encoded octet during frame transmission. The @p flush
 * flag is set on the final octet of the closing flag (0x7E), signalling
 * that the complete frame has been written and any hardware FIFO/DMA buffer
 * may be flushed.
 *
 * @param byte     Encoded octet to transmit.
 * @param flush    True on the last octet of the frame (closing flag).
 * @param user_ctx Opaque pointer provided in @ref atc_hdlc_platform_t.
 * @return 0 on success, negative value on transmission error.
 */
typedef int (*atc_hdlc_send_fn)(atc_hdlc_u8 byte, atc_hdlc_bool flush, void *user_ctx);

/**
 * @brief Timer start callback.
 *
 * Called by the library when a timer must be started. The platform must
 * arrange for the corresponding @c atc_hdlc_t1/t2/t3_expired() function to
 * be called after @p ms milliseconds.
 *
 * @param ms       Timer duration in milliseconds.
 * @param user_ctx Opaque pointer provided in @ref atc_hdlc_platform_t.
 */
typedef void (*atc_hdlc_timer_start_fn)(atc_hdlc_u32 ms, void *user_ctx);

/**
 * @brief Timer stop callback.
 *
 * Called by the library when a running timer must be cancelled before it
 * expires. The platform must ensure the corresponding expired callback is
 * NOT called after this point.
 *
 * @param user_ctx Opaque pointer provided in @ref atc_hdlc_platform_t.
 */
typedef void (*atc_hdlc_timer_stop_fn)(void *user_ctx);

/**
 * @brief Verified payload delivery callback.
 *
 * Invoked once per accepted I-frame or UI/TEST information field after FCS
 * validation and sequence-number acceptance. The @p payload pointer refers
 * to the station's internal RX buffer; it is valid only for the duration of
 * this callback unless a zero-copy swap is performed.
 *
 * @param payload  Pointer to the decoded information field.
 * @param len      Length of the information field in octets.
 * @param user_ctx Opaque pointer provided in @ref atc_hdlc_platform_t.
 */
typedef void (*atc_hdlc_on_data_fn)(const atc_hdlc_u8 *payload,
                                     atc_hdlc_u16 len,
                                     void *user_ctx);

/**
 * @brief Asynchronous event notification callback.
 *
 * Fired whenever a noteworthy condition is detected inside the station
 * (connection lifecycle, flow control, errors, diagnostics). See
 * @ref atc_hdlc_event_t for the full list of events.
 *
 * @param event    The event that occurred.
 * @param user_ctx Opaque pointer provided in @ref atc_hdlc_platform_t.
 */
typedef void (*atc_hdlc_on_event_fn)(atc_hdlc_event_t event, void *user_ctx);

/**
 * @brief Platform integration descriptor.
 *
 * Groups all platform-dependent capabilities required by the station layer.
 * Passed to @ref atc_hdlc_init() and kept alive for the lifetime of the
 * context. The library stores a pointer — it does not copy this struct.
 *
 * Only @c send is mandatory. @c on_data and @c on_event may be NULL if the
 * application does not require payload delivery or event notifications.
 */
typedef struct {
    atc_hdlc_send_fn     on_send;   /**< Physical byte-output function (mandatory). */
    atc_hdlc_on_data_fn  on_data;   /**< Payload delivery to upper layer (optional). */
    atc_hdlc_on_event_fn on_event;  /**< Event notification to upper layer (optional). */
    void                *user_ctx;  /**< Opaque pointer forwarded to all callbacks. */

    /* Timer callbacks (all optional — NULL disables the timer) */
    atc_hdlc_timer_start_fn t1_start; /**< Start T1 retransmission timer. */
    atc_hdlc_timer_stop_fn  t1_stop;  /**< Stop T1. */
    atc_hdlc_timer_start_fn t2_start; /**< Start T2 delayed-ACK timer. */
    atc_hdlc_timer_stop_fn  t2_stop;  /**< Stop T2. */
    atc_hdlc_timer_start_fn t3_start; /**< Start T3 idle/keep-alive timer. */
    atc_hdlc_timer_stop_fn  t3_stop;  /**< Stop T3. */
} atc_hdlc_platform_t;

/*
 * --------------------------------------------------------------------------
 * BUFFER DESCRIPTORS
 * --------------------------------------------------------------------------
 */

/**
 * @brief TX retransmit window descriptor.
 *
 * Describes the user-allocated memory region used for Go-Back-N
 * retransmission. All three pointer arrays must have at least
 * @c slot_count elements and must remain valid for the lifetime of the
 * station context.
 *
 * Consistency requirements checked by @ref atc_hdlc_init():
 *  - @c slot_count == @c config->window_size
 *  - @c slot_capacity >= @c config->max_frame_size
 */
typedef struct {
    atc_hdlc_u8  *slots;        /**< Flat buffer for frame payloads: slot_count * slot_capacity octets. */
    atc_hdlc_u32 *slot_lens;    /**< Per-slot stored payload length (slot_count elements). */
    atc_hdlc_u8  *seq_to_slot;  /**< Sequence-number-to-slot-index mapping (slot_count elements). */
    atc_hdlc_u32  slot_capacity;/**< Capacity of a single slot in octets. */
    atc_hdlc_u8   slot_count;   /**< Total number of slots (must equal window_size). */
} atc_hdlc_tx_window_t;

/**
 * @brief RX buffer descriptor.
 *
 * Describes the user-allocated receive buffer. The buffer must remain valid
 * for the lifetime of the station context.
 *
 * Consistency requirement checked by @ref atc_hdlc_init():
 *  - @c capacity >= @c config->max_frame_size
 */
typedef struct {
    atc_hdlc_u8  *buffer;   /**< Pointer to the receive buffer. */
    atc_hdlc_u32  capacity; /**< Buffer capacity in octets. */
} atc_hdlc_rx_buffer_t;

/*
 * --------------------------------------------------------------------------
 * STATISTICS
 * --------------------------------------------------------------------------
 */

/**
 * @brief Runtime statistics snapshot.
 *
 * Collected by the station during normal operation. Can be read at any time
 * via @ref atc_hdlc_get_stats(). All counters are monotonically increasing
 * and wrap on overflow (no saturation).
 *
 * Compile-time macro @c ATC_HDLC_ENABLE_STATS (hdlc_config.h) controls
 * whether instrumentation code is included. When set to 0, all stat
 * increments compile to no-ops.
 */
typedef struct {
    /* Transmission */
    atc_hdlc_u32 tx_i_frames;            /**< I-frames successfully transmitted. */
    atc_hdlc_u32 tx_bytes;               /**< Information bytes transmitted. */
    /* Reception */
    atc_hdlc_u32 rx_i_frames;            /**< In-sequence I-frames accepted. */
    atc_hdlc_u32 rx_bytes;               /**< Information bytes received. */
    /* Errors */
    atc_hdlc_u32 fcs_errors;             /**< Frames discarded due to FCS mismatch. */
    atc_hdlc_u32 frmr_count;             /**< FRMR frames received from peer. */
    atc_hdlc_u32 timeout_count;          /**< T1 timeout occurrences (retransmission triggers). */
    /* Flow control */
    atc_hdlc_u32 rej_sent;               /**< REJ frames sent. */
    atc_hdlc_u32 rej_received;           /**< REJ frames received. */
    atc_hdlc_u32 rnr_sent;               /**< RNR frames sent (local busy asserted). */
    atc_hdlc_u32 rnr_received;           /**< RNR frames received (remote busy detected). */
    atc_hdlc_u32 local_busy_transitions; /**< Number of times local busy was asserted. */
    /* Diagnostics */
    atc_hdlc_u32 test_sent;              /**< TEST frames sent. */
    atc_hdlc_u32 test_success;           /**< TEST frames with a matching response received. */
    atc_hdlc_u32 test_failed;            /**< TEST frames that timed out or had payload mismatch. */
} atc_hdlc_stats_t;

/*
 * --------------------------------------------------------------------------
 * TEST RESULT
 * --------------------------------------------------------------------------
 */

/**
 * @brief Result of a TEST frame round-trip.
 *
 * Populated by the station and delivered via the @ref ATC_HDLC_EVENT_TEST_RESULT
 * event. The caller may read this from the context's @c test_result field
 * inside the @c on_event callback.
 */
typedef struct {
    atc_hdlc_bool success;      /**< True if the peer echoed the correct payload. */
    atc_hdlc_bool timed_out;    /**< True if T1 expired before a response arrived. */
    atc_hdlc_u16  payload_len;  /**< Length of the test pattern that was sent. */
} atc_hdlc_test_result_t;

/*
 * --------------------------------------------------------------------------
 * CONTEXT STRUCTURE
 * --------------------------------------------------------------------------
 */

/**
 * @brief HDLC station context.
 *
 * Holds all runtime state for a single HDLC station instance. Must be
 * allocated by the caller (typically as a static variable). Initialised
 * exclusively by @ref atc_hdlc_init(); do not access fields directly from
 * application code.
 *
 * Memory layout note: pointer-sized fields are grouped first to avoid
 * padding on 32-bit and 64-bit targets.
 */
typedef struct {
    /* --- Injected references (pointer-sized, highest alignment) --- */
    const atc_hdlc_config_t   *config;    /**< Protocol configuration (user-owned, must outlive ctx). */
    const atc_hdlc_platform_t *platform;  /**< Platform callbacks (user-owned, must outlive ctx). */
    atc_hdlc_tx_window_t      *tx_window; /**< TX retransmit window descriptor (user-owned). */
    atc_hdlc_rx_buffer_t      *rx_buf;    /**< RX buffer descriptor (user-owned). */

    /* --- TEST pattern reference (valid only while test_pending is true) --- */
    const atc_hdlc_u8 *test_pattern; /**< Pointer to the outgoing test payload (user-owned). */

    /* --- Inner structs --- */
    atc_hdlc_frame_t       rx_frame;   /**< Temporary parsed-frame descriptor (RX path). */
    atc_hdlc_stats_t       stats;      /**< Runtime statistics counters. */
    atc_hdlc_test_result_t test_result;/**< Result of the most recent TEST round-trip. */

    /* --- 32-bit fields --- */
    atc_hdlc_u32 rx_index;         /**< Current write index into rx_buf->buffer. */

    /* --- State machine --- */
    volatile atc_hdlc_state_t current_state; /**< Current station state. */

    /* --- 16-bit fields --- */
    atc_hdlc_u16 tx_crc;            /**< Running FCS accumulator for the streaming TX path. */
    atc_hdlc_u16 test_pattern_len;  /**< Length of the outgoing test payload in octets. */

    /* --- 8-bit sequence variables --- */
    atc_hdlc_u8 my_address;   /**< Local station address (set at init time). */
    atc_hdlc_u8 peer_address; /**< Remote station address (set at connect time). */
    atc_hdlc_u8 vs;           /**< Send state variable V(S). */
    atc_hdlc_u8 vr;           /**< Receive state variable V(R). */
    atc_hdlc_u8 va;           /**< Acknowledge state variable V(A). */
    atc_hdlc_u8 window_size;  /**< TX window size cached from config (1–7). */
    atc_hdlc_u8 next_tx_slot; /**< Index of the next slot to allocate in tx_window. */
    atc_hdlc_u8 retry_count;  /**< Current retransmission attempt count. */
    atc_hdlc_u8 rx_state;     /**< RX byte-parser state (hdlc_rx_state_t). */

    /* --- Boolean sub-condition flags (all within CONNECTED state) --- */
    atc_hdlc_bool rej_exception; /**< REJ exception: suppresses duplicate REJ transmissions. */
    atc_hdlc_bool remote_busy;   /**< Peer is in RNR state; outgoing I-frames are suspended. */
    atc_hdlc_bool local_busy;    /**< Local RNR has been sent; peer TX is throttled. */
    atc_hdlc_bool test_pending;  /**< A TEST(P=1) has been sent; awaiting TEST(F=1) response. */

    /* --- Timer state flags (set/cleared by start/stop helpers) --- */
    atc_hdlc_bool t1_active; /**< T1 retransmission timer is currently running. */
    atc_hdlc_bool t2_active; /**< T2 delayed-ACK timer is currently running. */
    atc_hdlc_bool t3_active; /**< T3 idle/keep-alive timer is currently running. */
} atc_hdlc_context_t;

#ifdef __cplusplus
}
#endif

#endif // ATC_HDLC_TYPES_H
