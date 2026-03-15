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

/* --- Mock platform send callback (byte-based) --- */
int mock_send_cb(atc_hdlc_u8 byte, atc_hdlc_bool flush, void *user_ctx);

/* --- Mock platform on_data callback --- */
void mock_on_data_cb(const atc_hdlc_u8 *payload, atc_hdlc_u16 len, void *user_ctx);

/* --- Helpers --- */
void setup_test_context(atc_hdlc_context_t *ctx);
void reset_test_state(void);
void print_hexdump(const char *label, const atc_hdlc_u8 *data, int len);
void test_pass(const char *test_name);
void test_fail(const char *test_name, const char *reason);

#endif /* TEST_COMMON_H */
