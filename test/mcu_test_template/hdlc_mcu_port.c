#include "hdlc_mcu_port.h"
#include "hdlc_platform.h"

/* ---- Static storage ---- */

static atc_hdlc_ctx_t hdlc_ctx;
static atc_hdlc_config_t hdlc_cfg;
static atc_hdlc_platform_ops_t hdlc_plat;
static atc_hdlc_tx_window_t hdlc_tw;
static atc_hdlc_rx_buffer_t hdlc_rx_desc;

static atc_hdlc_u8 rx_buf[HDLC_PORT_MAX_INFO + 4u];
static atc_hdlc_u8 tx_slots[HDLC_PORT_WINDOW * HDLC_PORT_MAX_INFO];
static atc_hdlc_u32 tx_lens[HDLC_PORT_WINDOW];

/* ---- Timer state (written by callbacks, read by hdlc_port_run) ---- */

static volatile uint_least8_t t1_active;
static volatile uint32_t t1_started_ms;
static volatile uint_least8_t t2_active;
static volatile uint32_t t2_started_ms;

/* ---- Internal HDLC callbacks ---- */

static int on_send(atc_hdlc_u8 byte, bool flush, void* user_ctx) {
    (void)user_ctx;
    port_tx_byte(byte, flush);
    return 0;
}

static void on_data(const atc_hdlc_u8* data, atc_hdlc_u16 len, void* user_ctx) {
    (void)user_ctx;
    atc_hdlc_transmit_i(&hdlc_ctx, data, len);
}

/*
 * Test B self-test: on connection send 5 I-frames with a known pattern so
 * the host-side run_test_b() can verify reception and payload integrity.
 * Constants match test_physical_target.c: FRAME_COUNT=5, FRAME_SIZE=16.
 */
#define SELF_TEST_FRAME_COUNT 5u
#define SELF_TEST_FRAME_SIZE  16u

static const atc_hdlc_u8 self_test_payload[SELF_TEST_FRAME_SIZE] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
};

static void on_event(atc_hdlc_event_t event, void* user_ctx) {
    (void)user_ctx;

    if (event == ATC_HDLC_EVENT_CONNECT_ACCEPTED || event == ATC_HDLC_EVENT_INCOMING_CONNECT) {
        for (atc_hdlc_u8 i = 0; i < SELF_TEST_FRAME_COUNT; i++)
            atc_hdlc_transmit_i(&hdlc_ctx, self_test_payload, SELF_TEST_FRAME_SIZE);
    }
}

static void t1_start(atc_hdlc_u32 ms, void* user_ctx) {
    (void)ms;
    (void)user_ctx;
    t1_started_ms = port_tick_ms();
    t1_active = 1;
}

static void t1_stop(void* user_ctx) {
    (void)user_ctx;
    t1_active = 0;
}

/*
 * T2 is fired with t2_started_ms = 0 so that the expiry check in
 * hdlc_port_run() triggers on the very next iteration regardless of
 * the configured t2_ms value.  This matches the bare-metal trick from
 * the MCU reference: sending the RR on the same DMA burst as the last
 * received byte maximises pipeline throughput.
 */
static void t2_start(atc_hdlc_u32 ms, void* user_ctx) {
    (void)ms;
    (void)user_ctx;
    t2_started_ms = 0;
    t2_active = 1;
}

static void t2_stop(void* user_ctx) {
    (void)user_ctx;
    t2_active = 0;
}

/* ---- Public API ---- */

void hdlc_port_init(const hdlc_port_config_t* cfg) {
    hdlc_cfg.mode = ATC_HDLC_MODE_ABM;
    hdlc_cfg.address = cfg->local_addr;
    hdlc_cfg.max_info_size = HDLC_PORT_MAX_INFO;
    hdlc_cfg.max_retries = cfg->max_retries;
    hdlc_cfg.t1_ms = cfg->t1_ms;
    hdlc_cfg.t2_ms = cfg->t2_ms;

    hdlc_plat.on_send = on_send;
    hdlc_plat.on_data = on_data;
    hdlc_plat.on_event = on_event;
    hdlc_plat.user_ctx = NULL;
    hdlc_plat.t1_start = t1_start;
    hdlc_plat.t1_stop = t1_stop;
    hdlc_plat.t2_start = t2_start;
    hdlc_plat.t2_stop = t2_stop;

    hdlc_tw.slots = tx_slots;
    hdlc_tw.slot_lens = tx_lens;
    hdlc_tw.slot_count = HDLC_PORT_WINDOW;
    hdlc_tw.slot_capacity = HDLC_PORT_MAX_INFO;

    hdlc_rx_desc.buffer = rx_buf;
    hdlc_rx_desc.capacity = sizeof(rx_buf);

    atc_hdlc_params_t params = {
        .config = &hdlc_cfg,
        .platform = &hdlc_plat,
        .tx_window = &hdlc_tw,
        .rx_buf = &hdlc_rx_desc,
    };
    atc_hdlc_init(&hdlc_ctx, params);
    atc_hdlc_link_setup(&hdlc_ctx, cfg->peer_addr);
}

void hdlc_port_run(void) {
    uint_least8_t chunk[HDLC_PORT_RX_CHUNK];
    uint16_t n = port_rx_read(chunk, sizeof(chunk));
    if (n > 0)
        atc_hdlc_data_in(&hdlc_ctx, chunk, n);

    if (t1_active && (port_tick_ms() - t1_started_ms) >= hdlc_cfg.t1_ms) {
        t1_active = 0;
        atc_hdlc_t1_expired(&hdlc_ctx);
    }

    if (t2_active && (port_tick_ms() - t2_started_ms) >= hdlc_cfg.t2_ms) {
        t2_active = 0;
        atc_hdlc_t2_expired(&hdlc_ctx);
    }
}

atc_hdlc_error_t hdlc_port_transmit(const atc_hdlc_u8* data, atc_hdlc_u32 len) {
    return atc_hdlc_transmit_i(&hdlc_ctx, data, len);
}

atc_hdlc_ctx_t* hdlc_port_ctx(void) {
    return &hdlc_ctx;
}
