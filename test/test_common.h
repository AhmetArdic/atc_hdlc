#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "../inc/hdlc.h" // For atc_hdlc_u8

// --- ANSI Colors ---
#define COL_RESET "\033[0m"
#define COL_GREEN "\033[32m"
#define COL_RED "\033[31m"
#define COL_CYAN "\033[36m"
#define COL_YELLOW "\033[33m"

// --- Shared Mock State ---
extern atc_hdlc_u8 mock_output_buffer[16384];
extern atc_hdlc_u8 mock_rx_buffer[16384];
extern int mock_output_len;
extern atc_hdlc_frame_t last_received_frame;
extern int on_frame_call_count;

// --- Shared Mock Callbacks ---
void mock_output_byte_cb(atc_hdlc_u8 byte, atc_hdlc_bool flush, void *user_data);
void mock_on_frame_cb(const atc_hdlc_frame_t *frame, void *user_data);

// --- Helpers ---
void setup_test_context(atc_hdlc_context_t *ctx);
void reset_test_state(void);
void print_hexdump(const char *label, const atc_hdlc_u8 *data, int len);
void test_pass(const char *test_name);
void test_fail(const char *test_name, const char *reason);

#endif // TEST_COMMON_H
