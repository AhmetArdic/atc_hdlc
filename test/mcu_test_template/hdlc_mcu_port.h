#ifndef HDLC_MCU_PORT_H
#define HDLC_MCU_PORT_H

#include "hdlc.h"

/*
 * Buffer / window sizing.
 * Override via compiler -D flags to match your application.
 */
#ifndef HDLC_PORT_MAX_INFO
#define HDLC_PORT_MAX_INFO 512u /* max I-frame payload (bytes) */
#endif
#ifndef HDLC_PORT_WINDOW
#define HDLC_PORT_WINDOW 7u /* TX window depth (1–7) */
#endif
#ifndef HDLC_PORT_RX_CHUNK
#define HDLC_PORT_RX_CHUNK 256u /* bytes read per port_rx_read() call */
#endif

typedef struct {
    atc_hdlc_u8 local_addr;  /* this station's HDLC address */
    atc_hdlc_u8 peer_addr;   /* peer address (SABM destination) */
    atc_hdlc_u8 max_retries; /* N2: max retransmissions before link failure */
    atc_hdlc_u32 t1_ms;      /* T1 retransmission timeout (ms) */
    atc_hdlc_u32 t2_ms;      /* T2 delayed-ACK timeout (ms, must be < t1_ms) */
} hdlc_port_config_t;

/*
 * Initialize HDLC and send SABM to peer.
 * Call once before entering your main loop.
 */
void hdlc_port_init(const hdlc_port_config_t* cfg);

/*
 * Run one main-loop iteration: drain RX bytes and tick T1/T2 timers.
 * Call this as often as possible from your main loop or a periodic task.
 */
void hdlc_port_run(void);

/*
 * Queue a reliable I-frame for transmission.
 * Returns ATC_HDLC_OK on success or an error code (e.g. ATC_HDLC_ERR_WINDOW_FULL).
 */
atc_hdlc_error_t hdlc_port_transmit(const atc_hdlc_u8* data, atc_hdlc_u32 len);

/*
 * Returns a pointer to the internal HDLC context (for diagnostics / state checks).
 */
atc_hdlc_context_t* hdlc_port_ctx(void);

#endif /* HDLC_MCU_PORT_H */
