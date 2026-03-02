#include "test_common.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// --- Shared Mock State ---
atc_hdlc_u8 mock_output_buffer[16384];
atc_hdlc_u8 mock_rx_buffer[16384];
int mock_output_len = 0;
atc_hdlc_frame_t last_received_frame;
int on_frame_call_count = 0;

// --- Shared Mock Callbacks ---
void mock_output_byte_cb(atc_hdlc_u8 byte, atc_hdlc_bool flush, void *user_data) {
  (void)user_data;
  (void)flush;
  if (mock_output_len < (int)sizeof(mock_output_buffer)) {
    mock_output_buffer[mock_output_len++] = byte;
  }
}

void mock_on_frame_cb(const atc_hdlc_frame_t *frame, void *user_data) {
  (void)user_data;
  on_frame_call_count++;
  memcpy(&last_received_frame, frame, sizeof(atc_hdlc_frame_t));

  printf("   %s[ON FRAME EVENT] Frame Received!%s\n", COL_GREEN, COL_RESET);
  printf("   Type: %d, Addr: %02X, Ctrl: %02X, Information Len: %d\n",
         frame->type, frame->address, frame->control.value,
         frame->information_len);
  if (frame->information_len > 0) {
    printf("   Information: ");
    for (int i = 0; i < frame->information_len; i++)
      printf("%02X ", frame->information[i]);
    printf("\n");
  }
}

// --- Helpers ---
void setup_test_context(atc_hdlc_context_t *ctx) {
    static atc_hdlc_u8 static_retx_buf[1024]; 
    
    // Also re-init mocks
    reset_test_state();
    
    if (ctx) {
        atc_hdlc_init(ctx, mock_rx_buffer, sizeof(mock_rx_buffer), 
                      static_retx_buf, sizeof(static_retx_buf), 
                      HDLC_DEFAULT_RETRANSMIT_TIMEOUT, 
                      HDLC_DEFAULT_ACK_DELAY_TIMEOUT,
                      HDLC_DEFAULT_WINDOW_SIZE, 
                      3,
                      mock_output_byte_cb, 
                      mock_on_frame_cb, 
                      NULL, NULL);
    }
}

void reset_test_state(void) {
  mock_output_len = 0;
  on_frame_call_count = 0;
  memset(mock_output_buffer, 0, sizeof(mock_output_buffer));
  memset(&last_received_frame, 0, sizeof(atc_hdlc_frame_t));
}

// --- Helpers ---
void print_hexdump(const char *label, const atc_hdlc_u8 *data, int len) {
  printf("%s%s (%d bytes):%s ", COL_CYAN, label, len, COL_RESET);
  for (int i = 0; i < len; i++) {
    printf("%02X ", data[i]);
  }
  printf(" | ");
  for (int i = 0; i < len; i++) {
    printf("%c", isprint(data[i]) ? data[i] : '.');
  }
  printf("\n");
}

void test_pass(const char *test_name) {
  printf("%s[PASS] %s%s\n\n", COL_GREEN, test_name, COL_RESET);
}

void test_fail(const char *test_name, const char *reason) {
  printf("%s[FAIL] %s: %s%s\n", COL_RED, test_name, reason, COL_RESET);
  exit(1);
}
