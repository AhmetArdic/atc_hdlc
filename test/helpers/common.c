#include "common.h"
#include "../src/hdlc_crc.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Shared mock state --- */
atc_hdlc_u8 mock_output_buffer[16384];
atc_hdlc_u8 mock_rx_buffer[16384];
int mock_output_len = 0;
int mock_frame_count = 0;
int on_data_call_count = 0;
atc_hdlc_u8 last_data_payload[16384];
atc_hdlc_u16 last_data_len = 0;

/* --- Event mock state --- */
int on_event_call_count = 0;
atc_hdlc_event_t last_event = (atc_hdlc_event_t)-1;

/* --- Timer mock state --- */
int mock_t1_start_count = 0;
int mock_t1_stop_count = 0;
atc_hdlc_u32 mock_t1_last_ms = 0;
int mock_t2_start_count = 0;
int mock_t2_stop_count = 0;
atc_hdlc_u32 mock_t2_last_ms = 0;

/* ================================================================
 *  Mock platform callbacks
 * ================================================================ */

int mock_send_cb(atc_hdlc_u8 byte, bool flush, void* user_ctx) {
    (void)user_ctx;
    if (mock_output_len < (int)sizeof(mock_output_buffer))
        mock_output_buffer[mock_output_len++] = byte;
    if (flush)
        mock_frame_count++;
    return 0;
}

void mock_on_data_cb(const atc_hdlc_u8* payload, atc_hdlc_u16 len, void* user_ctx) {
    (void)user_ctx;
    on_data_call_count++;
    last_data_len = len;
    if (len > 0 && payload != NULL)
        memcpy(last_data_payload, payload,
               len < sizeof(last_data_payload) ? len : sizeof(last_data_payload));
    printf("   %s[ON DATA] %u bytes received%s\n", COL_GREEN, len, COL_RESET);
}

void mock_on_event_cb(atc_hdlc_event_t event, void* user_ctx) {
    (void)user_ctx;
    on_event_call_count++;
    last_event = event;
}

/* --- Timer callbacks --- */
void mock_t1_start_cb(atc_hdlc_u32 ms, void* user_ctx) {
    (void)user_ctx;
    mock_t1_start_count++;
    mock_t1_last_ms = ms;
}
void mock_t1_stop_cb(void* user_ctx) {
    (void)user_ctx;
    mock_t1_stop_count++;
}
void mock_t2_start_cb(atc_hdlc_u32 ms, void* user_ctx) {
    (void)user_ctx;
    mock_t2_start_count++;
    mock_t2_last_ms = ms;
}
void mock_t2_stop_cb(void* user_ctx) {
    (void)user_ctx;
    mock_t2_stop_count++;
}

/* ================================================================
 *  Default config / platform / storage
 * ================================================================ */

static const atc_hdlc_config_t s_default_config = {
    .mode = ATC_HDLC_MODE_ABM,
    .address = 0x01,
    .max_info_size = 1024,
    .max_retries = 3,
    .t1_ms = ATC_HDLC_DEFAULT_T1_TIMEOUT,
    .t2_ms = ATC_HDLC_DEFAULT_T2_TIMEOUT,
};

/** Full platform with all mock callbacks wired (including timers). */
static const atc_hdlc_platform_t s_default_platform = {
    .on_send = mock_send_cb,
    .on_data = mock_on_data_cb,
    .on_event = mock_on_event_cb,
    .user_ctx = NULL,
    .t1_start = mock_t1_start_cb,
    .t1_stop = mock_t1_stop_cb,
    .t2_start = mock_t2_start_cb,
    .t2_stop = mock_t2_stop_cb,
};

/* Static backing storage for default single-slot TX window */
static atc_hdlc_u8 s_tx_slots[1 * 1024];
static atc_hdlc_u32 s_tx_slot_lens[1];

static atc_hdlc_tx_window_t s_tx_window = {
    .slots = s_tx_slots,
    .slot_lens = s_tx_slot_lens,
    .slot_capacity = 1024,
    .slot_count = 1,
};

static atc_hdlc_rx_buffer_t s_rx_buf = {
    .buffer = mock_rx_buffer,
    .capacity = sizeof(mock_rx_buffer),
};

/* Multi-slot storage for variable window size tests (up to 7) */
static atc_hdlc_u8 s_tw_slots[7 * 1024];
static atc_hdlc_u32 s_tw_lens[7];

/* ================================================================
 *  Context setup helpers
 * ================================================================ */

void setup_test_context(atc_hdlc_context_t* ctx) {
    reset_test_state();
    if (!ctx)
        return;
    atc_hdlc_params_t p = {.config = &s_default_config,
                           .platform = &s_default_platform,
                           .tx_window = &s_tx_window,
                           .rx_buf = &s_rx_buf};
    atc_hdlc_init(ctx, p);
    ctx->peer_address = 0x02;
}

void setup_test_context_w(atc_hdlc_context_t* ctx, atc_hdlc_u8 window_size) {
    reset_test_state();
    if (!ctx)
        return;

    static atc_hdlc_config_t cfg;
    cfg = s_default_config;

    static atc_hdlc_tx_window_t tw;
    tw.slots = s_tw_slots;
    tw.slot_lens = s_tw_lens;
    tw.slot_capacity = 1024;
    tw.slot_count = window_size;

    atc_hdlc_params_t p = {
        .config = &cfg, .platform = &s_default_platform, .tx_window = &tw, .rx_buf = &s_rx_buf};
    atc_hdlc_init(ctx, p);
    ctx->peer_address = 0x02;
}

void setup_test_context_no_tw(atc_hdlc_context_t* ctx) {
    reset_test_state();
    if (!ctx)
        return;
    /* Pass NULL tx_window — tests ATC_HDLC_ERR_NO_BUFFER path */
    atc_hdlc_params_t p = {.config = &s_default_config,
                           .platform = &s_default_platform,
                           .tx_window = NULL,
                           .rx_buf = &s_rx_buf};
    atc_hdlc_init(ctx, p);
    ctx->peer_address = 0x02;
}

/* ================================================================
 *  State reset
 * ================================================================ */

void reset_test_state(void) {
    mock_output_len = 0;
    mock_frame_count = 0;
    on_data_call_count = 0;
    last_data_len = 0;
    on_event_call_count = 0;
    last_event = (atc_hdlc_event_t)-1;
    mock_t1_start_count = 0;
    mock_t1_stop_count = 0;
    mock_t1_last_ms = 0;
    mock_t2_start_count = 0;
    mock_t2_stop_count = 0;
    mock_t2_last_ms = 0;
    memset(mock_output_buffer, 0, sizeof(mock_output_buffer));
    memset(last_data_payload, 0, sizeof(last_data_payload));
    memset(mock_rx_buffer, 0, sizeof(mock_rx_buffer));
}

/* ================================================================
 *  Helpers
 * ================================================================ */

void print_hexdump(const char* label, const atc_hdlc_u8* data, int len) {
    printf("%s%s (%d bytes):%s ", COL_CYAN, label, len, COL_RESET);
    for (int i = 0; i < len; i++)
        printf("%02X ", data[i]);
    printf(" | ");
    for (int i = 0; i < len; i++)
        printf("%c", isprint(data[i]) ? data[i] : '.');
    printf("\n");
}

void test_pass(const char* test_name) {
    printf("%s[PASS] %s%s\n\n", COL_GREEN, test_name, COL_RESET);
}

void test_fail(const char* test_name, const char* reason) {
    printf("%s[FAIL] %s: %s%s\n", COL_RED, test_name, reason, COL_RESET);
    exit(1);
}

int test_pack_frame(atc_hdlc_u8 addr, atc_hdlc_u8 ctrl, const atc_hdlc_u8* info,
                    atc_hdlc_u16 info_len, atc_hdlc_u8* out, int out_cap) {
    atc_hdlc_u16 crc = ATC_HDLC_FCS_INIT_VALUE;
    int n = 0;
#define _RAW(b)                                                                                    \
    do {                                                                                           \
        if (n >= out_cap)                                                                          \
            return 0;                                                                              \
        out[n++] = (atc_hdlc_u8)(b);                                                               \
    } while (0)
#define _ESC(b)                                                                                    \
    do {                                                                                           \
        atc_hdlc_u8 _b = (atc_hdlc_u8)(b);                                                         \
        if (_b == 0x7E || _b == 0x7D) {                                                            \
            _RAW(0x7D);                                                                            \
            _RAW(_b ^ 0x20);                                                                       \
        } else {                                                                                   \
            _RAW(_b);                                                                              \
        }                                                                                          \
    } while (0)
#define _ESCC(b)                                                                                   \
    do {                                                                                           \
        atc_hdlc_u8 _c = (atc_hdlc_u8)(b);                                                         \
        crc = atc_hdlc_crc_ccitt_update(crc, _c);                                                  \
        _ESC(_c);                                                                                  \
    } while (0)
    _RAW(0x7E);
    _ESCC(addr);
    _ESCC(ctrl);
    for (atc_hdlc_u16 i = 0; i < info_len && info; i++)
        _ESCC(info[i]);
    _ESC((atc_hdlc_u8)(crc & 0xFF));
    _ESC((atc_hdlc_u8)(crc >> 8));
    _RAW(0x7E);
#undef _RAW
#undef _ESC
#undef _ESCC
    return n;
}

test_frame_t test_unpack_frame(const atc_hdlc_u8* buf, int buf_len, atc_hdlc_u8* flat,
                               int flat_cap) {
    test_frame_t r = {0};
    if (!buf || !flat || buf_len < 4)
        return r;

    int wi = 0;
    bool inside = false, esc = false;

    for (int i = 0; i < buf_len; i++) {
        atc_hdlc_u8 b = buf[i];
        if (b == 0x7E) {
            if (inside && wi >= 4) {
                break;
            }
            inside = true;
            wi = 0;
            continue;
        }
        if (!inside)
            continue;
        if (b == 0x7D) {
            esc = true;
            continue;
        }
        if (esc) {
            b = (atc_hdlc_u8)(b ^ 0x20);
            esc = false;
        }
        if (wi >= flat_cap)
            return r;
        flat[wi++] = b;
    }
    if (wi < 4)
        return r;

    atc_hdlc_u16 calced = ATC_HDLC_FCS_INIT_VALUE;
    int data_len = wi - 2;
    for (int i = 0; i < data_len; i++)
        calced = atc_hdlc_crc_ccitt_update(calced, flat[i]);
    atc_hdlc_u16 rx_fcs = (atc_hdlc_u16)(flat[data_len] | ((atc_hdlc_u16)flat[data_len + 1] << 8));
    if (calced != rx_fcs)
        return r;

    r.address = flat[0];
    r.control = flat[1];
    r.info_len = (atc_hdlc_u16)(data_len > 2 ? data_len - 2 : 0);
    r.info = (r.info_len > 0) ? &flat[2] : NULL;
    r.valid = true;
    return r;
}
