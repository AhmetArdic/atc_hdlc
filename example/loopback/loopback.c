/*
 * loopback.c — Two HDLC stations wired in memory.
 *
 * Demonstrates the full ABM lifecycle:
 *   connect → send I-frames → disconnect
 *
 * No serial port, no OS, no threads.  Both stations live in the same
 * process.  The wire_t type queues one HDLC frame per direction;
 * wire_deliver() hands it to the peer.  Explicit delivery calls make
 * the handshake steps visible and match the asynchronous reality of a
 * serial link.
 *
 * Build (from repo root):
 *   mkdir -p build && cd build && cmake .. && make loopback
 *   ./example/loopback/loopback
 */

#include "hdlc.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Wire: queues one frame, delivers it on wire_deliver().            */
/* ------------------------------------------------------------------ */

typedef struct {
    atc_hdlc_ctx_t* peer;
    atc_hdlc_u8 txbuf[256]; /* frame being assembled   */
    int txlen;
    atc_hdlc_u8 queued[256]; /* frame ready to deliver  */
    int qlen;
    bool ready;
} wire_t;

static atc_hdlc_ctx_t ctx_a, ctx_b;
static wire_t wire_ab = {.peer = &ctx_b}; /* A → B */
static wire_t wire_ba = {.peer = &ctx_a}; /* B → A */

static int send_cb(atc_hdlc_u8 byte, bool flush, void* user_ctx) {
    wire_t* w = (wire_t*)user_ctx;
    if (w->txlen < (int)sizeof(w->txbuf))
        w->txbuf[w->txlen++] = byte;
    if (flush) {
        memcpy(w->queued, w->txbuf, (size_t)w->txlen);
        w->qlen = w->txlen;
        w->ready = true;
        w->txlen = 0;
    }
    return 0;
}

/* Deliver the queued frame to the peer.  May cause the peer to send
 * a response frame into the opposite wire. */
static void wire_deliver(wire_t* w) {
    if (!w->ready)
        return;
    w->ready = false;
    atc_hdlc_data_in(w->peer, w->queued, (atc_hdlc_u32)w->qlen);
}

/* ------------------------------------------------------------------ */
/* Callbacks                                                          */
/* ------------------------------------------------------------------ */

static const char* event_str(atc_hdlc_event_t e) {
    switch (e) {
    case ATC_HDLC_EVENT_LINK_SETUP_REQUEST:
        return "LINK_SETUP_REQUEST";
    case ATC_HDLC_EVENT_CONNECT_ACCEPTED:
        return "CONNECT_ACCEPTED";
    case ATC_HDLC_EVENT_INCOMING_CONNECT:
        return "INCOMING_CONNECT";
    case ATC_HDLC_EVENT_RESET:
        return "RESET";
    case ATC_HDLC_EVENT_DISCONNECT_REQUEST:
        return "DISCONNECT_REQUEST";
    case ATC_HDLC_EVENT_DISCONNECT_COMPLETE:
        return "DISCONNECT_COMPLETE";
    case ATC_HDLC_EVENT_PEER_DISCONNECT:
        return "PEER_DISCONNECT";
    case ATC_HDLC_EVENT_PEER_REJECT:
        return "PEER_REJECT";
    case ATC_HDLC_EVENT_PROTOCOL_ERROR:
        return "PROTOCOL_ERROR";
    case ATC_HDLC_EVENT_LINK_FAILURE:
        return "LINK_FAILURE";
    case ATC_HDLC_EVENT_REMOTE_BUSY_ON:
        return "REMOTE_BUSY_ON";
    case ATC_HDLC_EVENT_REMOTE_BUSY_OFF:
        return "REMOTE_BUSY_OFF";
    case ATC_HDLC_EVENT_WINDOW_OPEN:
        return "WINDOW_OPEN";
    default:
        return "?";
    }
}

static void on_event_a(atc_hdlc_event_t e, void* u) {
    (void)u;
    printf("[A] %s\n", event_str(e));
}
static void on_event_b(atc_hdlc_event_t e, void* u) {
    (void)u;
    printf("[B] %s\n", event_str(e));
}

static void on_data_b(const atc_hdlc_u8* data, atc_hdlc_u16 len, void* u) {
    (void)u;
    printf("[B] received %u bytes: %.*s\n", len, (int)len, (const char*)data);
}

/* Timer stubs — no real timers needed; T2 is triggered manually. */
static void t_start(atc_hdlc_u32 ms, void* u) {
    (void)ms;
    (void)u;
}
static void t_stop(void* u) {
    (void)u;
}

/* ------------------------------------------------------------------ */
/* Static storage                                                     */
/* ------------------------------------------------------------------ */

static atc_hdlc_u8 rx_a[128], rx_b[128];
static atc_hdlc_u8 tx_slots_a[1 * 64];
static atc_hdlc_u32 tx_lens_a[1];

static atc_hdlc_rx_buffer_t rx_buf_a = {rx_a, sizeof(rx_a)};
static atc_hdlc_rx_buffer_t rx_buf_b = {rx_b, sizeof(rx_b)};
static atc_hdlc_tx_window_t tx_win_a = {tx_slots_a, tx_lens_a, 64, 1};

static const atc_hdlc_config_t cfg_a = {
    .mode = ATC_HDLC_MODE_ABM,
    .address = 0x01,
    .max_info_size = 64,
    .max_retries = 3,
    .t1_ms = 1000,
    .t2_ms = 10,
};
static const atc_hdlc_config_t cfg_b = {
    .mode = ATC_HDLC_MODE_ABM,
    .address = 0x02,
    .max_info_size = 64,
    .max_retries = 3,
    .t1_ms = 1000,
    .t2_ms = 10,
};

static const atc_hdlc_platform_ops_t plat_a = {
    .on_send = send_cb,
    .on_event = on_event_a,
    .t1_start = t_start,
    .t1_stop = t_stop,
    .t2_start = t_start,
    .t2_stop = t_stop,
    .user_ctx = &wire_ab,
};
static const atc_hdlc_platform_ops_t plat_b = {
    .on_send = send_cb,
    .on_data = on_data_b,
    .on_event = on_event_b,
    .t1_start = t_start,
    .t1_stop = t_stop,
    .t2_start = t_start,
    .t2_stop = t_stop,
    .user_ctx = &wire_ba,
};

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    atc_hdlc_init(&ctx_a, (atc_hdlc_params_t){
                              .config = &cfg_a,
                              .platform = &plat_a,
                              .tx_window = &tx_win_a,
                              .rx_buf = &rx_buf_a,
                          });
    atc_hdlc_init(&ctx_b, (atc_hdlc_params_t){
                              .config = &cfg_b,
                              .platform = &plat_b,
                              .tx_window = NULL, /* B only receives I-frames in this example */
                              .rx_buf = &rx_buf_b,
                          });

    /*
     * B must know A's address to send RR frames back after receiving
     * I-frames.  In a real system both addresses are provisioned at
     * boot; here we set it directly.
     */
    ctx_b.peer_address = 0x01;

    /* --- Connect ------------------------------------------------------ */
    puts("--- connect ---");

    atc_hdlc_link_setup(&ctx_a, 0x02); /* A queues SABM in wire_ab  */
    wire_deliver(&wire_ab);            /* SABM → B; B queues UA in wire_ba */
    wire_deliver(&wire_ba);            /* UA  → A; A is now CONNECTED */

    /* --- Send I-frames ------------------------------------------------ */
    puts("\n--- data ---");

    const atc_hdlc_u8 msg1[] = "Hello!";
    atc_hdlc_transmit_i(&ctx_a, msg1, sizeof(msg1) - 1); /* queues I-frame */
    wire_deliver(&wire_ab);                              /* I-frame → B; B receives, starts T2 */
    atc_hdlc_t2_expired(&ctx_b);                         /* simulate T2: B queues RR */
    wire_deliver(&wire_ba);                              /* RR → A; A's TX window opens */

    const atc_hdlc_u8 msg2[] = "World!";
    atc_hdlc_transmit_i(&ctx_a, msg2, sizeof(msg2) - 1);
    wire_deliver(&wire_ab);
    atc_hdlc_t2_expired(&ctx_b);
    wire_deliver(&wire_ba);

    /* --- Disconnect --------------------------------------------------- */
    puts("\n--- disconnect ---");

    atc_hdlc_disconnect(&ctx_a); /* A queues DISC */
    wire_deliver(&wire_ab);      /* DISC → B; B fires PEER_DISCONNECT, queues UA */
    wire_deliver(&wire_ba);      /* UA  → A; A fires DISCONNECT_COMPLETE */

    return 0;
}
