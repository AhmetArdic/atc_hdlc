/*
 * minimal_ui.c — Connectionless UI-frame send/receive.
 *
 * Shows the smallest possible library configuration:
 *   - No TX window  (tx_window = NULL, I-frame TX disabled)
 *   - No timer callbacks  (UI frames need no T1/T2)
 *   - No connection setup
 *
 * Suitable for broadcast/beacon use cases on heavily resource-
 * constrained targets where reliable delivery is not required.
 *
 * Build:
 *   make minimal_ui   (from build/)
 *   ./example/minimal_ui/minimal_ui
 */

#include "hdlc.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Self-loopback: on_send feeds bytes back into data_in.             */
/* Simulates a shared bus where the sender also sees its own frames. */
/* ------------------------------------------------------------------ */

static atc_hdlc_context_t ctx;

static atc_hdlc_u8  loopback_buf[64];
static int          loopback_len = 0;

static int send_cb(atc_hdlc_u8 byte, bool flush, void *user_ctx)
{
    (void)user_ctx;
    if (loopback_len < (int)sizeof(loopback_buf))
        loopback_buf[loopback_len++] = byte;
    if (flush) {
        atc_hdlc_data_in(&ctx, loopback_buf, (atc_hdlc_u32)loopback_len);
        loopback_len = 0;
    }
    return 0;
}

static void on_data(const atc_hdlc_u8 *data, atc_hdlc_u16 len, void *u)
{
    (void)u;
    printf("received %u bytes: %.*s\n", len, (int)len, (const char *)data);
}

/* ------------------------------------------------------------------ */
/* Static storage — sized for smallest sensible frame                */
/* ------------------------------------------------------------------ */

static atc_hdlc_u8 rx_buf_mem[32];

static const atc_hdlc_config_t cfg = {
    .mode           = ATC_HDLC_MODE_ABM,
    .address        = 0x01,
    .window_size    = 1,   /* required by init even when TX window is NULL */
    .max_frame_size = 16,
    .max_retries    = 0,
    .t1_ms          = 0,
    .t2_ms          = 0,
};

static const atc_hdlc_platform_t platform = {
    .on_send  = send_cb,
    .on_data  = on_data,
    /* All timer callbacks NULL — UI frames never start T1 or T2. */
};

static atc_hdlc_rx_buffer_t rx_buf = { rx_buf_mem, sizeof(rx_buf_mem) };

/* ------------------------------------------------------------------ */

int main(void)
{
    atc_hdlc_init(&ctx, (atc_hdlc_params_t){
        .config    = &cfg,
        .platform  = &platform,
        .tx_window = NULL,  /* disable reliable I-frame TX */
        .rx_buf    = &rx_buf,
    });

    const atc_hdlc_u8 msg[] = "hello world";

    /* Send a UI frame to the broadcast address (0xFF).
     * Works in any state — no connection required. */
    atc_hdlc_transmit_ui(&ctx, ATC_HDLC_BROADCAST_ADDRESS, msg, sizeof(msg) - 1);

    /* Send a larger payload using the streaming API to avoid
     * assembling it in a single buffer. */
    atc_hdlc_transmit_ui_start(&ctx, ATC_HDLC_BROADCAST_ADDRESS);
    atc_hdlc_transmit_ui_data(&ctx, (const atc_hdlc_u8 *)"stream", 6);
    atc_hdlc_transmit_ui_data(&ctx, (const atc_hdlc_u8 *)"ed", 2);
    atc_hdlc_transmit_ui_end(&ctx);

    return 0;
}
