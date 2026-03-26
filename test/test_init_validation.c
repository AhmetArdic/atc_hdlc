/**
 * @file test_init_validation.c
 * @brief Tests for atc_hdlc_init() parameter validation and error codes.
 *
 * Covers every ATC_HDLC_ERR_* path reachable from atc_hdlc_init() and
 * verifies that error codes are returned (not just "non-zero").
 */

#include "../inc/hdlc.h"
#include "../src/hdlc_frame.h"
#include "test_common.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  Minimal valid descriptors reused across tests
 * ================================================================ */
static atc_hdlc_u8 s_rx_storage[1028]; /* 1024 + 4 overhead */
static atc_hdlc_u8 s_tx_slots[1 * 1024];
static atc_hdlc_u32 s_tx_lens[1];

static atc_hdlc_config_t make_valid_cfg(void) {
    atc_hdlc_config_t c;
    memset(&c, 0, sizeof(c));
    c.mode = ATC_HDLC_MODE_ABM;
    c.address = 0x01;
    c.max_info_size = 1024;
    c.max_retries = 3;
    c.t1_ms = 1000;
    c.t2_ms = 10;
    return c;
}

static atc_hdlc_platform_t make_valid_plat(void) {
    atc_hdlc_platform_t p;
    memset(&p, 0, sizeof(p));
    p.on_send = mock_send_cb;
    p.on_data = NULL;
    p.on_event = NULL;
    p.user_ctx = NULL;
    p.t1_start = mock_t1_start_cb;
    p.t1_stop = mock_t1_stop_cb;
    p.t2_start = mock_t2_start_cb;
    p.t2_stop = mock_t2_stop_cb;
    return p;
}

static atc_hdlc_tx_window_t make_valid_tw(void) {
    atc_hdlc_tx_window_t tw;
    tw.slots = s_tx_slots;
    tw.slot_lens = s_tx_lens;
    tw.slot_capacity = 1024;
    tw.slot_count = 1;
    return tw;
}

static atc_hdlc_rx_buffer_t make_valid_rx(void) {
    atc_hdlc_rx_buffer_t rx;
    rx.buffer = s_rx_storage;
    rx.capacity = sizeof(s_rx_storage);
    return rx;
}

/* ================================================================
 *  Tests
 * ================================================================ */

void test_init_null_params(void) {
    printf("TEST: init — NULL parameter checks\n");

    atc_hdlc_context_t ctx;
    atc_hdlc_config_t cfg = make_valid_cfg();
    atc_hdlc_platform_t plat = make_valid_plat();
    atc_hdlc_tx_window_t tw = make_valid_tw();
    atc_hdlc_rx_buffer_t rx = make_valid_rx();
    atc_hdlc_params_t p = {.config = &cfg, .platform = &plat, .tx_window = &tw, .rx_buf = &rx};

    /* NULL ctx */
    if (atc_hdlc_init(NULL, p) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("Init NULL", "NULL ctx should return INVALID_PARAM");

    /* NULL config */
    p.config = NULL;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("Init NULL", "NULL config should return INVALID_PARAM");
    p.config = &cfg;

    /* NULL platform */
    p.platform = NULL;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("Init NULL", "NULL platform should return INVALID_PARAM");
    p.platform = &plat;

    /* NULL rx_buf */
    p.rx_buf = NULL;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("Init NULL", "NULL rx_buf should return INVALID_PARAM");
    p.rx_buf = &rx;

    /* NULL on_send */
    atc_hdlc_platform_t bad_plat = plat;
    bad_plat.on_send = NULL;
    p.platform = &bad_plat;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("Init NULL", "NULL on_send should return INVALID_PARAM");
    p.platform = &plat;

    /* NULL rx_buf.buffer */
    atc_hdlc_rx_buffer_t bad_rx = rx;
    bad_rx.buffer = NULL;
    p.rx_buf = &bad_rx;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("Init NULL", "NULL rx buffer should return INVALID_PARAM");
    p.rx_buf = &rx;

    /* tx_window == NULL is OK (reliable TX disabled) */
    p.tx_window = NULL;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_OK)
        test_fail("Init NULL", "NULL tx_window should be accepted");

    test_pass("Init — NULL parameter checks");
}

void test_init_unsupported_mode(void) {
    printf("TEST: init — unsupported mode\n");

    atc_hdlc_context_t ctx;
    atc_hdlc_config_t cfg = make_valid_cfg();
    atc_hdlc_platform_t plat = make_valid_plat();
    atc_hdlc_rx_buffer_t rx = make_valid_rx();
    atc_hdlc_params_t p = {.config = &cfg, .platform = &plat, .tx_window = NULL, .rx_buf = &rx};

    /* Non-ABM mode (cast to force invalid value past enum) */
    cfg.mode = (atc_hdlc_link_mode_t)1;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_ERR_UNSUPPORTED_MODE)
        test_fail("Init Mode", "non-ABM mode should return UNSUPPORTED_MODE");

    cfg.mode = (atc_hdlc_link_mode_t)2;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_ERR_UNSUPPORTED_MODE)
        test_fail("Init Mode", "non-ABM mode should return UNSUPPORTED_MODE");

    test_pass("Init — unsupported mode");
}

void test_init_invalid_slot_count(void) {
    printf("TEST: init — invalid slot count\n");

    atc_hdlc_context_t ctx;
    atc_hdlc_config_t cfg = make_valid_cfg();
    atc_hdlc_platform_t plat = make_valid_plat();
    atc_hdlc_rx_buffer_t rx = make_valid_rx();
    atc_hdlc_tx_window_t tw = make_valid_tw();
    atc_hdlc_params_t p = {.config = &cfg, .platform = &plat, .tx_window = &tw, .rx_buf = &rx};

    /* slot_count = 0 */
    tw.slot_count = 0;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("Init Slot Count", "slot_count=0 should return INVALID_PARAM");

    /* slot_count = 8 (> 7) */
    tw.slot_count = 8;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("Init Slot Count", "slot_count=8 should return INVALID_PARAM");

    /* slot_count = 7 — valid boundary */
    static atc_hdlc_u8 tw7_slots[7 * 1024];
    static atc_hdlc_u32 tw7_lens[7];
    tw.slots = tw7_slots;
    tw.slot_lens = tw7_lens;
    tw.slot_capacity = 1024;
    tw.slot_count = 7;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_OK)
        test_fail("Init Slot Count", "slot_count=7 should be valid");

    test_pass("Init — invalid slot count");
}

void test_init_inconsistent_rx_buffer(void) {
    printf("TEST: init — rx buffer too small\n");

    atc_hdlc_context_t ctx;
    atc_hdlc_config_t cfg = make_valid_cfg();
    atc_hdlc_platform_t plat = make_valid_plat();
    atc_hdlc_params_t p = {.config = &cfg, .platform = &plat, .tx_window = NULL, .rx_buf = NULL};

    /* Buffer exactly max_info_size — NOT enough (needs +4 for header/FCS) */
    static atc_hdlc_u8 small_buf[1024];
    atc_hdlc_rx_buffer_t rx_small = {.buffer = small_buf, .capacity = 1024};
    p.rx_buf = &rx_small;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_ERR_INCONSISTENT_BUFFER)
        test_fail("Init RX Buf", "capacity == max_info_size should return INCONSISTENT_BUFFER");

    /* Buffer = max_info_size + 3 — still one byte short */
    atc_hdlc_rx_buffer_t rx_near = {.buffer = small_buf, .capacity = 1027};
    p.rx_buf = &rx_near;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_ERR_INCONSISTENT_BUFFER)
        test_fail("Init RX Buf", "capacity = max_info_size+3 should return INCONSISTENT_BUFFER");

    /* Buffer = max_info_size + 4 — exact minimum */
    atc_hdlc_rx_buffer_t rx_exact = {.buffer = small_buf, .capacity = 1028};
    p.rx_buf = &rx_exact;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_OK)
        test_fail("Init RX Buf", "capacity = max_info_size+4 should succeed");

    test_pass("Init — rx buffer too small");
}

void test_init_inconsistent_tx_window(void) {
    printf("TEST: init — tx window inconsistencies\n");

    atc_hdlc_context_t ctx;
    atc_hdlc_config_t cfg = make_valid_cfg();
    atc_hdlc_platform_t plat = make_valid_plat();
    atc_hdlc_rx_buffer_t rx = make_valid_rx();
    atc_hdlc_tx_window_t tw = make_valid_tw();
    atc_hdlc_params_t p = {.config = &cfg, .platform = &plat, .tx_window = NULL, .rx_buf = &rx};

    /* NULL slots inside tx_window */
    atc_hdlc_tx_window_t bad_tw = tw;
    bad_tw.slots = NULL;
    p.tx_window = &bad_tw;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_ERR_INCONSISTENT_BUFFER)
        test_fail("Init TX Window", "NULL slots should return INCONSISTENT_BUFFER");

    /* NULL slot_lens */
    bad_tw = tw;
    bad_tw.slot_lens = NULL;
    p.tx_window = &bad_tw;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_ERR_INCONSISTENT_BUFFER)
        test_fail("Init TX Window", "NULL slot_lens should return INCONSISTENT_BUFFER");

    /* slot_capacity < max_info_size */
    bad_tw = tw;
    bad_tw.slot_capacity = 512; /* cfg.max_info_size = 1024 */
    p.tx_window = &bad_tw;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_ERR_INCONSISTENT_BUFFER)
        test_fail("Init TX Window",
                  "slot_capacity < max_info_size should return INCONSISTENT_BUFFER");

    /* Valid tx_window */
    p.tx_window = &tw;
    if (atc_hdlc_init(&ctx, p) != ATC_HDLC_OK)
        test_fail("Init TX Window", "Valid tx_window should succeed");

    test_pass("Init — tx window inconsistencies");
}

void test_init_success_sets_state(void) {
    printf("TEST: init — successful init sets DISCONNECTED state\n");

    atc_hdlc_context_t ctx;
    atc_hdlc_config_t cfg = make_valid_cfg();
    atc_hdlc_platform_t plat = make_valid_plat();
    atc_hdlc_tx_window_t tw = make_valid_tw();
    atc_hdlc_rx_buffer_t rx = make_valid_rx();
    atc_hdlc_params_t p = {.config = &cfg, .platform = &plat, .tx_window = &tw, .rx_buf = &rx};

    atc_hdlc_error_t err = atc_hdlc_init(&ctx, p);
    if (err != ATC_HDLC_OK)
        test_fail("Init Success", "Valid init should return OK");

    if (ctx.current_state != ATC_HDLC_STATE_DISCONNECTED)
        test_fail("Init Success", "State should be DISCONNECTED after init");

    if (ctx.rx_state != RX_HUNT)
        test_fail("Init Success", "RX state should be HUNT after init");

    if (ctx.vs != 0 || ctx.vr != 0 || ctx.va != 0)
        test_fail("Init Success", "Sequence variables should be 0 after init");

    if ((ctx.flags & HDLC_F_T1_ACTIVE) || (ctx.flags & HDLC_F_T2_ACTIVE))
        test_fail("Init Success", "No timers should be active after init");

    if (ctx.config != &cfg)
        test_fail("Init Success", "config pointer not stored");

    if (ctx.platform != &plat)
        test_fail("Init Success", "platform pointer not stored");

    test_pass("Init — successful init sets DISCONNECTED state");
}

/* ================================================================
 *  main
 * ================================================================ */
int main(void) {
    printf("\n%sSTARTING INIT VALIDATION TEST SUITE%s\n", COL_YELLOW, COL_RESET);
    printf("----------------------------------------\n\n");

    test_init_null_params();
    test_init_unsupported_mode();
    test_init_invalid_slot_count();
    test_init_inconsistent_rx_buffer();
    test_init_inconsistent_tx_window();
    test_init_success_sets_state();

    printf("\n%sALL INIT VALIDATION TESTS PASSED!%s\n", COL_GREEN, COL_RESET);
    return 0;
}
