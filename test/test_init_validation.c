/**
 * @file test_init_validation.c
 * @brief Tests for atc_hdlc_init() parameter validation and error codes.
 *
 * Covers every ATC_HDLC_ERR_* path reachable from atc_hdlc_init() and
 * verifies that error codes are returned (not just "non-zero").
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "../inc/hdlc.h"
#include "../src/hdlc_private.h"
#include "test_common.h"

/* ================================================================
 *  Minimal valid descriptors reused across tests
 * ================================================================ */
static atc_hdlc_u8  s_rx_storage[1028]; /* 1024 + 4 overhead */
static atc_hdlc_u8  s_tx_slots[1 * 1024];
static atc_hdlc_u32 s_tx_lens[1];
static atc_hdlc_u8  s_tx_seq[8];

static atc_hdlc_config_t make_valid_cfg(void) {
    atc_hdlc_config_t c;
    memset(&c, 0, sizeof(c));
    c.mode           = ATC_HDLC_MODE_ABM;
    c.address        = 0x01;
    c.window_size    = 1;
    c.max_frame_size = 1024;
    c.max_retries    = 3;
    c.t1_ms          = 1000;
    c.t2_ms          = 10;
    c.use_extended   = false;
    return c;
}

static atc_hdlc_platform_t make_valid_plat(void) {
    atc_hdlc_platform_t p;
    memset(&p, 0, sizeof(p));
    p.on_send  = mock_send_cb;
    p.on_data  = NULL;
    p.on_event = NULL;
    p.user_ctx = NULL;
    p.t1_start = mock_t1_start_cb;
    p.t1_stop  = mock_t1_stop_cb;
    p.t2_start = mock_t2_start_cb;
    p.t2_stop  = mock_t2_stop_cb;
    return p;
}

static atc_hdlc_tx_window_t make_valid_tw(void) {
    atc_hdlc_tx_window_t tw;
    tw.slots         = s_tx_slots;
    tw.slot_lens     = s_tx_lens;
    tw.seq_to_slot   = s_tx_seq;
    tw.slot_capacity = 1024;
    tw.slot_count    = 1;
    return tw;
}

static atc_hdlc_rx_buffer_t make_valid_rx(void) {
    atc_hdlc_rx_buffer_t rx;
    rx.buffer   = s_rx_storage;
    rx.capacity = sizeof(s_rx_storage);
    return rx;
}

/* ================================================================
 *  Tests
 * ================================================================ */

void test_init_null_params(void) {
    printf("TEST: init — NULL parameter checks\n");

    atc_hdlc_context_t ctx;
    atc_hdlc_config_t   cfg  = make_valid_cfg();
    atc_hdlc_platform_t plat = make_valid_plat();
    atc_hdlc_tx_window_t tw  = make_valid_tw();
    atc_hdlc_rx_buffer_t rx  = make_valid_rx();

    /* NULL ctx */
    if (atc_hdlc_init(NULL, &cfg, &plat, &tw, &rx) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("Init NULL", "NULL ctx should return INVALID_PARAM");

    /* NULL config */
    if (atc_hdlc_init(&ctx, NULL, &plat, &tw, &rx) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("Init NULL", "NULL config should return INVALID_PARAM");

    /* NULL platform */
    if (atc_hdlc_init(&ctx, &cfg, NULL, &tw, &rx) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("Init NULL", "NULL platform should return INVALID_PARAM");

    /* NULL rx_buf */
    if (atc_hdlc_init(&ctx, &cfg, &plat, &tw, NULL) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("Init NULL", "NULL rx_buf should return INVALID_PARAM");

    /* NULL on_send */
    atc_hdlc_platform_t bad_plat = plat;
    bad_plat.on_send = NULL;
    if (atc_hdlc_init(&ctx, &cfg, &bad_plat, &tw, &rx) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("Init NULL", "NULL on_send should return INVALID_PARAM");

    /* NULL rx_buf.buffer */
    atc_hdlc_rx_buffer_t bad_rx = rx;
    bad_rx.buffer = NULL;
    if (atc_hdlc_init(&ctx, &cfg, &plat, &tw, &bad_rx) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("Init NULL", "NULL rx buffer should return INVALID_PARAM");

    /* tx_window == NULL is OK (reliable TX disabled) */
    if (atc_hdlc_init(&ctx, &cfg, &plat, NULL, &rx) != ATC_HDLC_OK)
        test_fail("Init NULL", "NULL tx_window should be accepted");

    test_pass("Init — NULL parameter checks");
}

void test_init_unsupported_mode(void) {
    printf("TEST: init — unsupported mode\n");

    atc_hdlc_context_t ctx;
    atc_hdlc_config_t   cfg  = make_valid_cfg();
    atc_hdlc_platform_t plat = make_valid_plat();
    atc_hdlc_rx_buffer_t rx  = make_valid_rx();

    /* Non-ABM mode */
    cfg.mode = ATC_HDLC_MODE_ARM;
    if (atc_hdlc_init(&ctx, &cfg, &plat, NULL, &rx) != ATC_HDLC_ERR_UNSUPPORTED_MODE)
        test_fail("Init Mode", "ARM mode should return UNSUPPORTED_MODE");

    cfg.mode = ATC_HDLC_MODE_NRM;
    if (atc_hdlc_init(&ctx, &cfg, &plat, NULL, &rx) != ATC_HDLC_ERR_UNSUPPORTED_MODE)
        test_fail("Init Mode", "NRM mode should return UNSUPPORTED_MODE");

    /* use_extended = true */
    cfg.mode = ATC_HDLC_MODE_ABM;
    cfg.use_extended = true;
    if (atc_hdlc_init(&ctx, &cfg, &plat, NULL, &rx) != ATC_HDLC_ERR_UNSUPPORTED_MODE)
        test_fail("Init Mode", "use_extended=true should return UNSUPPORTED_MODE");

    test_pass("Init — unsupported mode");
}

void test_init_invalid_window_size(void) {
    printf("TEST: init — invalid window size\n");

    atc_hdlc_context_t ctx;
    atc_hdlc_config_t   cfg  = make_valid_cfg();
    atc_hdlc_platform_t plat = make_valid_plat();
    atc_hdlc_rx_buffer_t rx  = make_valid_rx();

    /* window_size = 0 */
    cfg.window_size = 0;
    if (atc_hdlc_init(&ctx, &cfg, &plat, NULL, &rx) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("Init Window", "window_size=0 should return INVALID_PARAM");

    /* window_size = 8 (> 7) */
    cfg.window_size = 8;
    if (atc_hdlc_init(&ctx, &cfg, &plat, NULL, &rx) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("Init Window", "window_size=8 should return INVALID_PARAM");

    /* window_size = 7 — valid boundary */
    cfg.window_size = 7;
    atc_hdlc_tx_window_t tw7;
    static atc_hdlc_u8  tw7_slots[7 * 1024];
    static atc_hdlc_u32 tw7_lens[7];
    static atc_hdlc_u8  tw7_seq[8];
    tw7.slots = tw7_slots; tw7.slot_lens = tw7_lens; tw7.seq_to_slot = tw7_seq;
    tw7.slot_capacity = 1024; tw7.slot_count = 7;
    if (atc_hdlc_init(&ctx, &cfg, &plat, &tw7, &rx) != ATC_HDLC_OK)
        test_fail("Init Window", "window_size=7 should be valid");

    test_pass("Init — invalid window size");
}

void test_init_inconsistent_rx_buffer(void) {
    printf("TEST: init — rx buffer too small\n");

    atc_hdlc_context_t ctx;
    atc_hdlc_config_t   cfg  = make_valid_cfg();
    atc_hdlc_platform_t plat = make_valid_plat();

    /* Buffer exactly max_frame_size — NOT enough (needs +4 for header/FCS) */
    static atc_hdlc_u8 small_buf[1024];
    atc_hdlc_rx_buffer_t rx_small = { .buffer = small_buf, .capacity = 1024 };
    if (atc_hdlc_init(&ctx, &cfg, &plat, NULL, &rx_small) != ATC_HDLC_ERR_INCONSISTENT_BUFFER)
        test_fail("Init RX Buf", "capacity == max_frame_size should return INCONSISTENT_BUFFER");

    /* Buffer = max_frame_size + 3 — still one byte short */
    atc_hdlc_rx_buffer_t rx_near = { .buffer = small_buf, .capacity = 1027 };
    if (atc_hdlc_init(&ctx, &cfg, &plat, NULL, &rx_near) != ATC_HDLC_ERR_INCONSISTENT_BUFFER)
        test_fail("Init RX Buf", "capacity = max_frame_size+3 should return INCONSISTENT_BUFFER");

    /* Buffer = max_frame_size + 4 — exact minimum */
    atc_hdlc_rx_buffer_t rx_exact = { .buffer = small_buf, .capacity = 1028 };
    if (atc_hdlc_init(&ctx, &cfg, &plat, NULL, &rx_exact) != ATC_HDLC_OK)
        test_fail("Init RX Buf", "capacity = max_frame_size+4 should succeed");

    test_pass("Init — rx buffer too small");
}

void test_init_inconsistent_tx_window(void) {
    printf("TEST: init — tx window inconsistencies\n");

    atc_hdlc_context_t ctx;
    atc_hdlc_config_t   cfg  = make_valid_cfg();
    atc_hdlc_platform_t plat = make_valid_plat();
    atc_hdlc_rx_buffer_t rx  = make_valid_rx();
    atc_hdlc_tx_window_t tw  = make_valid_tw();

    /* NULL slots inside tx_window */
    atc_hdlc_tx_window_t bad_tw = tw;
    bad_tw.slots = NULL;
    if (atc_hdlc_init(&ctx, &cfg, &plat, &bad_tw, &rx) != ATC_HDLC_ERR_INCONSISTENT_BUFFER)
        test_fail("Init TX Window", "NULL slots should return INCONSISTENT_BUFFER");

    /* NULL slot_lens */
    bad_tw = tw; bad_tw.slot_lens = NULL;
    if (atc_hdlc_init(&ctx, &cfg, &plat, &bad_tw, &rx) != ATC_HDLC_ERR_INCONSISTENT_BUFFER)
        test_fail("Init TX Window", "NULL slot_lens should return INCONSISTENT_BUFFER");

    /* NULL seq_to_slot */
    bad_tw = tw; bad_tw.seq_to_slot = NULL;
    if (atc_hdlc_init(&ctx, &cfg, &plat, &bad_tw, &rx) != ATC_HDLC_ERR_INCONSISTENT_BUFFER)
        test_fail("Init TX Window", "NULL seq_to_slot should return INCONSISTENT_BUFFER");

    /* slot_count != window_size */
    bad_tw = tw; bad_tw.slot_count = 3; /* cfg.window_size = 1 */
    if (atc_hdlc_init(&ctx, &cfg, &plat, &bad_tw, &rx) != ATC_HDLC_ERR_INCONSISTENT_BUFFER)
        test_fail("Init TX Window", "slot_count != window_size should return INCONSISTENT_BUFFER");

    /* slot_capacity < max_frame_size */
    bad_tw = tw; bad_tw.slot_capacity = 512; /* cfg.max_frame_size = 1024 */
    if (atc_hdlc_init(&ctx, &cfg, &plat, &bad_tw, &rx) != ATC_HDLC_ERR_INCONSISTENT_BUFFER)
        test_fail("Init TX Window", "slot_capacity < max_frame_size should return INCONSISTENT_BUFFER");

    /* Valid tx_window */
    if (atc_hdlc_init(&ctx, &cfg, &plat, &tw, &rx) != ATC_HDLC_OK)
        test_fail("Init TX Window", "Valid tx_window should succeed");

    test_pass("Init — tx window inconsistencies");
}

void test_init_success_sets_state(void) {
    printf("TEST: init — successful init sets DISCONNECTED state\n");

    atc_hdlc_context_t ctx;
    atc_hdlc_config_t   cfg  = make_valid_cfg();
    atc_hdlc_platform_t plat = make_valid_plat();
    atc_hdlc_tx_window_t tw  = make_valid_tw();
    atc_hdlc_rx_buffer_t rx  = make_valid_rx();

    atc_hdlc_error_t err = atc_hdlc_init(&ctx, &cfg, &plat, &tw, &rx);
    if (err != ATC_HDLC_OK)
        test_fail("Init Success", "Valid init should return OK");

    if (ctx.current_state != ATC_HDLC_STATE_DISCONNECTED)
        test_fail("Init Success", "State should be DISCONNECTED after init");

    if (ctx.rx_state != HDLC_RX_STATE_HUNT)
        test_fail("Init Success", "RX state should be HUNT after init");

    if (ctx.vs != 0 || ctx.vr != 0 || ctx.va != 0)
        test_fail("Init Success", "Sequence variables should be 0 after init");

    if (ctx.t1_active || ctx.t2_active)
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
    test_init_invalid_window_size();
    test_init_inconsistent_rx_buffer();
    test_init_inconsistent_tx_window();
    test_init_success_sets_state();

    printf("\n%sALL INIT VALIDATION TESTS PASSED!%s\n", COL_GREEN, COL_RESET);
    return 0;
}
