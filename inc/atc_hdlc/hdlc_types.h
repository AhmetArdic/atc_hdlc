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

#define ATC_HDLC_BROADCAST_ADDRESS (0xFF)

typedef uint_least8_t atc_hdlc_u8;
typedef uint_least16_t atc_hdlc_u16;
typedef uint_least32_t atc_hdlc_u32;

typedef enum {
    ATC_HDLC_STATE_DISCONNECTED,  /**< No logical connection; only SABM/UI/TEST are processed. */
    ATC_HDLC_STATE_CONNECTING,    /**< SABM sent, awaiting UA; T1 running. */
    ATC_HDLC_STATE_CONNECTED,     /**< Active session; all I/S/U frames processed normally. */
    ATC_HDLC_STATE_FRMR_ERROR,    /**< Irrecoverable protocol error; only reset/disconnect valid. */
    ATC_HDLC_STATE_DISCONNECTING, /**< DISC sent, awaiting UA; T1 running. */
} atc_hdlc_state_t;

typedef enum {
    ATC_HDLC_EVENT_SETUP_REQ,      /**< Local: atc_hdlc_link_setup() called; SABM sent. */
    ATC_HDLC_EVENT_CONN_ACCEPTED,  /**< UA received in response to our SABM. */
    ATC_HDLC_EVENT_CONN_REQ,       /**< Peer sent SABM — passive open accepted. */
    ATC_HDLC_EVENT_RESET,          /**< Link reset initiated (new SABM sent after internal reset). */
    ATC_HDLC_EVENT_DISC_REQ,       /**< Local: atc_hdlc_disconnect() called; DISC sent. */
    ATC_HDLC_EVENT_DISC_DONE,      /**< UA received in response to our DISC. */
    ATC_HDLC_EVENT_PEER_DISC,      /**< Peer sent DISC — connection closed by remote. */
    ATC_HDLC_EVENT_PEER_REJECT,    /**< Peer sent DM — connection rejected. */
    ATC_HDLC_EVENT_PROTOCOL_ERROR, /**< Peer sent FRMR — irrecoverable protocol violation. */
    ATC_HDLC_EVENT_LINK_FAILURE,   /**< N2 retry limit exceeded — link declared failed. */
    ATC_HDLC_EVENT_PEER_BUSY,      /**< Peer sent RNR; outgoing I-frame transmission suspended. */
    ATC_HDLC_EVENT_PEER_READY,     /**< Peer sent RR after RNR; transmission resumed. */
    ATC_HDLC_EVENT_WINDOW_OPEN,    /**< TX window slot freed; application may send again. */
} atc_hdlc_event_t;

typedef enum {
    ATC_HDLC_OK = 0,                /**< Operation completed successfully. */
    ATC_HDLC_ERR_FCS = -1,          /**< FCS mismatch — frame discarded. */
    ATC_HDLC_ERR_SHORT_FRAME = -2,  /**< Frame too short to be valid. */
    ATC_HDLC_ERR_BUFFER_FULL = -3,  /**< Destination buffer is full. */
    ATC_HDLC_ERR_NO_BUFFER = -4,    /**< Required buffer not provided. */
    ATC_HDLC_ERR_SEQUENCE = -5,     /**< Sequence number out of range. */
    ATC_HDLC_ERR_BAD_CMD = -6,      /**< Received unsupported/invalid command. */
    ATC_HDLC_ERR_FRMR = -7,         /**< Irrecoverable protocol error (FRMR). */
    ATC_HDLC_ERR_BAD_STATE = -8,    /**< Operation not permitted in current state. */
    ATC_HDLC_ERR_BAD_MODE = -9,     /**< Requested mode not implemented. */
    ATC_HDLC_ERR_MAX_RETRY = -10,   /**< N2 retry limit exceeded — link failed. */
    ATC_HDLC_ERR_BAD_PARAM = -11,   /**< NULL or out-of-range parameter. */
    ATC_HDLC_ERR_BAD_BUF = -12,     /**< Buffer geometry violates constraints. */
    ATC_HDLC_ERR_REMOTE_BUSY = -13, /**< Peer is busy (RNR received); TX suspended. */
    ATC_HDLC_ERR_WINDOW_FULL = -14, /**< TX window full; no free slots. */
    ATC_HDLC_ERR_FRAME_SIZE = -15,  /**< Payload exceeds max_info_size. */
} atc_hdlc_error_t;

typedef enum {
    ATC_HDLC_MODE_ABM = 0, /**< Asynchronous Balanced Mode (SABM). */
} atc_hdlc_mode_t;

typedef struct {
    atc_hdlc_mode_t mode;       /**< Operating mode. */
    atc_hdlc_u8 address;        /**< Local station address. */
    atc_hdlc_u32 max_info_size; /**< Maximum information field size in octets. */
    atc_hdlc_u8 max_retries;    /**< N2: maximum retransmission attempts before link failure. */
    atc_hdlc_u32 t1_ms;         /**< T1 retransmission timer in ms (typical 200–3000). */
    atc_hdlc_u32 t2_ms;         /**< T2 acknowledgement delay timer in ms (must be < t1_ms). */
} atc_hdlc_config_t;

/** @brief Byte output callback.
 *  @param byte Byte to send.
 *  @param flush true = last byte of frame.
 *  @param user_ctx User context.
 *  @return 0 on success.
 */
typedef int (*atc_hdlc_send_fn)(atc_hdlc_u8 byte, bool flush, void* user_ctx);

/** @brief Timer start callback.
 *  @param ms Duration in ms.
 *  @param user_ctx User context.
 */
typedef void (*atc_hdlc_tmr_start_fn)(atc_hdlc_u32 ms, void* user_ctx);

/** @brief Timer stop callback.
 *  @param user_ctx User context.
 */
typedef void (*atc_hdlc_tmr_stop_fn)(void* user_ctx);

/** @brief Data received callback.
 *  @param payload Data pointer.
 *  @param len Length.
 *  @param user_ctx User context.
 */
typedef void (*atc_hdlc_on_data_fn)(const atc_hdlc_u8* payload, atc_hdlc_u16 len, void* user_ctx);

/** @brief Event callback.
 *  @param event Event type.
 *  @param user_ctx User context.
 */
typedef void (*atc_hdlc_on_event_fn)(atc_hdlc_event_t event, void* user_ctx);

typedef struct {
    atc_hdlc_send_fn on_send;       /**< TX callback (required) */
    atc_hdlc_on_data_fn on_data;    /**< RX callback (optional) */
    atc_hdlc_on_event_fn on_event;  /**< Event callback (optional) */
    atc_hdlc_tmr_start_fn t1_start; /**< T1 (retransmission) start */
    atc_hdlc_tmr_stop_fn t1_stop;   /**< T1 stop */
    atc_hdlc_tmr_start_fn t2_start; /**< T2 (delayed-ACK) start */
    atc_hdlc_tmr_stop_fn t2_stop;   /**< T2 stop */
    void* user_ctx;                 /**< User context */
} atc_hdlc_plat_ops_t;

typedef struct {
    atc_hdlc_u8* slots;         /**< Flat buffer for frame payloads: slot_count * slot_capacity octets. */
    atc_hdlc_u32* slot_lens;    /**< Per-slot stored payload length (slot_count elements). */
    atc_hdlc_u32 slot_capacity; /**< Capacity of a single slot in octets. */
    atc_hdlc_u8 slot_count;     /**< TX window depth: number of retransmit slots (1–7). */
} atc_hdlc_txwin_t;

typedef struct {
    atc_hdlc_u8* buffer;   /**< Pointer to the receive buffer. */
    atc_hdlc_u32 capacity; /**< Buffer capacity in octets. */
} atc_hdlc_rxbuf_t;

typedef struct {
    atc_hdlc_u16 (*compute)(atc_hdlc_u16 crc, const atc_hdlc_u8* buf, atc_hdlc_u32 len);
} atc_hdlc_crc_ops_t;

extern const atc_hdlc_crc_ops_t atc_hdlc_crc_ops_default;

typedef struct {
    const atc_hdlc_config_t* config;     /**< Protocol settings (must stay valid). */
    const atc_hdlc_plat_ops_t* platform; /**< Callbacks (on_send required). */
    const atc_hdlc_txwin_t* tx_window;   /**< TX buffer for reliable TX (NULL = disable). */
    const atc_hdlc_rxbuf_t* rx_buf;      /**< RX buffer (required). */
    const atc_hdlc_crc_ops_t* crc;       /**< CRC driver (NULL = software default). */
} atc_hdlc_params_t;

typedef struct {
    const atc_hdlc_config_t* config;
    const atc_hdlc_plat_ops_t* platform;
    const atc_hdlc_crc_ops_t* crc;
    const atc_hdlc_txwin_t* tx_window;
    const atc_hdlc_rxbuf_t* rx_buf;

    atc_hdlc_u8 current_state;
    atc_hdlc_u32 rx_index;
    atc_hdlc_u16 rx_crc, tx_crc;
    atc_hdlc_u8 rx_state;
    atc_hdlc_u8 retransmit_from;
    atc_hdlc_u8 tx_next_slot;

    atc_hdlc_u8 my_address, peer_address;
    atc_hdlc_u8 vs, vr, va;
    atc_hdlc_u8 n2;
    atc_hdlc_u8 frmr_ctrl, frmr_flags;

    atc_hdlc_u8 flags;
} atc_hdlc_ctx_t;

#define HDLC_F_T1_ACTIVE          ((atc_hdlc_u8)0x01u)
#define HDLC_F_T2_ACTIVE          ((atc_hdlc_u8)0x02u)
#define HDLC_F_REJ_EXCEPTION      ((atc_hdlc_u8)0x04u)
#define HDLC_F_REMOTE_BUSY        ((atc_hdlc_u8)0x08u)
#define HDLC_F_LOCAL_BUSY         ((atc_hdlc_u8)0x10u)
#define HDLC_F_RETRANSMIT_PENDING ((atc_hdlc_u8)0x20u)

#define CTX_FLAG(ctx, f) ((ctx)->flags & (atc_hdlc_u8)(f))
#define CTX_SET(ctx, f)  ((ctx)->flags |= (atc_hdlc_u8)(f))
#define CTX_CLR(ctx, f)  ((ctx)->flags &= (atc_hdlc_u8) ~(unsigned)(f))

#ifdef __cplusplus
}
#endif

#endif /* ATC_HDLC_TYPES_H */
