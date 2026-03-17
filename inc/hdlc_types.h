/*
 * Copyright (C) 2026 Ahmet Talha ARDIC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef ATC_HDLC_TYPES_H
#define ATC_HDLC_TYPES_H

#include "hdlc_config.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ATC_HDLC_BROADCAST_ADDRESS 0xFF

typedef uint_least8_t atc_hdlc_u8;
typedef uint_least16_t atc_hdlc_u16;
typedef uint_least32_t atc_hdlc_u32;
typedef bool atc_hdlc_bool;

typedef enum {
    ATC_HDLC_FRAME_I,      /**< Information Frame (Data transfer) */
    ATC_HDLC_FRAME_S,      /**< Supervisory Frame (Flow/Error control) */
    ATC_HDLC_FRAME_U,      /**< Unnumbered Frame (Link management) */
    ATC_HDLC_FRAME_INVALID /**< Invalid or Unknown Frame format */
} atc_hdlc_frame_type_t;

typedef enum {
    ATC_HDLC_S_FRAME_TYPE_RR,      /**< Receive Ready */
    ATC_HDLC_S_FRAME_TYPE_RNR,     /**< Receive Not Ready */
    ATC_HDLC_S_FRAME_TYPE_REJ,     /**< Reject */
    ATC_HDLC_S_FRAME_TYPE_UNKNOWN  /**< Unknown or Invalid S-Frame */
} atc_hdlc_s_frame_sub_type_t;

typedef enum {
    ATC_HDLC_U_FRAME_TYPE_SABM,    /**< Set Asynchronous Balanced Mode */
    ATC_HDLC_U_FRAME_TYPE_SNRM,    /**< Set Normal Response Mode */
    ATC_HDLC_U_FRAME_TYPE_SARM,    /**< Set Asynchronous Response Mode */
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

typedef struct {
    atc_hdlc_u8 *information;      /**< Pointer to Information Field (Payload). */
    atc_hdlc_frame_type_t type;    /**< Resolved Frame Type (I/S/U). */
    atc_hdlc_u16 information_len;  /**< Length of valid data in information. */
    atc_hdlc_u8 address;           /**< Address Field. */
    atc_hdlc_u8 control;           /**< Control Field. */
} atc_hdlc_frame_t;

typedef enum {
    ATC_HDLC_STATE_DISCONNECTED,   /**< No logical connection; only SABM/UI/TEST are processed. */
    ATC_HDLC_STATE_CONNECTING,     /**< SABM sent, awaiting UA; T1 running. */
    ATC_HDLC_STATE_CONNECTED,      /**< Active session; all I/S/U frames processed normally. */
    ATC_HDLC_STATE_FRMR_ERROR,     /**< Irrecoverable protocol error; only reset/disconnect valid. */
    ATC_HDLC_STATE_DISCONNECTING,  /**< DISC sent, awaiting UA; T1 running. */
} atc_hdlc_state_t;

typedef enum {
    ATC_HDLC_EVENT_LINK_SETUP_REQUEST, /**< Local: atc_hdlc_link_setup() called; SABM sent. */
    ATC_HDLC_EVENT_CONNECT_ACCEPTED,   /**< UA received in response to our SABM. */
    ATC_HDLC_EVENT_INCOMING_CONNECT,   /**< Peer sent SABM — passive open accepted. */
    ATC_HDLC_EVENT_RESET,              /**< Link reset initiated (new SABM sent after internal reset). */
    ATC_HDLC_EVENT_DISCONNECT_REQUEST, /**< Local: atc_hdlc_disconnect() called; DISC sent. */
    ATC_HDLC_EVENT_DISCONNECT_COMPLETE,/**< UA received in response to our DISC. */
    ATC_HDLC_EVENT_PEER_DISCONNECT,    /**< Peer sent DISC — connection closed by remote. */
    ATC_HDLC_EVENT_PEER_REJECT,        /**< Peer sent DM — connection rejected. */
    ATC_HDLC_EVENT_PROTOCOL_ERROR,     /**< Peer sent FRMR — irrecoverable protocol violation. */
    ATC_HDLC_EVENT_LINK_FAILURE,       /**< N2 retry limit exceeded — link declared failed. */
    ATC_HDLC_EVENT_REMOTE_BUSY_ON,     /**< Peer sent RNR; outgoing I-frame transmission suspended. */
    ATC_HDLC_EVENT_REMOTE_BUSY_OFF,    /**< Peer sent RR after RNR; transmission resumed. */
    ATC_HDLC_EVENT_WINDOW_OPEN,        /**< TX window slot freed; application may send again. */
    ATC_HDLC_EVENT_TEST_RESULT,        /**< TEST frame round-trip complete; check test_result field. */
} atc_hdlc_event_t;

typedef enum {
    ATC_HDLC_OK                       =  0,     /**< Operation completed successfully. */
    ATC_HDLC_ERR_FCS                  = -1,     /**< FCS mismatch — frame discarded. */
    ATC_HDLC_ERR_SHORT_FRAME          = -2,     /**< Frame too short to be valid. */
    ATC_HDLC_ERR_BUFFER_FULL          = -3,     /**< Destination buffer is full. */
    ATC_HDLC_ERR_NO_BUFFER            = -4,     /**< Required buffer not provided. */
    ATC_HDLC_ERR_SEQUENCE             = -5,     /**< Sequence number out of range. */
    ATC_HDLC_ERR_INVALID_COMMAND      = -6,     /**< Received unsupported/invalid command. */
    ATC_HDLC_ERR_FRMR                 = -7,     /**< Irrecoverable protocol error (FRMR). */
    ATC_HDLC_ERR_INVALID_STATE        = -8,     /**< Operation not permitted in current state. */
    ATC_HDLC_ERR_UNSUPPORTED_MODE     = -9,     /**< Requested mode not implemented. */
    ATC_HDLC_ERR_MAX_RETRY            = -10,    /**< N2 retry limit exceeded — link failed. */
    ATC_HDLC_ERR_INVALID_PARAM        = -11,    /**< NULL or out-of-range parameter. */
    ATC_HDLC_ERR_INCONSISTENT_BUFFER  = -12,    /**< Buffer geometry violates constraints. */
    ATC_HDLC_ERR_REMOTE_BUSY          = -13,    /**< Peer is busy (RNR received); TX suspended. */
    ATC_HDLC_ERR_WINDOW_FULL          = -14,    /**< TX window full; no free slots. */
    ATC_HDLC_ERR_FRAME_TOO_LARGE      = -15,    /**< Payload exceeds max_frame_size (MRU). */
    ATC_HDLC_ERR_TEST_PENDING         = -16,    /**< A TEST frame is already awaiting response. */
} atc_hdlc_error_t;

typedef enum {
    ATC_HDLC_MODE_ABM = 0, /**< Asynchronous Balanced Mode (SABM). Combined stations. */
    ATC_HDLC_MODE_NRM = 1, /**< Normal Response Mode (SNRM). Primary polls Secondary. */
    ATC_HDLC_MODE_ARM = 2  /**< Asynchronous Response Mode (SARM). */
} atc_hdlc_link_mode_t;

typedef enum {
    ATC_HDLC_ROLE_COMBINED = 0, /**< Equal rights (used in ABM). */
    ATC_HDLC_ROLE_PRIMARY  = 1, /**< Master node (sends Commands, receives Responses). */
    ATC_HDLC_ROLE_SECONDARY= 2  /**< Slave node (receives Commands, sends Responses). */
} atc_hdlc_station_role_t;

typedef struct {
    atc_hdlc_link_mode_t mode;              /**< Operating mode. */
    atc_hdlc_u8          address;           /**< Local station address. */
    atc_hdlc_u8          window_size;       /**< Sliding window size, 1–7 (mod-8). */
    atc_hdlc_u32         max_frame_size;    /**< Maximum information field size in octets (MRU). */
    atc_hdlc_u8          max_retries;       /**< N2: maximum retransmission attempts before link failure. */
    atc_hdlc_u32         t1_ms;             /**< T1 retransmission timer in ms (typical 200–3000). */
    atc_hdlc_u32         t2_ms;             /**< T2 acknowledgement delay timer in ms (must be < t1_ms). */
    atc_hdlc_u32         t3_ms;             /**< T3 idle/keep-alive timer in ms (typical 10000–60000). */
    atc_hdlc_bool        use_extended;      /**< Extended (mod-128) mode flag. Must be false in v1. */
} atc_hdlc_config_t;

/** @brief Byte output callback.
 *  @param byte Byte to send.
 *  @param flush true = last byte of frame.
 *  @param user_ctx User context.
 *  @return 0 on success.
 */
typedef int (*atc_hdlc_send_fn)(atc_hdlc_u8 byte, atc_hdlc_bool flush, void *user_ctx);

/** @brief Timer start callback.
 *  @param ms Duration in ms.
 *  @param user_ctx User context.
 */
typedef void (*atc_hdlc_timer_start_fn)(atc_hdlc_u32 ms, void *user_ctx);

/** @brief Timer stop callback.
 *  @param user_ctx User context.
 */
typedef void (*atc_hdlc_timer_stop_fn)(void *user_ctx);

/** @brief Data received callback.
 *  @param payload Data pointer.
 *  @param len Length.
 *  @param user_ctx User context.
 */
typedef void (*atc_hdlc_on_data_fn)(const atc_hdlc_u8 *payload, atc_hdlc_u16 len, void *user_ctx);

/** @brief Event callback.
 *  @param event Event type.
 *  @param user_ctx User context.
 */
typedef void (*atc_hdlc_on_event_fn)(atc_hdlc_event_t event, void *user_ctx);

typedef struct {
    atc_hdlc_send_fn on_send;          /**< TX callback (required) */
    atc_hdlc_on_data_fn on_data;       /**< RX callback (optional) */
    atc_hdlc_on_event_fn on_event;     /**< Event callback (optional) */
    void *user_ctx;                    /**< User context */
    atc_hdlc_timer_start_fn t1_start;  /**< T1 (retransmission) start */
    atc_hdlc_timer_stop_fn t1_stop;    /**< T1 stop */
    atc_hdlc_timer_start_fn t2_start;  /**< T2 (delayed-ACK) start */
    atc_hdlc_timer_stop_fn t2_stop;    /**< T2 stop */
    atc_hdlc_timer_start_fn t3_start;  /**< T3 (idle/keep-alive) start */
    atc_hdlc_timer_stop_fn t3_stop;    /**< T3 stop */
} atc_hdlc_platform_t;

typedef struct {
    atc_hdlc_u8  *slots;            /**< Flat buffer for frame payloads: slot_count * slot_capacity octets. */
    atc_hdlc_u32 *slot_lens;        /**< Per-slot stored payload length (slot_count elements). */
    atc_hdlc_u8  *seq_to_slot;      /**< Sequence-number-to-slot-index mapping (slot_count elements). */
    atc_hdlc_u32  slot_capacity;    /**< Capacity of a single slot in octets. */
    atc_hdlc_u8   slot_count;       /**< Total number of slots (must equal window_size). */
} atc_hdlc_tx_window_t;

typedef struct {
    atc_hdlc_u8  *buffer;   /**< Pointer to the receive buffer. */
    atc_hdlc_u32  capacity; /**< Buffer capacity in octets. */
} atc_hdlc_rx_buffer_t;

typedef struct {
    atc_hdlc_u32 tx_i_frames;            /**< I-frames successfully transmitted. */
    atc_hdlc_u32 tx_bytes;               /**< Information bytes transmitted. */
    atc_hdlc_u32 rx_i_frames;            /**< In-sequence I-frames accepted. */
    atc_hdlc_u32 rx_bytes;               /**< Information bytes received. */
    atc_hdlc_u32 fcs_errors;             /**< Frames discarded due to FCS mismatch. */
    atc_hdlc_u32 frmr_count;             /**< FRMR frames received from peer. */
    atc_hdlc_u32 timeout_count;          /**< T1 timeout occurrences (retransmission triggers). */
    atc_hdlc_u32 rej_sent;               /**< REJ frames sent. */
    atc_hdlc_u32 rej_received;           /**< REJ frames received. */
    atc_hdlc_u32 rnr_sent;               /**< RNR frames sent (local busy asserted). */
    atc_hdlc_u32 rnr_received;           /**< RNR frames received (remote busy detected). */
    atc_hdlc_u32 local_busy_transitions; /**< Number of times local busy was asserted. */
    atc_hdlc_u32 test_sent;              /**< TEST frames sent. */
    atc_hdlc_u32 test_success;           /**< TEST frames with a matching response received. */
    atc_hdlc_u32 test_failed;            /**< TEST frames that timed out or had payload mismatch. */
} atc_hdlc_stats_t;

typedef struct {
    atc_hdlc_bool success;      /**< True if the peer echoed the correct payload. */
    atc_hdlc_bool timed_out;    /**< True if T1 expired before a response arrived. */
    atc_hdlc_u16  payload_len;  /**< Length of the test pattern that was sent. */
} atc_hdlc_test_result_t;

/** @brief Main context (opaque). */
typedef struct {
    const atc_hdlc_config_t   *config;    /**< Protocol configuration (user-owned, must outlive ctx). */
    const atc_hdlc_platform_t *platform;  /**< Platform callbacks (user-owned, must outlive ctx). */
    atc_hdlc_tx_window_t      *tx_window; /**< TX retransmit window descriptor (user-owned). */
    atc_hdlc_rx_buffer_t      *rx_buf;    /**< RX buffer descriptor (user-owned). */
    const atc_hdlc_u8 *test_pattern; /**< Pointer to the outgoing test payload (user-owned). */
    atc_hdlc_frame_t       rx_frame;   /**< Temporary parsed-frame descriptor (RX path). */
    atc_hdlc_stats_t       stats;      /**< Runtime statistics counters. */
    atc_hdlc_test_result_t test_result;/**< Result of the most recent TEST round-trip. */
    atc_hdlc_u32 rx_index;         /**< Current write index into rx_buf->buffer. */
    volatile atc_hdlc_state_t current_state; /**< Current station state. */
    atc_hdlc_u16 tx_crc;            /**< Running FCS accumulator for the streaming TX path. */
    atc_hdlc_u16 test_pattern_len;  /**< Length of the outgoing test payload in octets. */
    atc_hdlc_u8 my_address;   /**< Local station address (set at init time). */
    atc_hdlc_u8 peer_address; /**< Remote station address (set at connect time). */
    atc_hdlc_u8 vs;           /**< Send state variable V(S). */
    atc_hdlc_u8 vr;           /**< Receive state variable V(R). */
    atc_hdlc_u8 va;           /**< Acknowledge state variable V(A). */
    atc_hdlc_u8 window_size;  /**< TX window size cached from config (1–7). */
    atc_hdlc_u8 next_tx_slot; /**< Index of the next slot to allocate in tx_window. */
    atc_hdlc_u8 retry_count;  /**< Current retransmission attempt count. */
    atc_hdlc_u8 rx_state;     /**< RX byte-parser state (hdlc_rx_state_t). */
    atc_hdlc_bool rej_exception; /**< REJ exception: suppresses duplicate REJ transmissions. */
    atc_hdlc_bool remote_busy;   /**< Peer is in RNR state; outgoing I-frames are suspended. */
    atc_hdlc_bool local_busy;    /**< Local RNR has been sent; peer TX is throttled. */
    atc_hdlc_bool test_pending;  /**< A TEST(P=1) has been sent; awaiting TEST(F=1) response. */
    atc_hdlc_bool t1_active; /**< T1 retransmission timer is currently running. */
    atc_hdlc_bool t2_active; /**< T2 delayed-ACK timer is currently running. */
    atc_hdlc_bool t3_active; /**< T3 idle/keep-alive timer is currently running. */
} atc_hdlc_context_t;

#ifdef __cplusplus
}
#endif

#endif /* ATC_HDLC_TYPES_H */
