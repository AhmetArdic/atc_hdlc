#include "hdlc_mcu_port.h"
#include "hdlc_platform.h"

/* ---- Static storage ---- */

static atc_hdlc_context_t   hdlc_ctx;
static atc_hdlc_config_t    hdlc_cfg;
static atc_hdlc_platform_t  hdlc_plat;
static atc_hdlc_tx_window_t hdlc_tw;
static atc_hdlc_rx_buffer_t hdlc_rx_desc;

static atc_hdlc_u8  rx_buf[HDLC_PORT_MAX_INFO + 4u];
static atc_hdlc_u8  tx_slots[HDLC_PORT_WINDOW * HDLC_PORT_MAX_INFO];
static atc_hdlc_u32 tx_lens[HDLC_PORT_WINDOW];

/* ---- Timer state (written by callbacks, read by hdlc_port_run) ---- */

static volatile uint8_t  t1_active;
static volatile uint32_t t1_started_ms;
static volatile uint8_t  t2_active;
static volatile uint32_t t2_started_ms;

/* ---- Weak application hooks ---- */

__attribute__((weak))
void hdlc_port_on_data(const atc_hdlc_u8 *data, atc_hdlc_u16 len)
{
    /* Default: echo received payload back as a new I-frame. */
    atc_hdlc_transmit_i(&hdlc_ctx, data, len);
}

__attribute__((weak))
void hdlc_port_on_event(atc_hdlc_event_t event)
{
    (void)event;
}

/* ---- Internal HDLC callbacks ---- */

static int on_send(atc_hdlc_u8 byte, bool flush, void *user_ctx)
{
    (void)user_ctx;
    port_tx_byte((uint8_t)byte, flush);
    return 0;
}

static void on_data(const atc_hdlc_u8 *data, atc_hdlc_u16 len, void *user_ctx)
{
    (void)user_ctx;
    hdlc_port_on_data(data, len);
}

static void on_event(atc_hdlc_event_t event, void *user_ctx)
{
    (void)user_ctx;
    hdlc_port_on_event(event);
}

static void t1_start(atc_hdlc_u32 ms, void *user_ctx)
{
    (void)ms; (void)user_ctx;
    t1_started_ms = port_tick_ms();
    t1_active     = 1;
}

static void t1_stop(void *user_ctx)
{
    (void)user_ctx;
    t1_active = 0;
}

/*
 * T2 is fired with t2_started_ms = 0 so that the expiry check in
 * hdlc_port_run() triggers on the very next iteration regardless of
 * the configured t2_ms value.  This matches the bare-metal trick from
 * the STM32 reference: sending the RR on the same DMA burst as the last
 * received byte maximises pipeline throughput.
 */
static void t2_start(atc_hdlc_u32 ms, void *user_ctx)
{
    (void)ms; (void)user_ctx;
    t2_started_ms = 0;
    t2_active     = 1;
}

static void t2_stop(void *user_ctx)
{
    (void)user_ctx;
    t2_active = 0;
}

/* ---- Public API ---- */

void hdlc_port_init(const hdlc_port_config_t *cfg)
{
    hdlc_cfg.mode          = ATC_HDLC_MODE_ABM;
    hdlc_cfg.address       = cfg->local_addr;
    hdlc_cfg.max_info_size = HDLC_PORT_MAX_INFO;
    hdlc_cfg.max_retries   = cfg->max_retries;
    hdlc_cfg.t1_ms         = cfg->t1_ms;
    hdlc_cfg.t2_ms         = cfg->t2_ms;

    hdlc_plat.on_send  = on_send;
    hdlc_plat.on_data  = on_data;
    hdlc_plat.on_event = on_event;
    hdlc_plat.user_ctx = NULL;
    hdlc_plat.t1_start = t1_start;
    hdlc_plat.t1_stop  = t1_stop;
    hdlc_plat.t2_start = t2_start;
    hdlc_plat.t2_stop  = t2_stop;

    hdlc_tw.slots         = tx_slots;
    hdlc_tw.slot_lens     = tx_lens;
    hdlc_tw.slot_count    = HDLC_PORT_WINDOW;
    hdlc_tw.slot_capacity = HDLC_PORT_MAX_INFO;

    hdlc_rx_desc.buffer   = rx_buf;
    hdlc_rx_desc.capacity = sizeof(rx_buf);

    atc_hdlc_params_t params = {
        .config    = &hdlc_cfg,
        .platform  = &hdlc_plat,
        .tx_window = &hdlc_tw,
        .rx_buf    = &hdlc_rx_desc,
    };
    atc_hdlc_init(&hdlc_ctx, params);
    atc_hdlc_link_setup(&hdlc_ctx, cfg->peer_addr);
}

void hdlc_port_run(void)
{
    uint8_t  chunk[HDLC_PORT_RX_CHUNK];
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

atc_hdlc_error_t hdlc_port_transmit(const atc_hdlc_u8 *data, atc_hdlc_u32 len)
{
    return atc_hdlc_transmit_i(&hdlc_ctx, data, len);
}

atc_hdlc_context_t *hdlc_port_ctx(void)
{
    return &hdlc_ctx;
}
