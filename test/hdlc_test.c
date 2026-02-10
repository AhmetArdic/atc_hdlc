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
static atc_hdlc_u8 tx_buffer[4096];
static int tx_len = 0;

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

void mock_tx_cb(atc_hdlc_u8 byte, void *user_data) {
  (void)user_data;
  if (tx_len < sizeof(tx_buffer)) {
    tx_buffer[tx_len++] = byte;
  }
}

// Global hook for RX verification
static atc_hdlc_frame_t last_rx_frame;
static int rx_callback_count = 0;

void mock_rx_cb(const atc_hdlc_frame_t *frame, void *user_data) {
  (void)user_data;
  rx_callback_count++;
  memcpy(&last_rx_frame, frame, sizeof(atc_hdlc_frame_t));

  printf("   %s[RX EVENT] Frame Received!%s\n", COL_GREEN, COL_RESET);
  printf("   Type: %d, Addr: %02X, Ctrl: %02X, Information Len: %d\n", frame->type,
         frame->address, frame->control.value, frame->information_len);
  if (frame->information_len > 0) {
    printf("   Information: ");
    for (int i = 0; i < frame->information_len; i++)
      printf("%02X ", frame->information[i]);
    printf("\n");
  }
}

void reset_test() {
  tx_len = 0;
  rx_callback_count = 0;
  memset(tx_buffer, 0, sizeof(tx_buffer));
  memset(&last_rx_frame, 0, sizeof(atc_hdlc_frame_t));
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
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  atc_hdlc_frame_t frame_out = {
      .address = 0xFF, .control.value = 0x00, .information_len = 4};
  memcpy(frame_out.information, "TEST", 4);

  atc_hdlc_send_frame(&ctx, &frame_out);
  print_hexdump("TX Buffer", tx_buffer, tx_len);

  printf("Feeding back bytes:\n");
  for (int i = 0; i < tx_len; i++) {
    atc_hdlc_input_byte(&ctx, tx_buffer[i]);
  }

  if (rx_callback_count == 1 && memcmp(last_rx_frame.information, "TEST", 4) == 0) {
    assert_pass("Basic Frame");
  } else {
    assert_fail("Basic Frame", "Frame not received correctly");
  }
}

void test_empty_information() {
  printf("========================================\n");
  printf("TEST: Empty Information (Header only)\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  atc_hdlc_frame_t frame_out = {.address = 0xAA,
                            .control.value = 0x11, // Some random control
                            .information_len = 0};

  atc_hdlc_send_frame(&ctx, &frame_out);
  print_hexdump("TX Buffer", tx_buffer, tx_len);

  for (int i = 0; i < tx_len; i++)
    atc_hdlc_input_byte(&ctx, tx_buffer[i]);

  if (rx_callback_count == 1 && last_rx_frame.information_len == 0 &&
      last_rx_frame.address == 0xAA) {
    assert_pass("Empty Information");
  } else {
    assert_fail("Empty Information", "Failed to receive empty information frame");
  }
}

void test_byte_stuffing_heavy() {
  printf("========================================\n");
  printf("TEST: Heavy Byte Stuffing\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  // Data with many flags and escapes
  atc_hdlc_u8 tricky_data[] = {0x7E, 0x7E, 0x7D, 0x7D, 0x7E, 0x00};
  atc_hdlc_frame_t frame_out = {.address = 0x01,
                            .control.value = 0x03,
                            .information_len = sizeof(tricky_data)};
  memcpy(frame_out.information, tricky_data, sizeof(tricky_data));

  atc_hdlc_send_frame(&ctx, &frame_out);
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
    atc_hdlc_input_byte(&ctx, tx_buffer[i]);

  if (rx_callback_count == 1 &&
      memcmp(last_rx_frame.information, tricky_data, sizeof(tricky_data)) == 0) {
    assert_pass("Heavy Stuffing");
  } else {
    assert_fail("Heavy Stuffing", "Information mismatch after unstuffing");
  }
}

void test_garbage_noise() {
  printf("========================================\n");
  printf("TEST: Garbage / Noise Rejection\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  // 1. Generate a valid frame
  atc_hdlc_frame_t frame_out = {
      .address = 0x05, .control.value = 0x05, .information_len = 1};
  frame_out.information[0] = 0xCC;
  atc_hdlc_send_frame(&ctx, &frame_out); // Fills tx_buffer

  // 2. Inject noise BEFORE the frame
  atc_hdlc_u8 noise[] = {0x00, 0x12, 0x34, 0x56, 0xAA, 0xBB};
  printf("Injecting %ld bytes of noise before frame...\n", sizeof(noise));
  for (size_t i = 0; i < sizeof(noise); i++)
    atc_hdlc_input_byte(&ctx, noise[i]);

  // 3. Inject the valid frame
  printf("Injecting valid frame...\n");
  for (int i = 0; i < tx_len; i++)
    atc_hdlc_input_byte(&ctx, tx_buffer[i]);

  // 4. Inject noise AFTER the frame
  printf("Injecting noise after frame...\n");
  for (size_t i = 0; i < sizeof(noise); i++)
    atc_hdlc_input_byte(&ctx, noise[i]);

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
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  atc_hdlc_frame_t frame_out = {
      .address = 0x10, .control.value = 0x10, .information_len = 0};
  atc_hdlc_send_frame(&ctx, &frame_out);
  // tx_buffer has valid frame.
  // Format: 7E ... 7E

  // Feed: 7E 7E 7E 7E [Frame] 7E 7E 7E
  printf("Feeding: 7E 7E 7E 7E\n");
  atc_hdlc_input_byte(&ctx, 0x7E); // Flag
  atc_hdlc_input_byte(&ctx, 0x7E); // Flag
  atc_hdlc_input_byte(&ctx, 0x7E); // Flag
  atc_hdlc_input_byte(&ctx, 0x7E); // Flag

  printf("Feeding Frame...\n");
  // Skip first byte of tx_buffer (it's 7E, already sent multiple) or valid to
  // send again? HDLC says adjacent flags are valid. send full buffer.
  for (int i = 0; i < tx_len; i++)
    atc_hdlc_input_byte(&ctx, tx_buffer[i]);

  printf("Feeding: 7E 7E\n");
  atc_hdlc_input_byte(&ctx, 0x7E);
  atc_hdlc_input_byte(&ctx, 0x7E);

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
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  // Construct a Tiny Frame: 7E 01 02 7E (Addr, Ctrl, No CRC) -> Size 2
  // (invalid)
  atc_hdlc_u8 tiny[] = {0x7E, 0x01, 0x02, 0x7E};

  printf("Feeding Tiny Frame (2 bytes information inside flags)...\n");
  for (size_t i = 0; i < sizeof(tiny); i++)
    atc_hdlc_input_byte(&ctx, tiny[i]);

  // Construct Frame with CRC but weird: 7E 01 02 03 7E -> Size 3 (Addr, Ctrl, 1
  // byte CRC?) -> Invalid
  atc_hdlc_u8 tiny2[] = {0x7E, 0x01, 0x02, 0x03, 0x7E};
  printf("Feeding Too Short Frame (3 bytes information inside flags)...\n");
  for (size_t i = 0; i < sizeof(tiny2); i++)
    atc_hdlc_input_byte(&ctx, tiny2[i]);

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
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  // Start a frame, write some data, then hit Flag immediately (Frame
  // Abort/Resync)
  printf("Start Frame (7E)...\n");
  atc_hdlc_input_byte(&ctx, 0x7E);
  atc_hdlc_input_byte(&ctx, 0xFF); // Addr
  atc_hdlc_input_byte(&ctx, 0x00); // Ctrl
  atc_hdlc_input_byte(&ctx, 0xAA); // Data
  atc_hdlc_input_byte(&ctx, 0xBB); // Data

  // Premature Flag!
  printf("Premature Flag (7E) -> Should reset\n");
  atc_hdlc_input_byte(&ctx, 0x7E);

  // Now send a REAL valid frame immediately after
  atc_hdlc_frame_t frame_out = {
      .address = 0x01, .control.value = 0x11, .information_len = 0};
  atc_hdlc_send_frame(&ctx, &frame_out);

  // Send valid frame (skipping first 7E since we just sent one? No, safe to
  // send all)
  for (int i = 0; i < tx_len; i++)
    atc_hdlc_input_byte(&ctx, tx_buffer[i]);

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
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  atc_hdlc_frame_t frame_out = {
      .address = 0xFF, .control.value = 0x00, .information_len = 4};
  memcpy(frame_out.information, "DATA", 4);
  atc_hdlc_send_frame(&ctx, &frame_out);

  // Corrupt the last byte (part of CRC)
  tx_buffer[tx_len - 2] ^= 0x01;

  print_hexdump("TX Buffer (Corrupted)", tx_buffer, tx_len);
  for (int i = 0; i < tx_len; i++)
    atc_hdlc_input_byte(&ctx, tx_buffer[i]);

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
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  printf("Feeding Start Flag...\n");
  atc_hdlc_input_byte(&ctx, 0x7E); // Start

  printf("Feeding %d bytes (MTU + 50)...\n", HDLC_MAX_INFORMATION_LEN + 50);
  // Feed more than MTU
  for (int i = 0; i < HDLC_MAX_INFORMATION_LEN + 50; i++) {
    atc_hdlc_input_byte(&ctx, 0xAA);
  }

  atc_hdlc_input_byte(&ctx, 0x7E); // End Flag

  if (rx_callback_count == 0) {
    assert_pass("MTU Overflow");
  } else {
    assert_fail("MTU Overflow", "Overflow frame triggered callback");
  }
}

void test_mtu() {
  printf("========================================\n");
  printf("TEST: MTU\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  printf("Feeding Start...\n");
  atc_hdlc_send_packet_start(&ctx, 0xAA, 0xBB); // Addr, Ctrl

  printf("Feeding %d bytes (MTU)...\n", HDLC_MAX_INFORMATION_LEN);
  // Feed more than MTU
  for (int i = 0; i < HDLC_MAX_INFORMATION_LEN; i++) {
      atc_hdlc_send_packet_information_byte(&ctx, 0xAA);
  }
  atc_hdlc_send_packet_end(&ctx); // End Flag

  print_hexdump("TX Buffer (Streamed)", tx_buffer, tx_len);

  for (int i = 0; i < tx_len; i++)
      atc_hdlc_input_byte(&ctx, tx_buffer[i]);

  if (rx_callback_count == 1 && last_rx_frame.information_len == HDLC_MAX_INFORMATION_LEN) {
    assert_pass("MTU");
  } else {
    assert_fail("MTU", "MTU error");
  }
}

void test_streaming_api() {
  printf("========================================\n");
  printf("TEST: Streaming API (Zero-Copy)\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  atc_hdlc_send_packet_start(&ctx, 0xAA, 0xBB); // Addr, Ctrl
  atc_hdlc_send_packet_information_byte(&ctx, 0x7E); // Data (Stuffing needed)
  atc_hdlc_send_packet_information_byte(&ctx, 0x7D); // Data (Stuffing needed)
  atc_hdlc_send_packet_end(&ctx);

  print_hexdump("TX Buffer (Streamed)", tx_buffer, tx_len);

  for (int i = 0; i < tx_len; i++)
      atc_hdlc_input_byte(&ctx, tx_buffer[i]);

  if (rx_callback_count == 1 && last_rx_frame.information_len == 2) {
      // Information should be 7E 7D
      if (last_rx_frame.information[0] == 0x7E && last_rx_frame.information[1] == 0x7D) {
          assert_pass("Streaming API");
      } else {
          assert_fail("Streaming API", "Information content mismatch");
      }
  } else {
      assert_fail("Streaming API", "Frame not received");
  }

  reset_test();

  atc_hdlc_u8 information[] = {0x7E, 0x7D};
  atc_hdlc_send_packet_start(&ctx, 0xAA, 0xBB); // Addr, Ctrl
  atc_hdlc_send_packet_information_byte(&ctx, 0x7C); // Data
  atc_hdlc_send_packet_information_bytes_array(&ctx, information, 2); // Data (Stuffing needed)
  atc_hdlc_send_packet_information_byte(&ctx, 0x7F); // Data
  atc_hdlc_send_packet_information_byte(&ctx, 0x7A); // Data
  atc_hdlc_send_packet_end(&ctx);

  print_hexdump("TX Buffer (Streamed)", tx_buffer, tx_len);

  for (int i = 0; i < tx_len; i++)
    atc_hdlc_input_byte(&ctx, tx_buffer[i]);

  if (rx_callback_count == 1 && last_rx_frame.information_len == 5) {
    // Information should be 7C 7E 7D 7F 7A
    if (last_rx_frame.information[0] == 0x7C &&
        last_rx_frame.information[1] == 0x7E &&
        last_rx_frame.information[2] == 0x7D &&
        last_rx_frame.information[3] == 0x7F &&
        last_rx_frame.information[4] == 0x7A) {
      assert_pass("Streaming API");
    } else {
      assert_fail("Streaming API", "Information content mismatch");
    }
  } else {
    assert_fail("Streaming API", "Frame not received");
  }
}

void test_fragmented_delivery() {
  printf("========================================\n");
  printf("TEST: Fragmented / Slow Delivery\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
  reset_test();

  atc_hdlc_frame_t frame_out = {
      .address = 0x99, .control.value = 0x88, .information_len = 10};
  memcpy(frame_out.information, "0123456789", 10);
  atc_hdlc_send_frame(&ctx, &frame_out);

  // Simulate UART getting bytes 1 by 1 with delays (conceptually)
  // or chunks.
  printf("Feeding bytes 1 by 1...\n");
  for (int i = 0; i < tx_len; i++) {
    atc_hdlc_input_byte(&ctx, tx_buffer[i]);
  }

  if (rx_callback_count != 1)
    assert_fail("Fragmented 1-by-1", "Failed simple loop");

  // Reset and try CHUNKS
  reset_test();
  atc_hdlc_send_frame(&ctx, &frame_out);
  rx_callback_count = 0;

  printf("Feeding in 3 chunks...\n");
  int chunk1 = 3;
  int chunk2 = 5;
  int chunk3 = tx_len - chunk1 - chunk2;

  printf("Chunk 1 (%d bytes)\n", chunk1);
  for (int i = 0; i < chunk1; i++)
    atc_hdlc_input_byte(&ctx, tx_buffer[i]);

  printf("Chunk 2 (%d bytes)\n", chunk2);
  for (int i = chunk1; i < chunk1 + chunk2; i++)
    atc_hdlc_input_byte(&ctx, tx_buffer[i]);

  printf("Chunk 3 (%d bytes)\n", chunk3);
  for (int i = chunk1 + chunk2; i < tx_len; i++)
    atc_hdlc_input_byte(&ctx, tx_buffer[i]);

  if (rx_callback_count == 1) {
    assert_pass("Fragmented Delivery");
  } else {
    assert_fail("Fragmented Delivery", "Chunked delivery failed");
  }
}

// --- Control Field Tests ---

void test_control_field_i() {
    printf("========================================\n");
    printf("TEST: Control Field - I-Frame Loopback\n");
    printf("========================================\n");
    
    atc_hdlc_context_t ctx;
    atc_hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
    reset_test();
    
    // N(S)=5, N(R)=3, P/F=1
    atc_hdlc_frame_t i_frame = {
        .address = 0x01,
        .control = atc_hdlc_create_i_ctrl(5, 3, 1),
        .information_len = 0
    };
    
    printf("Generated I-Frame Ctrl Value: 0x%02X\n", i_frame.control.value);
    
    atc_hdlc_send_frame(&ctx, &i_frame);
    for(int i=0; i<tx_len; i++) atc_hdlc_input_byte(&ctx, tx_buffer[i]);
    
    if(rx_callback_count == 1 && last_rx_frame.type == HDLC_FRAME_I) {
         atc_hdlc_control_t rc = last_rx_frame.control;
         printf("Received: Type=I, N(S)=%d, N(R)=%d, P/F=%d\n", 
                rc.i_frame.ns, rc.i_frame.nr, rc.i_frame.pf);
         
         if(rc.i_frame.ns == 5 && rc.i_frame.nr == 3 && rc.i_frame.pf == 1) {
             assert_pass("I-Frame Loopback");
         } else {
             assert_fail("I-Frame", "Field content mismatch");
         }
    } else {
        assert_fail("I-Frame", "Frame not received or wrong type");
    }
}

void test_control_field_s() {
    printf("========================================\n");
    printf("TEST: Control Field - S-Frame Loopback\n");
    printf("========================================\n");
    
    atc_hdlc_context_t ctx;
    atc_hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
    reset_test();
    
    // RR (S=00), N(R)=7, P/F=0
    atc_hdlc_frame_t s_frame = {
        .address = 0x01,
        .control = atc_hdlc_create_s_ctrl(0, 7, 0), 
        .information_len = 0
    };
    
    printf("Generated S-Frame Ctrl Value: 0x%02X\n", s_frame.control.value);
    atc_hdlc_send_frame(&ctx, &s_frame);
    for(int i=0; i<tx_len; i++) atc_hdlc_input_byte(&ctx, tx_buffer[i]);

    if(rx_callback_count == 1 && last_rx_frame.type == HDLC_FRAME_S) {
         atc_hdlc_control_t rc = last_rx_frame.control;
         printf("Received: Type=S, S=%d, N(R)=%d, P/F=%d\n", 
                rc.s_frame.s, rc.s_frame.nr, rc.s_frame.pf);
         
         if(rc.s_frame.s == 0 && rc.s_frame.nr == 7) {
             assert_pass("S-Frame Loopback");
         } else {
             assert_fail("S-Frame", "Field content mismatch");
         }
    } else {
        assert_fail("S-Frame", "Frame not received or wrong type");
    }
}

void test_control_field_u() {
    printf("========================================\n");
    printf("TEST: Control Field - U-Frame Loopback\n");
    printf("========================================\n");
    
    atc_hdlc_context_t ctx;
    atc_hdlc_init(&ctx, mock_tx_cb, mock_rx_cb, NULL);
    reset_test();
    
    // SABM: 0x2F (base) + P=1 => 0x3F
    // M_LO = 3 (11), M_HI = 1 (001)
    atc_hdlc_frame_t u_frame = {
        .address = 0x01,
        .control = atc_hdlc_create_u_ctrl(3, 1, 1), 
        .information_len = 0
    };
    
    printf("Generated U-Frame (SABM) Ctrl Value: 0x%02X\n", u_frame.control.value);
    
    atc_hdlc_send_frame(&ctx, &u_frame);
    for(int i=0; i<tx_len; i++) atc_hdlc_input_byte(&ctx, tx_buffer[i]);

    if(rx_callback_count == 1 && last_rx_frame.type == HDLC_FRAME_U) {
         atc_hdlc_control_t rc = last_rx_frame.control;
         printf("Received: Type=U, M_LO=%d, M_HI=%d, P=%d\n", 
                rc.u_frame.m_lo, rc.u_frame.m_hi, rc.u_frame.pf);
         
         if(rc.value == 0x3F) {
             assert_pass("U-Frame Loopback");
         } else {
             assert_fail("U-Frame", "Field content mismatch");
         }
    } else {
        assert_fail("U-Frame", "Frame not received or wrong type");
    }
}

int main() {
  printf("\n%sSTARTING COMPREHENSIVE HDLC TEST SUITE%s\n", COL_YELLOW,
         COL_RESET);
  printf("----------------------------------------\n\n");

  test_basic_frame();
  test_empty_information();
  test_byte_stuffing_heavy();
  test_garbage_noise();
  test_consecutive_flags();
  test_min_size_rejection();
  test_aborted_frame();
  test_crc_error_injection();
  test_mtu_overflow();
  test_mtu();
  test_streaming_api();
  test_fragmented_delivery();
  test_control_field_i();
  test_control_field_s();
  test_control_field_u();

  printf("\n%sALL TESTS PASSED SUCCESSFULLY!%s\n", COL_GREEN, COL_RESET);
  return 0;
}
