/**
 * @file test_error_codes.c
 * @brief Exhaustive tests for ATC_HDLC_ERR_* return codes from the public API.
 *
 * Each test verifies a specific error code is returned (not just non-zero)
 * when the documented error condition is triggered.
 */

#include "../../inc/hdlc.h"
#include "../../src/hdlc_frame.h"
#include "../helpers/common.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  Helpers — connect a context to CONNECTED state
 * ================================================================ */
static void force_connected(atc_hdlc_context_t* ctx) {
    ctx->current_state = ATC_HDLC_STATE_CONNECTED;
    ctx->vs = 0;
    ctx->vr = 0;
    ctx->va = 0;
}

/* ================================================================
 *  Tests
 * ================================================================ */

/**
 * @brief ATC_HDLC_ERR_INVALID_STATE — API called in wrong state.
 */
void test_err_invalid_state(void) {
    printf("TEST: ATC_HDLC_ERR_INVALID_STATE\n");

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);

    /* transmit_i in DISCONNECTED */
    atc_hdlc_u8 payload[] = {0xAA};
    if (atc_hdlc_transmit_i(&ctx, payload, 1) != ATC_HDLC_ERR_INVALID_STATE)
        test_fail("ERR_INVALID_STATE", "transmit_i in DISCONNECTED should fail");

    /* disconnect in DISCONNECTED */
    if (atc_hdlc_disconnect(&ctx) != ATC_HDLC_ERR_INVALID_STATE)
        test_fail("ERR_INVALID_STATE", "disconnect in DISCONNECTED should fail");

    /* link_setup in CONNECTING (not DISCONNECTED) */
    ctx.current_state = ATC_HDLC_STATE_CONNECTING;
    ctx.peer_address = 0x02;
    if (atc_hdlc_link_setup(&ctx, 0x02) != ATC_HDLC_ERR_INVALID_STATE)
        test_fail("ERR_INVALID_STATE", "link_setup in CONNECTING should fail");

    /* set_local_busy in DISCONNECTED */
    ctx.current_state = ATC_HDLC_STATE_DISCONNECTED;
    if (atc_hdlc_set_local_busy(&ctx, true) != ATC_HDLC_ERR_INVALID_STATE)
        test_fail("ERR_INVALID_STATE", "set_local_busy in DISCONNECTED should fail");

    test_pass("ATC_HDLC_ERR_INVALID_STATE");
}

/**
 * @brief ATC_HDLC_ERR_INVALID_PARAM — NULL pointer arguments.
 */
void test_err_invalid_param(void) {
    printf("TEST: ATC_HDLC_ERR_INVALID_PARAM\n");

    /* link_setup NULL ctx */
    if (atc_hdlc_link_setup(NULL, 0x02) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("ERR_INVALID_PARAM", "link_setup(NULL) should fail");

    /* disconnect NULL ctx */
    if (atc_hdlc_disconnect(NULL) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("ERR_INVALID_PARAM", "disconnect(NULL) should fail");

    /* link_reset NULL ctx */
    if (atc_hdlc_link_reset(NULL) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("ERR_INVALID_PARAM", "link_reset(NULL) should fail");

    /* transmit_i NULL ctx */
    atc_hdlc_u8 payload[] = {0x01};
    if (atc_hdlc_transmit_i(NULL, payload, 1) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("ERR_INVALID_PARAM", "transmit_i(NULL ctx) should fail");

    /* transmit_ui NULL ctx */
    if (atc_hdlc_transmit_ui(NULL, 0x02, payload, 1) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("ERR_INVALID_PARAM", "transmit_ui(NULL ctx) should fail");

    /* set_local_busy NULL ctx */
    if (atc_hdlc_set_local_busy(NULL, true) != ATC_HDLC_ERR_INVALID_PARAM)
        test_fail("ERR_INVALID_PARAM", "set_local_busy(NULL) should fail");

    test_pass("ATC_HDLC_ERR_INVALID_PARAM");
}

/**
 * @brief ATC_HDLC_ERR_NO_BUFFER — transmit_i without tx_window.
 */
void test_err_no_buffer(void) {
    printf("TEST: ATC_HDLC_ERR_NO_BUFFER\n");

    atc_hdlc_context_t ctx;
    setup_test_context_no_tw(&ctx);
    force_connected(&ctx);

    atc_hdlc_u8 payload[] = {0xAA, 0xBB};
    atc_hdlc_error_t err = atc_hdlc_transmit_i(&ctx, payload, 2);
    if (err != ATC_HDLC_ERR_NO_BUFFER)
        test_fail("ERR_NO_BUFFER", "transmit_i without tx_window should return NO_BUFFER");

    test_pass("ATC_HDLC_ERR_NO_BUFFER");
}

/**
 * @brief ATC_HDLC_ERR_WINDOW_FULL — send more frames than window allows.
 */
void test_err_window_full(void) {
    printf("TEST: ATC_HDLC_ERR_WINDOW_FULL\n");

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx); /* slot_count = 1 */
    force_connected(&ctx);

    atc_hdlc_u8 payload[] = {0x01, 0x02};

    /* First frame — slot_count=1, should succeed */
    if (atc_hdlc_transmit_i(&ctx, payload, 2) != ATC_HDLC_OK)
        test_fail("ERR_WINDOW_FULL", "First frame should succeed");

    /* Second frame — window full */
    atc_hdlc_error_t err = atc_hdlc_transmit_i(&ctx, payload, 2);
    if (err != ATC_HDLC_ERR_WINDOW_FULL)
        test_fail("ERR_WINDOW_FULL", "Second frame should return WINDOW_FULL");

    test_pass("ATC_HDLC_ERR_WINDOW_FULL");
}

/**
 * @brief ATC_HDLC_ERR_FRAME_TOO_LARGE — payload > max_info_size.
 */
void test_err_frame_too_large(void) {
    printf("TEST: ATC_HDLC_ERR_FRAME_TOO_LARGE\n");

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx); /* max_info_size = 1024 */
    force_connected(&ctx);

    /* Payload exactly at limit — OK */
    static atc_hdlc_u8 big[1024];
    if (atc_hdlc_transmit_i(&ctx, big, 1024) != ATC_HDLC_OK)
        test_fail("ERR_FRAME_TOO_LARGE", "1024-byte frame should succeed");

    /* ACK so window is free again */
    ctx.va = ctx.vs;
    t1_stop(&ctx);

    /* One byte over limit */
    static atc_hdlc_u8 oversized[1025];
    atc_hdlc_error_t err = atc_hdlc_transmit_i(&ctx, oversized, 1025);
    if (err != ATC_HDLC_ERR_FRAME_TOO_LARGE)
        test_fail("ERR_FRAME_TOO_LARGE", "1025-byte frame should return FRAME_TOO_LARGE");

    /* transmit_ui over limit */
    err = atc_hdlc_transmit_ui(&ctx, 0x02, oversized, 1025);
    if (err != ATC_HDLC_ERR_FRAME_TOO_LARGE)
        test_fail("ERR_FRAME_TOO_LARGE", "transmit_ui over limit should return FRAME_TOO_LARGE");

    test_pass("ATC_HDLC_ERR_FRAME_TOO_LARGE");
}

/**
 * @brief ATC_HDLC_ERR_REMOTE_BUSY — transmit_i while peer sent RNR.
 */
void test_err_remote_busy(void) {
    printf("TEST: ATC_HDLC_ERR_REMOTE_BUSY\n");

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    force_connected(&ctx);

    ctx.flags |= HDLC_F_REMOTE_BUSY; /* simulate received RNR */

    atc_hdlc_u8 payload[] = {0x55};
    atc_hdlc_error_t err = atc_hdlc_transmit_i(&ctx, payload, 1);
    if (err != ATC_HDLC_ERR_REMOTE_BUSY)
        test_fail("ERR_REMOTE_BUSY", "transmit_i while remote_busy should return REMOTE_BUSY");

    test_pass("ATC_HDLC_ERR_REMOTE_BUSY");
}

/**
 * @brief ATC_HDLC_ERR_MAX_RETRY — N2 retries exceeded causes link failure.
 */
void test_err_max_retry(void) {
    printf("TEST: ATC_HDLC_ERR_MAX_RETRY (link failure via T1)\n");

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx); /* max_retries = 3 */
    ctx.peer_address = 0x02;

    atc_hdlc_link_setup(&ctx, 0x02);

    /* Exhaust retries: max_retries=3, call t1_expired 4 times (1 initial + 3 retries → fail) */
    for (int i = 0; i < 4; i++) {
        atc_hdlc_t1_expired(&ctx);
    }

    /* After N2 exceeded, station must be DISCONNECTED */
    if (ctx.current_state != ATC_HDLC_STATE_DISCONNECTED)
        test_fail("ERR_MAX_RETRY", "State should be DISCONNECTED after N2 exceeded");

    /* LINK_FAILURE event must have fired */
    if (last_event != ATC_HDLC_EVENT_LINK_FAILURE)
        test_fail("ERR_MAX_RETRY", "LINK_FAILURE event not fired");

    test_pass("ATC_HDLC_ERR_MAX_RETRY (link failure)");
}

/* ================================================================
 *  main
 * ================================================================ */
int main(void) {
    printf("\n%sSTARTING ERROR CODE TEST SUITE%s\n", COL_YELLOW, COL_RESET);
    printf("----------------------------------------\n\n");

    test_err_invalid_state();
    test_err_invalid_param();
    test_err_no_buffer();
    test_err_window_full();
    test_err_frame_too_large();
    test_err_remote_busy();
    test_err_max_retry();

    printf("\n%sALL ERROR CODE TESTS PASSED!%s\n", COL_GREEN, COL_RESET);
    return 0;
}
