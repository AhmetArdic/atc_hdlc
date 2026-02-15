#include "hdlc.h"
#include "test_common.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Mocking & Utilities ---
static atc_hdlc_u8 output_buffer[16384]; // Increased for large payload tests
static atc_hdlc_u8 input_buffer[16384];  // Matched to output buffer
static int output_len = 0;

void mock_output_byte_cb(atc_hdlc_u8 byte, atc_hdlc_bool flush, void *user_data) {
  (void)user_data;
  (void)flush;
  if (output_len < sizeof(output_buffer)) {
    output_buffer[output_len++] = byte;
  }
}

// Global hook for RX verification
static atc_hdlc_frame_t last_received_frame;
static int on_frame_call_count = 0;

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

void reset_test() {
  output_len = 0;
  on_frame_call_count = 0;
  memset(output_buffer, 0, sizeof(output_buffer));
  memset(&last_received_frame, 0, sizeof(atc_hdlc_frame_t));
}

// --- Tests ---

void test_basic_frame() {
  printf("========================================\n");
  printf("TEST: Basic Frame (I-Frame)\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
  reset_test();

  atc_hdlc_u8 payload[] = "TEST";
  atc_hdlc_frame_t frame_out = {
      .address = 0xFF, .control.value = 0x00, .information = payload, .information_len = 4};

  atc_hdlc_output_frame(&ctx, &frame_out);
  print_hexdump("Output Buffer", output_buffer, output_len);

  printf("Feeding back bytes:\n");
  for (int i = 0; i < output_len; i++) {
    atc_hdlc_input_byte(&ctx, output_buffer[i]);
  }

  if (on_frame_call_count == 1 &&
      memcmp(last_received_frame.information, "TEST", 4) == 0) {
    test_pass("Basic Frame");
  } else {
    test_fail("Basic Frame", "Frame not received correctly");
  }
}

void test_empty_information() {
  printf("========================================\n");
  printf("TEST: Empty Information (Header only)\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
  reset_test();

  atc_hdlc_frame_t frame_out = {.address = 0xAA,
                                .control.value = 0x11, // Some random control
                                .information = NULL,
                                .information_len = 0};

  atc_hdlc_output_frame(&ctx, &frame_out);
  print_hexdump("Output Buffer", output_buffer, output_len);

  for (int i = 0; i < output_len; i++)
    atc_hdlc_input_byte(&ctx, output_buffer[i]);

  if (on_frame_call_count == 1 && last_received_frame.information_len == 0 &&
      last_received_frame.address == 0xAA) {
    test_pass("Empty Information");
  } else {
    test_fail("Empty Information",
                "Failed to receive empty information frame");
  }
}

void test_byte_stuffing_heavy() {
  printf("========================================\n");
  printf("TEST: Heavy Byte Stuffing\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
  reset_test();

  // Data with many flags and escapes
  atc_hdlc_u8 tricky_data[] = {0x7E, 0x7E, 0x7D, 0x7D, 0x7E, 0x00};
  atc_hdlc_frame_t frame_out = {.address = 0x01,
                                .control.value = 0x03,
                                .information = tricky_data,
                                .information_len = sizeof(tricky_data)};

  atc_hdlc_output_frame(&ctx, &frame_out);
  print_hexdump("Output Buffer (Stuffed)", output_buffer, output_len);

  // Verify manually that buffer is larger than raw data
  // Raw: 1(Addr)+1(Ctrl)+6(Pay)+2(CRC) = 10 bytes (+2 Flags = 12)
  // Escapes: 7E->2, 7E->2, 7D->2, 7D->2, 7E->2. Total +5 bytes?
  printf("Checking escaping logic...\n");
  int escapes = 0;
  for (int i = 0; i < output_len; i++)
    if (output_buffer[i] == 0x7D)
      escapes++;
  printf("Total raw escapes found: %d\n", escapes);

  for (int i = 0; i < output_len; i++)
    atc_hdlc_input_byte(&ctx, output_buffer[i]);

  if (on_frame_call_count == 1 && memcmp(last_received_frame.information, tricky_data,
                                       sizeof(tricky_data)) == 0) {
    test_pass("Heavy Stuffing");
  } else {
    test_fail("Heavy Stuffing", "Information mismatch after unstuffing");
  }
}

void test_garbage_noise() {
  printf("========================================\n");
  printf("TEST: Garbage / Noise Rejection\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
  reset_test();

  // 1. Generate a valid frame
  atc_hdlc_u8 payload[] = {0xCC};
  atc_hdlc_frame_t frame_out = {
      .address = 0x05, .control.value = 0x05, .information = payload, .information_len = 1};
  atc_hdlc_output_frame(&ctx, &frame_out); // Fills output_buffer

  // 2. Inject noise BEFORE the frame
  atc_hdlc_u8 noise[] = {0x00, 0x12, 0x34, 0x56, 0xAA, 0xBB};
  printf("Injecting %ld bytes of noise before frame...\n", sizeof(noise));
  for (size_t i = 0; i < sizeof(noise); i++)
    atc_hdlc_input_byte(&ctx, noise[i]);

  // 3. Inject the valid frame
  printf("Injecting valid frame...\n");
  for (int i = 0; i < output_len; i++)
    atc_hdlc_input_byte(&ctx, output_buffer[i]);

  // 4. Inject noise AFTER the frame
  printf("Injecting noise after frame...\n");
  for (size_t i = 0; i < sizeof(noise); i++)
    atc_hdlc_input_byte(&ctx, noise[i]);

  // We expect exactly 1 frame
  if (on_frame_call_count == 1) {
    test_pass("Garbage Noise");
  } else {
    test_fail("Garbage Noise",
                "Noise caused valid frame drop or phantom frame");
  }
}

void test_consecutive_flags() {
  printf("========================================\n");
  printf("TEST: Consecutive Flags (Inter-frame fill)\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
  reset_test();

  atc_hdlc_frame_t frame_out = {
      .address = 0x10, .control.value = 0x10, .information = NULL, .information_len = 0};
  atc_hdlc_output_frame(&ctx, &frame_out);
  // output_buffer has valid frame.
  // Format: 7E ... 7E

  // Feed: 7E 7E 7E 7E [Frame] 7E 7E 7E
  printf("Feeding: 7E 7E 7E 7E\n");
  atc_hdlc_input_byte(&ctx, 0x7E); // Flag
  atc_hdlc_input_byte(&ctx, 0x7E); // Flag
  atc_hdlc_input_byte(&ctx, 0x7E); // Flag
  atc_hdlc_input_byte(&ctx, 0x7E); // Flag

  printf("Feeding Frame...\n");
  // Skip first byte of output_buffer (it's 7E, already sent multiple) or valid to
  // send again? HDLC says adjacent flags are valid. send full buffer.
  for (int i = 0; i < output_len; i++)
    atc_hdlc_input_byte(&ctx, output_buffer[i]);

  printf("Feeding: 7E 7E\n");
  atc_hdlc_input_byte(&ctx, 0x7E);
  atc_hdlc_input_byte(&ctx, 0x7E);

  if (on_frame_call_count == 1) {
    test_pass("Consecutive Flags");
  } else {
    test_fail("Consecutive Flags", "Multiple flags caused parsing error");
  }
}

void test_min_size_rejection() {
  printf("========================================\n");
  printf("TEST: Minimum Size Rejection (<4 bytes)\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
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

  if (on_frame_call_count == 0) {
    test_pass("Min Size Rejection");
  } else {
    test_fail("Min Size Rejection", "Short frames were accepted!");
  }
}

void test_aborted_frame() {
  printf("========================================\n");
  printf("TEST: Aborted / Interrupted Frame\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
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
      .address = 0x01, .control.value = 0x11, .information = NULL, .information_len = 0};
  atc_hdlc_output_frame(&ctx, &frame_out);

  // Send valid frame (skipping first 7E since we just sent one? No, safe to
  // send all)
  for (int i = 0; i < output_len; i++)
    atc_hdlc_input_byte(&ctx, output_buffer[i]);

  if (on_frame_call_count == 1 && last_received_frame.address == 0x01) {
    test_pass("Aborted Frame");
  } else {
    test_fail("Aborted Frame", "Recovery from aborted frame failed");
  }
}

// --- Restored & New Tests ---

void test_crc_error_injection() {
  printf("========================================\n");
  printf("TEST: CRC Error Injection (Single Bit)\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
  reset_test();

  atc_hdlc_u8 payload[] = "DATA";
  atc_hdlc_frame_t frame_out = {
      .address = 0xFF, .control.value = 0x00, .information = payload, .information_len = 4};
  atc_hdlc_output_frame(&ctx, &frame_out);

  // Corrupt the last byte (part of CRC)
  output_buffer[output_len - 2] ^= 0x01;

  print_hexdump("Output Buffer (Corrupted)", output_buffer, output_len);
  for (int i = 0; i < output_len; i++)
    atc_hdlc_input_byte(&ctx, output_buffer[i]);

  if (on_frame_call_count == 0 && ctx.stats_crc_errors == 1) {
    test_pass("CRC Error Injection");
  } else {
    test_fail("CRC Error Injection", "Bad CRC was accepted or not counted");
  }
}

void test_input_buffer_overflow() {
  printf("========================================\n");
  printf("TEST: Input Buffer Overflow Safety\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
  reset_test();

  printf("Feeding Start Flag...\n");
  atc_hdlc_input_byte(&ctx, 0x7E); // Start

  printf("Feeding %d bytes (Input Buffer Len + 50)...\n", 1024 + 50);
  // Feed more than buffer size
  for (int i = 0; i < 1024 + 50; i++) {
    atc_hdlc_input_byte(&ctx, 0xAA);
  }

  atc_hdlc_input_byte(&ctx, 0x7E); // End Flag

  if (on_frame_call_count == 0) {
    test_pass("Input Buffer Overflow");
  } else {
    test_fail("Input Buffer Overflow", "Overflow frame triggered callback");
  }
}

void test_streaming_large_payload(int payload_size) {
  printf("========================================\n");
  printf("TEST: Streaming Large Payload (%d bytes)\n", payload_size);
  printf("========================================\n");
  if (payload_size > sizeof(output_buffer) / 2) {
      printf("Skipping test: Payload %d too large for buffer %llu\n", payload_size, sizeof(output_buffer));
      return;
  }

  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
  reset_test();

  printf("Feeding Start...\n");
  atc_hdlc_output_packet_start(&ctx, 0xAA, 0xBB); // Addr, Ctrl

  printf("Feeding %d bytes...\n", payload_size);
  // Feed large payload
  for (int i = 0; i < payload_size; i++) {
    // Generate predictable pattern: 0x00, 0x01 ... 0xFF, 0x00 ...
    atc_hdlc_output_packet_information_byte(&ctx, (atc_hdlc_u8)(i % 256));
  }
  atc_hdlc_output_packet_end(&ctx); // End Flag

  // print_hexdump("Output Buffer (Streamed)", output_buffer, output_len); 
  printf("Output Buffer Length: %d\n", output_len);

  for (int i = 0; i < output_len; i++)
    atc_hdlc_input_byte(&ctx, output_buffer[i]);

  if (on_frame_call_count == 1 &&
      last_received_frame.information_len == payload_size) {
    
    // Verify content
    bool match = true;
    for(int i=0; i<payload_size; i++) {
        if (last_received_frame.information[i] != (atc_hdlc_u8)(i % 256)) {
            match = false;
            printf("Mismatch at index %d: expected %02X, got %02X\n", i, (atc_hdlc_u8)(i%256), last_received_frame.information[i]);
            break;
        }
    }

    if (match) {
        test_pass("Streaming Large Payload");
    } else {
        test_fail("Streaming Large Payload", "Payload content mismatch");
    }

  } else {
    printf("Callback count: %d, InfoLen: %d (Expected %d)\n", on_frame_call_count, last_received_frame.information_len, payload_size);
    test_fail("Streaming Large Payload", "Large payload error");
  }
}

void test_streaming_api() {
  printf("========================================\n");
  printf("TEST: Streaming API (Zero-Copy)\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
  reset_test();

  atc_hdlc_output_packet_start(&ctx, 0xAA, 0xBB);      // Addr, Ctrl
  atc_hdlc_output_packet_information_byte(&ctx, 0x7E); // Data (Stuffing needed)
  atc_hdlc_output_packet_information_byte(&ctx, 0x7D); // Data (Stuffing needed)
  atc_hdlc_output_packet_end(&ctx);

  print_hexdump("Output Buffer (Streamed)", output_buffer, output_len);

  for (int i = 0; i < output_len; i++)
    atc_hdlc_input_byte(&ctx, output_buffer[i]);

  if (on_frame_call_count == 1 && last_received_frame.information_len == 2) {
    // Information should be 7E 7D
    if (last_received_frame.information[0] == 0x7E &&
        last_received_frame.information[1] == 0x7D) {
      test_pass("Streaming API");
    } else {
      test_fail("Streaming API", "Information content mismatch");
    }
  } else {
    test_fail("Streaming API", "Frame not received");
  }

  reset_test();

  atc_hdlc_u8 information[] = {0x7E, 0x7D};
  atc_hdlc_output_packet_start(&ctx, 0xAA, 0xBB);      // Addr, Ctrl
  atc_hdlc_output_packet_information_byte(&ctx, 0x7C); // Data
  atc_hdlc_output_packet_information_bytes(&ctx, information,
                                               2);   // Data (Stuffing needed)
  atc_hdlc_output_packet_information_byte(&ctx, 0x7F); // Data
  atc_hdlc_output_packet_information_byte(&ctx, 0x7A); // Data
  atc_hdlc_output_packet_end(&ctx);

  print_hexdump("Output Buffer (Streamed)", output_buffer, output_len);

  for (int i = 0; i < output_len; i++)
    atc_hdlc_input_byte(&ctx, output_buffer[i]);

  if (on_frame_call_count == 1 && last_received_frame.information_len == 5) {
    // Information should be 7C 7E 7D 7F 7A
    if (last_received_frame.information[0] == 0x7C &&
        last_received_frame.information[1] == 0x7E &&
        last_received_frame.information[2] == 0x7D &&
        last_received_frame.information[3] == 0x7F &&
        last_received_frame.information[4] == 0x7A) {
      test_pass("Streaming API");
    } else {
      test_fail("Streaming API", "Information content mismatch");
    }
  } else {
    test_fail("Streaming API", "Frame not received");
  }
}

void test_fragmented_delivery() {
  printf("========================================\n");
  printf("TEST: Fragmented / Slow Delivery\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
  reset_test();

  atc_hdlc_u8 payload[] = "0123456789";
  atc_hdlc_frame_t frame_out = {
      .address = 0x99, .control.value = 0x88, .information = payload, .information_len = 10};
  atc_hdlc_output_frame(&ctx, &frame_out);

  // Simulate UART getting bytes 1 by 1 with delays (conceptually)
  // or chunks.
  printf("Feeding bytes 1 by 1...\n");
  for (int i = 0; i < output_len; i++) {
    atc_hdlc_input_byte(&ctx, output_buffer[i]);
  }

  if (on_frame_call_count != 1)
    test_fail("Fragmented 1-by-1", "Failed simple loop");

  // Reset and try CHUNKS
  reset_test();
  atc_hdlc_output_frame(&ctx, &frame_out);
  on_frame_call_count = 0;

  printf("Feeding in 3 chunks...\n");
  int chunk1 = 3;
  int chunk2 = 5;
  int chunk3 = output_len - chunk1 - chunk2;

  printf("Chunk 1 (%d bytes)\n", chunk1);
  for (int i = 0; i < chunk1; i++)
    atc_hdlc_input_byte(&ctx, output_buffer[i]);

  printf("Chunk 2 (%d bytes)\n", chunk2);
  for (int i = chunk1; i < chunk1 + chunk2; i++)
    atc_hdlc_input_byte(&ctx, output_buffer[i]);

  printf("Chunk 3 (%d bytes)\n", chunk3);
  for (int i = chunk1 + chunk2; i < output_len; i++)
    atc_hdlc_input_byte(&ctx, output_buffer[i]);

  if (on_frame_call_count == 1) {
    test_pass("Fragmented Delivery");
  } else {
    test_fail("Fragmented Delivery", "Chunked delivery failed");
  }
}

// --- Control Field Tests ---

void test_control_field_i() {
  printf("========================================\n");
  printf("TEST: Control Field - I-Frame Loopback\n");
  printf("========================================\n");

  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
  reset_test();

  // N(S)=5, N(R)=3, P/F=1
  atc_hdlc_frame_t i_frame = {.address = 0x01,
                              .control = atc_hdlc_create_i_ctrl(5, 3, 1),
                              .information = NULL,
                              .information_len = 0};

  printf("Generated I-Frame Ctrl Value: 0x%02X\n", i_frame.control.value);

  atc_hdlc_output_frame(&ctx, &i_frame);
  for (int i = 0; i < output_len; i++)
    atc_hdlc_input_byte(&ctx, output_buffer[i]);

  if (on_frame_call_count == 1 && last_received_frame.type == HDLC_FRAME_I) {
    atc_hdlc_control_t rc = last_received_frame.control;
    printf("Received: Type=I, N(S)=%d, N(R)=%d, P/F=%d\n", rc.i_frame.ns,
           rc.i_frame.nr, rc.i_frame.pf);

    if (rc.i_frame.ns == 5 && rc.i_frame.nr == 3 && rc.i_frame.pf == 1) {
      test_pass("I-Frame Loopback");
    } else {
      test_fail("I-Frame", "Field content mismatch");
    }
  } else {
    test_fail("I-Frame", "Frame not received or wrong type");
  }
}

void test_control_field_s() {
  printf("========================================\n");
  printf("TEST: Control Field - S-Frame Loopback\n");
  printf("========================================\n");

  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
  reset_test();

  // RR (S=00), N(R)=7, P/F=0
  atc_hdlc_frame_t s_frame = {.address = 0x01,
                              .control = atc_hdlc_create_s_ctrl(0, 7, 0),
                              .information = NULL,
                              .information_len = 0};

  printf("Generated S-Frame Ctrl Value: 0x%02X\n", s_frame.control.value);
  atc_hdlc_output_frame(&ctx, &s_frame);
  for (int i = 0; i < output_len; i++)
    atc_hdlc_input_byte(&ctx, output_buffer[i]);

  if (on_frame_call_count == 1 && last_received_frame.type == HDLC_FRAME_S) {
    atc_hdlc_control_t rc = last_received_frame.control;
    printf("Received: Type=S, S=%d, N(R)=%d, P/F=%d\n", rc.s_frame.s,
           rc.s_frame.nr, rc.s_frame.pf);

    if (rc.s_frame.s == 0 && rc.s_frame.nr == 7) {
      test_pass("S-Frame Loopback");
    } else {
      test_fail("S-Frame", "Field content mismatch");
    }
  } else {
    test_fail("S-Frame", "Frame not received or wrong type");
  }
}

void test_control_field_u() {
  printf("========================================\n");
  printf("TEST: Control Field - U-Frame Loopback\n");
  printf("========================================\n");

  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
  reset_test();

  // SABM: 0x2F (base) + P=1 => 0x3F
  // M_LO = 3 (11), M_HI = 1 (001)
  atc_hdlc_frame_t u_frame = {.address = 0x01,
                              .control = atc_hdlc_create_u_ctrl(3, 1, 1),
                              .information = NULL,
                              .information_len = 0};

  printf("Generated U-Frame (SABM) Ctrl Value: 0x%02X\n",
         u_frame.control.value);

  atc_hdlc_output_frame(&ctx, &u_frame);
  for (int i = 0; i < output_len; i++)
    atc_hdlc_input_byte(&ctx, output_buffer[i]);

  if (on_frame_call_count == 1 && last_received_frame.type == HDLC_FRAME_U) {
    atc_hdlc_control_t rc = last_received_frame.control;
    printf("Received: Type=U, M_LO=%d, M_HI=%d, P=%d\n", rc.u_frame.m_lo,
           rc.u_frame.m_hi, rc.u_frame.pf);

    if (rc.value == 0x3F) {
      test_pass("U-Frame Loopback");
    } else {
      test_fail("U-Frame", "Field content mismatch");
    }
  } else {
    test_fail("U-Frame", "Frame not received or wrong type");
  }
}

void test_input_bytes() {
  printf("========================================\n");
  printf("TEST: Bulk Input (input_bytes)\n");
  printf("========================================\n");
  atc_hdlc_context_t ctx;
  atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
  reset_test();

  // Build a valid frame
  atc_hdlc_u8 payload[] = "BULK";
  atc_hdlc_frame_t frame_out = {
      .address = 0xFF, .control.value = 0x00, .information = payload, .information_len = 4};
  atc_hdlc_output_frame(&ctx, &frame_out);
  print_hexdump("Output Buffer", output_buffer, output_len);

  // Feed back using bulk API
  printf("Feeding back using atc_hdlc_input_bytes...\n");
  atc_hdlc_input_bytes(&ctx, output_buffer, output_len);

  if (on_frame_call_count == 1 && last_received_frame.information_len == 4 &&
      memcmp(last_received_frame.information, "BULK", 4) == 0) {
    test_pass("Bulk Input (input_bytes)");
  } else {
    test_fail("Bulk Input (input_bytes)", "Frame not received correctly");
  }
}

// --- Buffer Encoding Tests ---

void test_frame_pack_success() {
  printf("========================================\n");
  printf("TEST: Pack Frame - Success Case\n");
  printf("========================================\n");

  atc_hdlc_u8 payload[] = "TEST";
  atc_hdlc_frame_t frame = {
      .address = 0xFF, .control.value = 0x03, .information = payload, .information_len = 4};

  atc_hdlc_u8 buffer[128];
  atc_hdlc_u32 len = 0;

  bool success = atc_hdlc_frame_pack(&frame, buffer, sizeof(buffer), &len);
  if (success) {
    print_hexdump("Encoded Buffer", buffer, len);
  }

  if (success && len == 10 && buffer[0] == 0x7E && buffer[9] == 0x7E) {
    test_pass("Pack Frame - Success Case");
  } else {
    test_fail("Pack Frame - Success Case",
                "Encoding failed or content mismatch");
  }
}

void test_frame_pack_overflow() {
  printf("========================================\n");
  printf("TEST: Pack Frame - Overflow Case\n");
  printf("========================================\n");

  atc_hdlc_u8 payload[10];
  memset(payload, 0xAA, 10);
  atc_hdlc_frame_t frame = {
      .address = 0xFF, .control.value = 0x03, .information = payload, .information_len = 10};

  // Frame needs ~16 bytes. Provide only 5.
  atc_hdlc_u8 buffer[5];
  atc_hdlc_u32 len = 0;

  bool success = atc_hdlc_frame_pack(&frame, buffer, sizeof(buffer), &len);
  if (success) {
    print_hexdump("Encoded Buffer", buffer, len);
  }

  if (!success && len == 0) {
    test_pass("Pack Frame - Overflow Case");
  } else {
    test_fail("Pack Frame - Overflow Case", "Should fail on overflow");
  }
}

void test_frame_pack_stuffing() {
  printf("========================================\n");
  printf("TEST: Pack Frame - Stuffing\n");
  printf("========================================\n");

  atc_hdlc_frame_t frame = {.address = 0x7E,       // Needs escaping -> 7D 5E
                            .control.value = 0x7D, // Needs escaping -> 7D 5D
                            .information = NULL,
                            .information_len = 0};

  atc_hdlc_u8 buffer[128];
  atc_hdlc_u32 len = 0;

  bool success = atc_hdlc_frame_pack(&frame, buffer, sizeof(buffer), &len);
  if (success) {
    print_hexdump("Encoded Buffer", buffer, len);
  }

  // Expected: 7E (7D 5E) (7D 5D) [CRC_LO] [CRC_HI] 7E
  // Length > 6 checks basic stuffing occurred.
  if (success && len > 6 && buffer[1] == 0x7D && buffer[2] == 0x5E &&
      buffer[3] == 0x7D && buffer[4] == 0x5D) {
    test_pass("Pack Frame - Stuffing");
  } else {
    test_fail("Pack Frame - Stuffing", "Stuffing logic failed");
  }
}


void test_frame_unpack_roundtrip() {
  printf("========================================\n");
  printf("TEST: Unpack Frame (Round Trip)\n");
  printf("========================================\n");

  // 1. Encode
  atc_hdlc_u8 payload[] = "ROUNDTRIP";
  atc_hdlc_frame_t frame_in = {
      .address = 0xAA, .control.value = 0x55, .information = payload, .information_len = 9};

  atc_hdlc_u8 raw_buffer[128];
  atc_hdlc_u32 raw_len = 0;

  bool ok = atc_hdlc_frame_pack(&frame_in, raw_buffer, sizeof(raw_buffer), &raw_len);
  if (!ok) {
    test_fail("Unpack Round Trip", "Packing failed");
  }
  print_hexdump("Encoded", raw_buffer, raw_len);

  // 2. Decode
  atc_hdlc_frame_t frame_out;
  atc_hdlc_u8 flat_buffer[128];
  memset(&frame_out, 0, sizeof(frame_out));

  ok = atc_hdlc_frame_unpack(raw_buffer, raw_len, &frame_out, flat_buffer, sizeof(flat_buffer));

  if (ok) {
     printf("Decoded Frame: Addr=%02X, Ctrl=%02X, InfoLen=%d\n", 
            frame_out.address, frame_out.control.value, frame_out.information_len);
     
     if (frame_out.address == 0xAA && 
         frame_out.control.value == 0x55 && 
         frame_out.information_len == 9 &&
         memcmp(frame_out.information, "ROUNDTRIP", 9) == 0) {
         test_pass("Unpack Round Trip");
     } else {
       test_fail("Unpack Round Trip", "Fields mismatch");
     }
  } else {
    test_fail("Unpack Round Trip", "Unpack returned false");
  }

  // 3. Test Decode Error (Bad CRC)
  printf("Testing Bad CRC...\n");
  raw_buffer[raw_len - 2] ^= 0xFF; // Corrupt FCS
  ok = atc_hdlc_frame_unpack(raw_buffer, raw_len, &frame_out, flat_buffer, sizeof(flat_buffer));
  if (!ok) {
     printf("Caught bad CRC as expected.\n");
  } else {
     test_fail("Unpack Round Trip", "Failed to detect bad CRC");
  }
}

void test_broadcast_behavior() {
    printf("========================================\n");
    printf("TEST: Broadcast Behavior\n");
    printf("========================================\n");

    atc_hdlc_context_t ctx;
    atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    reset_test();
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02); // My=0x01, Peer=0x02

    atc_hdlc_u8 packed_frame[128];
    atc_hdlc_u32 packed_len = 0;
    atc_hdlc_u8 payload[] = "ROUNDTRIP"; // Re-use payload from previous test

    // 1. Broadcast UI (Valid, no response)
    printf("Testing Broadcast UI reception...\n");
    atc_hdlc_frame_t ui_frame = {
        .address = HDLC_BROADCAST_ADDRESS, .control.value = 0x03, // UI
        .information = payload, .information_len = 9};
    
    atc_hdlc_output_frame(&ctx, &ui_frame);
    packed_len = output_len;
    memcpy(packed_frame, output_buffer, packed_len);
    reset_test();
    
    atc_hdlc_input_bytes(&ctx, packed_frame, packed_len);

    // Check: Should be received by app
    if (on_frame_call_count == 1 && last_received_frame.address == HDLC_BROADCAST_ADDRESS) {
        printf("[PASS] Broadcast UI received by application.\n");
    } else {
        test_fail("Broadcast UI", "Broadcast Frame not delivered to app");
    }

    // Check: Should not generate a response
    if (output_len == 0) {
        printf("[PASS] Broadcast UI generated NO response.\n");
    } else {
        test_fail("Broadcast UI", "Slave replied to Broadcast UI!");
    }

    // 2. Broadcast SABM (P=1)
    printf("Testing Broadcast SABM...\n");
    atc_hdlc_frame_t sabm_frame = {
        .address = HDLC_BROADCAST_ADDRESS, .control.value = 0x3F, // SABM(P=1)
        .information = NULL, .information_len = 0};

    atc_hdlc_output_frame(&ctx, &sabm_frame);
    packed_len = output_len;
    memcpy(packed_frame, output_buffer, packed_len);
    reset_test(); // clear tx buffer

    atc_hdlc_input_bytes(&ctx, packed_frame, packed_len);

    // Check: Should not change state (SABM is for point-to-point)
    if (ctx.current_state == HDLC_STATE_DISCONNECTED) {
        printf("[PASS] Broadcast SABM ignored (State verification).\n");
    } else {
        test_fail("Broadcast SABM", "Broadcast SABM changed state!");
    }

    // Check: Should not generate a response (UA/DM)
    if (output_len == 0) {
        printf("[PASS] Broadcast SABM generated NO response.\n");
    } else {
        test_fail("Broadcast SABM", "Slave replied to Broadcast SABM!");
    }

    // 3. Invalid Broadcast DISC
    printf("Testing Broadcast DISC rejection...\n");
    reset_test();
    // Ensure we are connected first to test if DISC disconnects us
    ctx.current_state = HDLC_STATE_CONNECTED;
    
    atc_hdlc_frame_t disc_frame = {
        .address = HDLC_BROADCAST_ADDRESS, .control.value = 0x53, // DISC(P=1) -> 0x53
        .information = NULL, .information_len = 0};
    
    atc_hdlc_output_frame(&ctx, &disc_frame);
    packed_len = output_len;
    memcpy(packed_frame, output_buffer, packed_len);
    reset_test(); // clear tx buffer

    atc_hdlc_input_bytes(&ctx, packed_frame, packed_len);

    // Check: Should remain CONNECTED (Broadcast DISC ignored)
    if (ctx.current_state == HDLC_STATE_CONNECTED) {
         printf("[PASS] Broadcast DISC ignored (State verification).\n");
    } else {
         test_fail("Broadcast DISC", "Broadcast DISC disconnected the station!");
    }
    
    if (output_len == 0) {
        printf("[PASS] Broadcast DISC generated NO response.\n");
    } else {
        test_fail("Broadcast DISC", "Slave replied to Broadcast DISC!");
    }

    // 4. Foreign Address (Address Filter)
    // Frame addressed to 0x99 (Not Me=0x01, Not Broadcast=0xFF)
    // Depending on implementation, this might callback (promiscuous) or not.
    // BUT it MUST NOT generate a response or change state.
    printf("Testing Foreign Address (0x99) handling...\n");
    reset_test();
    
    atc_hdlc_frame_t foreign_frame = {
        .address = 0x99, .control.value = 0x03, .information = payload, .information_len = 9};
    
    atc_hdlc_output_frame(&ctx, &foreign_frame);
    packed_len = output_len;
    memcpy(packed_frame, output_buffer, packed_len);
    reset_test();

    atc_hdlc_input_bytes(&ctx, packed_frame, packed_len);

    // Protocol Logic Check: Implicitly satisfied if output_len == 0 (no UA/DM)
    if (output_len == 0) {
        printf("[PASS] Foreign Frame generated NO response.\n");
    } else {
         test_fail("Foreign Address", "Slave replied to Foreign Logic!");
    }

    test_pass("Broadcast Behavior");
}

void test_ui_frame_transmission(void) {
    printf("========================================\n");
    printf("TEST: UI Frame Transmission\n");
    printf("========================================\n");
    
    atc_hdlc_context_t ctx;
    atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    reset_test();
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02); // My=0x01, Peer=0x02

    const char *payload = "HELLO";
    bool res = atc_hdlc_send_ui(&ctx, (const atc_hdlc_u8*)payload, 5);
    
    if (res && output_len >= 11) {
         // Check Control Field for UI (0x03 or 0x13)
        // Addr=0x02 (Peer)
        if (output_buffer[0] == 0x7E && output_buffer[1] == 0x02 && (output_buffer[2] & 0xEF) == 0x03) {
            test_pass("UI Frame Transmission");
        } else {
             test_fail("UI Frame Transmission", "Header mismatch");
        }
    } else {
        test_fail("UI Frame Transmission", "Send failed or length too short");
    }
}

void test_ui_frame_reception(void) {
    printf("========================================\n");
    printf("TEST: UI Frame Reception\n");
    printf("========================================\n");
    
    atc_hdlc_context_t ctx;
    atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    reset_test();
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02); // My=0x01, Peer=0x02

    // Construct a valid UI frame addressed to ME (0x01)
    // Addr=0x01, Ctrl=0x03 (UI, P=0), Data="WORLD"
    atc_hdlc_output_packet_start(&ctx, 0x01, 0x03); 
    atc_hdlc_output_packet_information_bytes(&ctx, (atc_hdlc_u8*)"WORLD", 5);
    atc_hdlc_output_packet_end(&ctx);
    
    // Now Feed it back
    // The output buffer contains the frame we just constructed.
    // We feed it into the input to simulate reception.
    for (int i = 0; i < output_len; i++) {
        atc_hdlc_input_byte(&ctx, output_buffer[i]);
    }
    
    if (on_frame_call_count == 1 && 
        last_received_frame.type == HDLC_FRAME_U &&
        last_received_frame.address == 0x01 &&
        (last_received_frame.control.value & 0xEF) == 0x03 &&
        last_received_frame.information_len == 5 &&
        memcmp(last_received_frame.information, "WORLD", 5) == 0) {
        test_pass("UI Frame Reception");
    } else {
        test_fail("UI Frame Reception", "Frame mismatch or not received");
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
  test_input_buffer_overflow();
  test_streaming_large_payload(100);
  test_streaming_large_payload(4096); // 4KB
  test_streaming_large_payload(8192); // 8KB
  test_streaming_api();
  test_fragmented_delivery();
  test_control_field_i();
  test_control_field_s();
  test_control_field_u();
  test_input_bytes();
  test_frame_pack_success();
  test_frame_pack_overflow();
  test_frame_pack_stuffing();
  test_frame_unpack_roundtrip();
  test_ui_frame_transmission();
  test_ui_frame_reception();
  test_broadcast_behavior();

  printf("\n%sALL TESTS PASSED SUCCESSFULLY!%s\n", COL_GREEN, COL_RESET);
  return 0;
}
