#include "test_common.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* --- Shared mock state --- */
atc_hdlc_u8  mock_output_buffer[16384];
atc_hdlc_u8  mock_rx_buffer[16384];
int          mock_output_len   = 0;
int          on_data_call_count = 0;
atc_hdlc_u8  last_data_payload[16384];
atc_hdlc_u16 last_data_len = 0;

/* --- Default config used by setup_test_context() --- */
static const atc_hdlc_config_t s_default_config = {
    .mode           = ATC_HDLC_MODE_ABM,
    .address        = 0x01,
    .window_size    = ATC_HDLC_DEFAULT_WINDOW_SIZE,
    .max_frame_size = 1024,
    .max_retries    = 3,
    .t1_ms          = ATC_HDLC_DEFAULT_RETRANSMIT_TIMEOUT,
    .t2_ms          = ATC_HDLC_DEFAULT_ACK_DELAY_TIMEOUT,
    .t3_ms          = 30000,
    .use_extended   = false,
};

/* Static retransmit window backing storage (window_size = 1 default) */
static atc_hdlc_u8  s_tx_slots[1 * 1024];
static atc_hdlc_u32 s_tx_slot_lens[1];
static atc_hdlc_u8  s_tx_seq_to_slot[1];

static atc_hdlc_tx_window_t s_tx_window = {
    .slots         = s_tx_slots,
    .slot_lens     = s_tx_slot_lens,
    .seq_to_slot   = s_tx_seq_to_slot,
    .slot_capacity = 1024,
    .slot_count    = 1,
};

static atc_hdlc_rx_buffer_t s_rx_buf = {
    .buffer   = mock_rx_buffer,
    .capacity = sizeof(mock_rx_buffer),
};

/* --- Mock platform send callback --- */
int mock_send_cb(atc_hdlc_u8 byte, atc_hdlc_bool flush, void *user_ctx) {
    (void)user_ctx;
    (void)flush;
    if (mock_output_len < (int)sizeof(mock_output_buffer)) {
        mock_output_buffer[mock_output_len++] = byte;
    }
    return 0;
}

/* --- Mock platform on_data callback --- */
void mock_on_data_cb(const atc_hdlc_u8 *payload, atc_hdlc_u16 len, void *user_ctx) {
    (void)user_ctx;
    on_data_call_count++;
    last_data_len = len;
    if (len > 0 && payload != NULL) {
        memcpy(last_data_payload, payload, len < sizeof(last_data_payload) ? len : sizeof(last_data_payload));
    }
    printf("   %s[ON DATA] %u bytes received%s\n", COL_GREEN, len, COL_RESET);
}

static const atc_hdlc_platform_t s_default_platform = {
    .send     = mock_send_cb,
    .on_data  = mock_on_data_cb,
    .on_event = NULL,
    .user_ctx = NULL,
};

/* --- Helpers --- */
void setup_test_context(atc_hdlc_context_t *ctx) {
    reset_test_state();
    if (ctx) {
        atc_hdlc_init(ctx,
                      &s_default_config,
                      &s_default_platform,
                      &s_tx_window,
                      &s_rx_buf);
    }
}

void reset_test_state(void) {
    mock_output_len    = 0;
    on_data_call_count = 0;
    last_data_len      = 0;
    memset(mock_output_buffer, 0, sizeof(mock_output_buffer));
    memset(last_data_payload,  0, sizeof(last_data_payload));
    memset(mock_rx_buffer,     0, sizeof(mock_rx_buffer));
}

void print_hexdump(const char *label, const atc_hdlc_u8 *data, int len) {
    printf("%s%s (%d bytes):%s ", COL_CYAN, label, len, COL_RESET);
    for (int i = 0; i < len; i++) printf("%02X ", data[i]);
    printf(" | ");
    for (int i = 0; i < len; i++) printf("%c", isprint(data[i]) ? data[i] : '.');
    printf("\n");
}

void test_pass(const char *test_name) {
    printf("%s[PASS] %s%s\n\n", COL_GREEN, test_name, COL_RESET);
}

void test_fail(const char *test_name, const char *reason) {
    printf("%s[FAIL] %s: %s%s\n", COL_RED, test_name, reason, COL_RESET);
    exit(1);
}
