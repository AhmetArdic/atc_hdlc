#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "../inc/hdlc.h"

/* --- ANSI Colors --- */
#define COL_RESET  "\033[0m"
#define COL_GREEN  "\033[32m"
#define COL_RED    "\033[31m"
#define COL_CYAN   "\033[36m"
#define COL_YELLOW "\033[33m"

/* --- Shared mock state --- */
extern atc_hdlc_u8    mock_output_buffer[16384];
extern atc_hdlc_u8    mock_rx_buffer[16384];
extern int            mock_output_len;
extern int            on_data_call_count;
extern atc_hdlc_u8    last_data_payload[16384];
extern atc_hdlc_u16   last_data_len;

/* --- Event mock state --- */
extern int                 on_event_call_count;
extern atc_hdlc_event_t    last_event;

/* --- Timer mock state ---
 * Tracks how many times each timer callback was invoked and with what duration.
 * Allows unit tests to verify that the library starts/stops T1/T2/T3 at the
 * correct protocol moments without needing a real OS timer.
 */
extern int           mock_t1_start_count;
extern int           mock_t1_stop_count;
extern atc_hdlc_u32  mock_t1_last_ms;
extern int           mock_t2_start_count;
extern int           mock_t2_stop_count;
extern atc_hdlc_u32  mock_t2_last_ms;

/* --- Mock platform callbacks --- */
int  mock_send_cb     (atc_hdlc_u8 byte, atc_hdlc_bool flush, void *user_ctx);
void mock_on_data_cb  (const atc_hdlc_u8 *payload, atc_hdlc_u16 len, void *user_ctx);
void mock_on_event_cb (atc_hdlc_event_t event, void *user_ctx);

void mock_t1_start_cb (atc_hdlc_u32 ms, void *user_ctx);
void mock_t1_stop_cb  (void *user_ctx);
void mock_t2_start_cb (atc_hdlc_u32 ms, void *user_ctx);
void mock_t2_stop_cb  (void *user_ctx);

/* --- Context setup variants ---
 *
 * setup_test_context()         — default: window=1, timers=mock
 * setup_test_context_w()       — configurable window size, timers=mock
 * setup_test_context_no_tw()   — no TX window (tests ATC_HDLC_ERR_NO_BUFFER path)
 */
void setup_test_context(atc_hdlc_context_t *ctx);
void setup_test_context_w(atc_hdlc_context_t *ctx, atc_hdlc_u8 window_size);
void setup_test_context_no_tw(atc_hdlc_context_t *ctx);

/* --- State reset --- */
void reset_test_state(void);

/* --- Helpers --- */
void print_hexdump(const char *label, const atc_hdlc_u8 *data, int len);
void test_pass(const char *test_name);
void test_fail(const char *test_name, const char *reason);

#endif /* TEST_COMMON_H */
