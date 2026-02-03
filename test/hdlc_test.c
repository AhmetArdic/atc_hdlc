#include "hdlc.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- ANSI Colors for Verbose Output ---
#define COL_RESET "\033[0m"
#define COL_GREEN "\033[32m"
#define COL_RED "\033[31m"
#define COL_CYAN "\033[36m"
#define COL_YELLOW "\033[33m"

// --- Mocking & Utilities ---
static hdlc_u8 tx_buffer[4096];
static int tx_len = 0;

void print_hexdump(const char *label, const hdlc_u8 *data, int len) {
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

void mock_tx_cb(void *user_data, hdlc_u8 byte) {
  if (tx_len < sizeof(tx_buffer)) {
    tx_buffer[tx_len++] = byte;
  }
}

// Global hook for RX verification
static hdlc_frame_t last_rx_frame;
static int rx_callback_count = 0;

void mock_rx_cb(void *user_data, const hdlc_frame_t *frame) {
  rx_callback_count++;
  memcpy(&last_rx_frame, frame, sizeof(hdlc_frame_t));

  printf("   %s[RX EVENT] Frame Received!%s\n", COL_GREEN, COL_RESET);
  printf("   Type: %d, Addr: %02X, Ctrl: %02X, Payload Len: %d\n", frame->type,
         frame->address, frame->control.value, frame->payload_len);
  if (frame->payload_len > 0) {
    printf("   Payload: ");
    for (int i = 0; i < frame->payload_len; i++)
      printf("%02X ", frame->payload[i]);
    printf("\n");
  }
}

void reset_test() {
  tx_len = 0;
  rx_callback_count = 0;
  memset(tx_buffer, 0, sizeof(tx_buffer));
  memset(&last_rx_frame, 0, sizeof(hdlc_frame_t));
}

// --- Test Helpers ---

void assert_pass(const char *test_name) {
  printf("%s[PASS] %s%s\n\n", COL_GREEN, test_name, COL_RESET);
}

void assert_fail(const char *test_name, const char *reason) {
  printf("%s[FAIL] %s: %s%s\n", COL_RED, test_name, reason, COL_RESET);
  exit(1);
}

// --- Tests ---

void test_basic_frame() {
  printf("========================================\n");
  printf("TEST: Basic Frame (I-Frame)\n");
  printf("========================================\n");
  hdlc_context_t ctx;
  hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  hdlc_frame_t frame_out = {
      .address = 0xFF, .control.value = 0x00, .payload_len = 4};
  memcpy(frame_out.payload, "TEST", 4);

  hdlc_send_frame(&ctx, &frame_out);
  print_hexdump("TX Buffer", tx_buffer, tx_len);

  printf("Feeding back bytes:\n");
  for (int i = 0; i < tx_len; i++) {
    hdlc_input_byte(&ctx, tx_buffer[i]);
  }

  if (rx_callback_count == 1 && memcmp(last_rx_frame.payload, "TEST", 4) == 0) {
    assert_pass("Basic Frame");
  } else {
    assert_fail("Basic Frame", "Frame not received correctly");
  }
}

void test_empty_payload() {
  printf("========================================\n");
  printf("TEST: Empty Payload (Header only)\n");
  printf("========================================\n");
  hdlc_context_t ctx;
  hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  hdlc_frame_t frame_out = {.address = 0xAA,
                            .control.value = 0x11, // Some random control
                            .payload_len = 0};

  hdlc_send_frame(&ctx, &frame_out);
  print_hexdump("TX Buffer", tx_buffer, tx_len);

  for (int i = 0; i < tx_len; i++)
    hdlc_input_byte(&ctx, tx_buffer[i]);

  if (rx_callback_count == 1 && last_rx_frame.payload_len == 0 &&
      last_rx_frame.address == 0xAA) {
    assert_pass("Empty Payload");
  } else {
    assert_fail("Empty Payload", "Failed to receive empty payload frame");
  }
}

void test_byte_stuffing_heavy() {
  printf("========================================\n");
  printf("TEST: Heavy Byte Stuffing\n");
  printf("========================================\n");
  hdlc_context_t ctx;
  hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  // Data with many flags and escapes
  hdlc_u8 tricky_data[] = {0x7E, 0x7E, 0x7D, 0x7D, 0x7E, 0x00};
  hdlc_frame_t frame_out = {.address = 0x01,
                            .control.value = 0x03,
                            .payload_len = sizeof(tricky_data)};
  memcpy(frame_out.payload, tricky_data, sizeof(tricky_data));

  hdlc_send_frame(&ctx, &frame_out);
  print_hexdump("TX Buffer (Stuffed)", tx_buffer, tx_len);

  // Verify manually that buffer is larger than raw data
  // Raw: 1(Addr)+1(Ctrl)+6(Pay)+2(CRC) = 10 bytes (+2 Flags = 12)
  // Escapes: 7E->2, 7E->2, 7D->2, 7D->2, 7E->2. Total +5 bytes?
  printf("Checking escaping logic...\n");
  int escapes = 0;
  for (int i = 0; i < tx_len; i++)
    if (tx_buffer[i] == 0x7D)
      escapes++;
  printf("Total raw escapes found: %d\n", escapes);

  for (int i = 0; i < tx_len; i++)
    hdlc_input_byte(&ctx, tx_buffer[i]);

  if (rx_callback_count == 1 &&
      memcmp(last_rx_frame.payload, tricky_data, sizeof(tricky_data)) == 0) {
    assert_pass("Heavy Stuffing");
  } else {
    assert_fail("Heavy Stuffing", "Payload mismatch after unstuffing");
  }
}

void test_garbage_noise() {
  printf("========================================\n");
  printf("TEST: Garbage / Noise Rejection\n");
  printf("========================================\n");
  hdlc_context_t ctx;
  hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  // 1. Generate a valid frame
  hdlc_frame_t frame_out = {
      .address = 0x05, .control.value = 0x05, .payload_len = 1};
  frame_out.payload[0] = 0xCC;
  hdlc_send_frame(&ctx, &frame_out); // Fills tx_buffer

  // 2. Inject noise BEFORE the frame
  hdlc_u8 noise[] = {0x00, 0x12, 0x34, 0x56, 0xAA, 0xBB};
  printf("Injecting %ld bytes of noise before frame...\n", sizeof(noise));
  for (size_t i = 0; i < sizeof(noise); i++)
    hdlc_input_byte(&ctx, noise[i]);

  // 3. Inject the valid frame
  printf("Injecting valid frame...\n");
  for (int i = 0; i < tx_len; i++)
    hdlc_input_byte(&ctx, tx_buffer[i]);

  // 4. Inject noise AFTER the frame
  printf("Injecting noise after frame...\n");
  for (size_t i = 0; i < sizeof(noise); i++)
    hdlc_input_byte(&ctx, noise[i]);

  // We expect exactly 1 frame
  if (rx_callback_count == 1) {
    assert_pass("Garbage Noise");
  } else {
    assert_fail("Garbage Noise",
                "Noise caused valid frame drop or phantom frame");
  }
}

void test_consecutive_flags() {
  printf("========================================\n");
  printf("TEST: Consecutive Flags (Inter-frame fill)\n");
  printf("========================================\n");
  hdlc_context_t ctx;
  hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  hdlc_frame_t frame_out = {
      .address = 0x10, .control.value = 0x10, .payload_len = 0};
  hdlc_send_frame(&ctx, &frame_out);
  // tx_buffer has valid frame.
  // Format: 7E ... 7E

  // Feed: 7E 7E 7E 7E [Frame] 7E 7E 7E
  printf("Feeding: 7E 7E 7E 7E\n");
  hdlc_input_byte(&ctx, 0x7E); // Flag
  hdlc_input_byte(&ctx, 0x7E); // Flag
  hdlc_input_byte(&ctx, 0x7E); // Flag
  hdlc_input_byte(&ctx, 0x7E); // Flag

  printf("Feeding Frame...\n");
  // Skip first byte of tx_buffer (it's 7E, already sent multiple) or valid to
  // send again? HDLC says adjacent flags are valid. send full buffer.
  for (int i = 0; i < tx_len; i++)
    hdlc_input_byte(&ctx, tx_buffer[i]);

  printf("Feeding: 7E 7E\n");
  hdlc_input_byte(&ctx, 0x7E);
  hdlc_input_byte(&ctx, 0x7E);

  if (rx_callback_count == 1) {
    assert_pass("Consecutive Flags");
  } else {
    assert_fail("Consecutive Flags", "Multiple flags caused parsing error");
  }
}

void test_min_size_rejection() {
  printf("========================================\n");
  printf("TEST: Minimum Size Rejection (<4 bytes)\n");
  printf("========================================\n");
  hdlc_context_t ctx;
  hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  // Construct a Tiny Frame: 7E 01 02 7E (Addr, Ctrl, No CRC) -> Size 2
  // (invalid)
  hdlc_u8 tiny[] = {0x7E, 0x01, 0x02, 0x7E};

  printf("Feeding Tiny Frame (2 bytes payload inside flags)...\n");
  for (size_t i = 0; i < sizeof(tiny); i++)
    hdlc_input_byte(&ctx, tiny[i]);

  // Construct Frame with CRC but weird: 7E 01 02 03 7E -> Size 3 (Addr, Ctrl, 1
  // byte CRC?) -> Invalid
  hdlc_u8 tiny2[] = {0x7E, 0x01, 0x02, 0x03, 0x7E};
  printf("Feeding Too Short Frame (3 bytes payload inside flags)...\n");
  for (size_t i = 0; i < sizeof(tiny2); i++)
    hdlc_input_byte(&ctx, tiny2[i]);

  if (rx_callback_count == 0) {
    assert_pass("Min Size Rejection");
  } else {
    assert_fail("Min Size Rejection", "Short frames were accepted!");
  }
}

void test_aborted_frame() {
  printf("========================================\n");
  printf("TEST: Aborted / Interrupted Frame\n");
  printf("========================================\n");
  hdlc_context_t ctx;
  hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  // Start a frame, write some data, then hit Flag immediately (Frame
  // Abort/Resync)
  printf("Start Frame (7E)...\n");
  hdlc_input_byte(&ctx, 0x7E);
  hdlc_input_byte(&ctx, 0xFF); // Addr
  hdlc_input_byte(&ctx, 0x00); // Ctrl
  hdlc_input_byte(&ctx, 0xAA); // Data
  hdlc_input_byte(&ctx, 0xBB); // Data

  // Premature Flag!
  printf("Premature Flag (7E) -> Should reset\n");
  hdlc_input_byte(&ctx, 0x7E);

  // Now send a REAL valid frame immediately after
  hdlc_frame_t frame_out = {
      .address = 0x01, .control.value = 0x11, .payload_len = 0};
  hdlc_send_frame(&ctx, &frame_out);

  // Send valid frame (skipping first 7E since we just sent one? No, safe to
  // send all)
  for (int i = 0; i < tx_len; i++)
    hdlc_input_byte(&ctx, tx_buffer[i]);

  if (rx_callback_count == 1 && last_rx_frame.address == 0x01) {
    assert_pass("Aborted Frame");
  } else {
    assert_fail("Aborted Frame", "Recovery from aborted frame failed");
  }
}

// --- Restored & New Tests ---

void test_crc_error_injection() {
  printf("========================================\n");
  printf("TEST: CRC Error Injection (Single Bit)\n");
  printf("========================================\n");
  hdlc_context_t ctx;
  hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  hdlc_frame_t frame_out = {
      .address = 0xFF, .control.value = 0x00, .payload_len = 4};
  memcpy(frame_out.payload, "DATA", 4);
  hdlc_send_frame(&ctx, &frame_out);

  // Corrupt the last byte (part of CRC)
  tx_buffer[tx_len - 2] ^= 0x01;

  print_hexdump("TX Buffer (Corrupted)", tx_buffer, tx_len);
  for (int i = 0; i < tx_len; i++)
    hdlc_input_byte(&ctx, tx_buffer[i]);

  if (rx_callback_count == 0 && ctx.stats_crc_errors == 1) {
    assert_pass("CRC Error Injection");
  } else {
    assert_fail("CRC Error Injection", "Bad CRC was accepted or not counted");
  }
}

void test_mtu_overflow() {
  printf("========================================\n");
  printf("TEST: MTU Overflow Safety\n");
  printf("========================================\n");
  hdlc_context_t ctx;
  hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  printf("Feeding Start Flag...\n");
  hdlc_input_byte(&ctx, 0x7E); // Start

  printf("Feeding %d bytes (MTU + 50)...\n", HDLC_MAX_MTU + 50);
  // Feed more than MTU
  for (int i = 0; i < HDLC_MAX_MTU + 50; i++) {
    hdlc_input_byte(&ctx, 0xAA);
  }

  hdlc_input_byte(&ctx, 0x7E); // End Flag

  if (rx_callback_count == 0) {
    assert_pass("MTU Overflow");
  } else {
    assert_fail("MTU Overflow", "Overflow frame triggered callback");
  }
}

void test_streaming_api() {
  printf("========================================\n");
  printf("TEST: Streaming API (Zero-Copy)\n");
  printf("========================================\n");
  hdlc_context_t ctx;
  hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  hdlc_send_packet_start(&ctx);
  hdlc_send_packet_byte(&ctx, 0xAA); // Addr
  hdlc_send_packet_byte(&ctx, 0xBB); // Ctrl
  hdlc_send_packet_byte(&ctx, 0x7E); // Data (Stuffing needed)
  hdlc_send_packet_byte(&ctx, 0x7D); // Data (Stuffing needed)
  hdlc_send_packet_end(&ctx);

  print_hexdump("TX Buffer (Streamed)", tx_buffer, tx_len);

  for (int i = 0; i < tx_len; i++)
    hdlc_input_byte(&ctx, tx_buffer[i]);

  if (rx_callback_count == 1 && last_rx_frame.payload_len == 2) {
    // Payload should be 7E 7D
    if (last_rx_frame.payload[0] == 0x7E && last_rx_frame.payload[1] == 0x7D) {
      assert_pass("Streaming API");
    } else {
      assert_fail("Streaming API", "Payload content mismatch");
    }
  } else {
    assert_fail("Streaming API", "Frame not received");
  }
}

void test_fragmented_delivery() {
  printf("========================================\n");
  printf("TEST: Fragmented / Slow Delivery\n");
  printf("========================================\n");
  hdlc_context_t ctx;
  hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  hdlc_frame_t frame_out = {
      .address = 0x99, .control.value = 0x88, .payload_len = 10};
  memcpy(frame_out.payload, "0123456789", 10);
  hdlc_send_frame(&ctx, &frame_out);

  // Simulate UART getting bytes 1 by 1 with delays (conceptually)
  // or chunks.
  printf("Feeding bytes 1 by 1...\n");
  for (int i = 0; i < tx_len; i++) {
    hdlc_input_byte(&ctx, tx_buffer[i]);
  }

  if (rx_callback_count != 1)
    assert_fail("Fragmented 1-by-1", "Failed simple loop");

  // Reset and try CHUNKS
  reset_test();
  hdlc_send_frame(&ctx, &frame_out);
  rx_callback_count = 0;

  printf("Feeding in 3 chunks...\n");
  int chunk1 = 3;
  int chunk2 = 5;
  int chunk3 = tx_len - chunk1 - chunk2;

  printf("Chunk 1 (%d bytes)\n", chunk1);
  for (int i = 0; i < chunk1; i++)
    hdlc_input_byte(&ctx, tx_buffer[i]);

  printf("Chunk 2 (%d bytes)\n", chunk2);
  for (int i = chunk1; i < chunk1 + chunk2; i++)
    hdlc_input_byte(&ctx, tx_buffer[i]);

  printf("Chunk 3 (%d bytes)\n", chunk3);
  for (int i = chunk1 + chunk2; i < tx_len; i++)
    hdlc_input_byte(&ctx, tx_buffer[i]);

  if (rx_callback_count == 1) {
    assert_pass("Fragmented Delivery");
  } else {
    assert_fail("Fragmented Delivery", "Chunked delivery failed");
  }
}

int main() {
  printf("\n%sSTARTING COMPREHENSIVE HDLC TEST SUITE%s\n", COL_YELLOW,
         COL_RESET);
  printf("----------------------------------------\n\n");

  test_basic_frame();
  test_empty_payload();
  test_byte_stuffing_heavy();
  test_garbage_noise();
  test_consecutive_flags();
  test_min_size_rejection();
  test_aborted_frame();

  // Restored & New Tests
  test_crc_error_injection();
  test_mtu_overflow();
  test_streaming_api();
  test_fragmented_delivery();

  printf("\n%sALL TESTS PASSED SUCCESSFULLY!%s\n", COL_GREEN, COL_RESET);
  return 0;
}
