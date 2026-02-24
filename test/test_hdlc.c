#include "../inc/hdlc.h"
#include "test_common.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Helpers ---

// setup_test_context is now in test_common.c

// --- Tests ---

/**
 * @brief Test: Basic I-Frame Transmission and Reception.
 *        Verifies that a simple frame can be sent, loopbacked, and received.
 */
void test_basic_frame() {
  printf("TEST: Basic Frame (I-Frame)\n");
  reset_test_state();

  atc_hdlc_context_t ctx;
  setup_test_context(&ctx);

  atc_hdlc_u8 payload[] = "TEST";
  atc_hdlc_frame_t frame_out = {
      .address = 0xFF, .control.value = 0x00, .information = payload, .information_len = 4};

  atc_hdlc_output_frame(&ctx, &frame_out);
  print_hexdump("Output Buffer", mock_output_buffer, mock_output_len);

  // Validate Output Size
  // Flag(1) + Addr(1) + Ctrl(1) + Data(4) + FCS(2) + Flag(1) = 10 bytes
  // If escaping occurs, it might be more. "TEST" = 54 45 53 54. No escaping needed.
  if (mock_output_len < 10) {
    test_fail("Basic Frame", "Output length too short");
  }

  // Loopback: Feed output to input
  int loop_len = mock_output_len;
  for (int i = 0; i < loop_len; i++) {
    atc_hdlc_input_byte(&ctx, mock_output_buffer[i]);
  }

  // Verify Reception
  if (on_frame_call_count != 1) {
    test_fail("Basic Frame", "Callback not triggered");
  }

  if (memcmp(last_received_frame.information, payload, 4) != 0) {
    test_fail("Basic Frame", "Data Mismatch");
  }

  test_pass("Basic Frame (I-Frame)");
}

/**
 * @brief Test: Empty Information Field.
 *        Checks handling of frames with zero payload length.
 */
void test_empty_information() {
  printf("TEST: Empty Information Field\n");
  reset_test_state();

  atc_hdlc_context_t ctx;
  setup_test_context(&ctx);

  atc_hdlc_frame_t frame_out = {
      .address = 0xFF, .control.value = 0x00, .information = NULL, .information_len = 0};

  atc_hdlc_output_frame(&ctx, &frame_out);
  
  int loop_len = mock_output_len;
  for (int i = 0; i < loop_len; i++) {
    atc_hdlc_input_byte(&ctx, mock_output_buffer[i]);
  }

  if (on_frame_call_count != 1) {
    test_fail("Empty Frame", "Frame not received");
  }
  if (last_received_frame.information_len != 0) {
    test_fail("Empty Frame", "Information length not zero");
  }

  test_pass("Empty Information Field");
}

/**
 * @brief Test: Heavy Byte Stuffing.
 *        Sends a payload full of 0x7E and 0x7D to stress the escaping logic.
 */
void test_byte_stuffing_heavy() {
  printf("TEST: Heavy Byte Stuffing (Flags/Escapes)\n");
  reset_test_state();

  atc_hdlc_context_t ctx;
  setup_test_context(&ctx);

  /*
   * NOTE: For static tests verifying basic decoding (byte stuffing, CRC, etc.),
   * we use Broadcast UI (Unnumbered Information) frames (Control = 0x03) instead of I-frames.
   * This is because the HDLC dispatcher strictly filters out duplicate/out-of-sequence
   * I-frames for Go-Back-N reliability. Repeatedly feeding identical mock I-frames 
   * (e.g., N(S)=0) would cause the dispatcher to rightfully drop them.
   */

  // Payload with special characters
  atc_hdlc_u8 special[] = {0x7E, 0x7D, 0x7E, 0x7D, 0x00, 0xFF, 0x7E};
  atc_hdlc_frame_t frame_out = {
      .address = 0xFF, .control.value = 0x03, .information = special, .information_len = sizeof(special)};

  atc_hdlc_output_frame(&ctx, &frame_out);
  // Verify escaping happened (size should be > raw size)
  // Raw: 7 (Flag) + 1 (Addr) + 1 (Ctrl) + 7 (Data) + 2 (FCS) + 1 (Flag) = 19
  // Escaped: 0x7E -> 7D 5E (2 bytes)
  // 0x7D -> 7D 5D (2 bytes)
  // Payload has 5 special chars (7E, 7D, 7E, 7D, 7E). So 5 extra bytes.
  // Addr/Ctrl/FCS might also need escaping but unlikely for fixed values used here.
  // Min expected: 13 (base) + 5 (escapes) = 18.
  if (mock_output_len < 18) {
     char msg[100];
     sprintf(msg, "Output too short for escaping (len=%d)", mock_output_len);
     test_fail("Stuffing", msg);
  }

  // Loopback
  int loop_len = mock_output_len;
  for (int i = 0; i < loop_len; i++) {
    atc_hdlc_input_byte(&ctx, mock_output_buffer[i]);
  }

  if (on_frame_call_count != 1) {
    test_fail("Stuffing", "Frame decode failed");
  }
  
  if (last_received_frame.information_len != sizeof(special) ||
      memcmp(last_received_frame.information, special, sizeof(special)) != 0) {
      test_fail("Stuffing", "Decoded payload mismatch");
  }

  test_pass("Heavy Byte Stuffing");
}

/**
 * @brief Test: Garbage/Noise rejection.
 *        Feeds random noise before and after a valid frame.
 */
void test_garbage_noise() {
  printf("TEST: Garbage / Noise Rejection\n");
  reset_test_state();

  atc_hdlc_context_t ctx;
  setup_test_context(&ctx);

  // Generate valid frame first
  atc_hdlc_frame_t valid_frame = {
      .address = 0xFF, .control.value = 0x03, .information = (atc_hdlc_u8*)"DATA", .information_len = 4};
  
  mock_output_len = 0;
  atc_hdlc_output_frame(&ctx, &valid_frame);
  
  // Store valid frame bytes
  int valid_len = mock_output_len;
  atc_hdlc_u8 valid_bytes[256];
  memcpy(valid_bytes, mock_output_buffer, valid_len);

  // 1. Feed Garbage
  atc_hdlc_u8 noise[] = {0x00, 0x01, 0xFF, 0x55, 0xAA, 0x7D}; // No 0x7E
  for(size_t i=0; i<sizeof(noise); i++) atc_hdlc_input_byte(&ctx, noise[i]);
  
  // 2. Feed Valid Frame
  for(int i=0; i<valid_len; i++) atc_hdlc_input_byte(&ctx, valid_bytes[i]);
  
  // 3. Feed More Garbage
  for(size_t i=0; i<sizeof(noise); i++) atc_hdlc_input_byte(&ctx, noise[i]);

  if (on_frame_call_count != 1) {
      test_fail("Garbage Noise", "Valid frame not extracted from noise");
  }
  
  test_pass("Garbage / Noise Rejection");
}

/**
 * @brief Test: Consecutive Flags.
 *        Frames separated by multiple 0x7E flags (0x7E 0x7E 0x7E Frame 0x7E 0x7E).
 */
void test_consecutive_flags() {
  printf("TEST: Consecutive Flags (Idle Line)\n");
  reset_test_state();

  atc_hdlc_context_t ctx;
  setup_test_context(&ctx);

  mock_output_len = 0;
  atc_hdlc_frame_t f = {.address=0xFF, .control.value=0x03, .information=NULL, .information_len=0};
  atc_hdlc_output_frame(&ctx, &f);
  
  int frame_len = mock_output_len;
  atc_hdlc_u8 frame_bytes[256];
  memcpy(frame_bytes, mock_output_buffer, frame_len);

  // Input: 7E 7E 7E [Frame] 7E 7E [Frame] 7E
  atc_hdlc_input_byte(&ctx, 0x7E);
  atc_hdlc_input_byte(&ctx, 0x7E);
  for(int i=0; i<frame_len; i++) atc_hdlc_input_byte(&ctx, frame_bytes[i]);
  atc_hdlc_input_byte(&ctx, 0x7E);
  atc_hdlc_input_byte(&ctx, 0x7E);
  for(int i=0; i<frame_len; i++) atc_hdlc_input_byte(&ctx, frame_bytes[i]);
  atc_hdlc_input_byte(&ctx, 0x7E);

  if (on_frame_call_count != 2) {
      char msg[64];
      sprintf(msg, "Expected 2 frames, got %d", on_frame_call_count);
      test_fail("Consecutive Flags", msg);
  }
  
  test_pass("Consecutive Flags");
}

/**
 * @brief Test: Rejection of frames smaller than minimum length.
 */
void test_min_size_rejection() {
  printf("TEST: Minimum Frame Size Rejection\n");
  reset_test_state();
  
  atc_hdlc_context_t ctx;
  setup_test_context(&ctx);

  // Min size is usually ~4 bytes (Addr+Ctrl+FCS). 
  // Send 0x7E 0xFF 0x7E (Too short)
  atc_hdlc_input_byte(&ctx, 0x7E);
  atc_hdlc_input_byte(&ctx, 0xFF); // Just address
  atc_hdlc_input_byte(&ctx, 0x7E);
  
  if (on_frame_call_count != 0) {
      test_fail("Min Size", "Short frame accepted");
  }
  
  test_pass("Minimum Frame Size Rejection");
}

/**
 * @brief Test: Aborted Frame handling (0x7E ... 0x7F ... 0x7E).
 *        If 0x7F was abort char (HDLC doesn't define standard 7F abort, but 7E terminates).
 *        Standard HDLC abort is 7-15 contiguous '1' bits, usually software uses Flag to reset.
 *        We simulate frame interruption by Flag.
 */
void test_aborted_frame() {
  printf("TEST: Aborted/Interrupted Frame\n");
  reset_test_state();
  
  atc_hdlc_context_t ctx;
  setup_test_context(&ctx);

  // Send start of frame: 7E Addr Ctrl Data...
  atc_hdlc_input_byte(&ctx, 0x7E);
  atc_hdlc_input_byte(&ctx, 0xFF);
  atc_hdlc_input_byte(&ctx, 0x00);
  atc_hdlc_input_byte(&ctx, 'A');
  atc_hdlc_input_byte(&ctx, 'B');
  
  // Then unexpected Flag (Terminates previous, starts new or empty)
  // The previous frame 'AB' is incomplete (no FCS). Should be discarded.
  atc_hdlc_input_byte(&ctx, 0x7E);
  
  if (on_frame_call_count != 0) {
      test_fail("Aborted Frame", "Incomplete frame reported as valid");
  }
  
  test_pass("Aborted/Interrupted Frame");
}

/**
 * @brief Test: CRC Error detection.
 *        Modifies one byte of a valid frame and expects rejection.
 */
void test_crc_error_injection() {
  printf("TEST: CRC Error Injection\n");
  reset_test_state();
  
  atc_hdlc_context_t ctx;
  setup_test_context(&ctx);

  // Generate valid frame
  atc_hdlc_frame_t f = {.address=0xFF, .control.value=0x00, .information=(atc_hdlc_u8*)"123", .information_len=3};
  atc_hdlc_output_frame(&ctx, &f);
  
  // Corrupt it: Flip bit in data
  // 7E Addr Ctrl [Data] FCS FCS 7E
  // 0  1    2     3...
  if (mock_output_len > 4) {
      mock_output_buffer[3] ^= 0xFF; // Invert first data byte
  }
  
  // Feed back
  int loop_len = mock_output_len;
  for(int i=0; i<loop_len; i++) atc_hdlc_input_byte(&ctx, mock_output_buffer[i]);
  
  if (on_frame_call_count != 0) {
      test_fail("CRC Error", "Corrupted frame accepted");
  }
  
  test_pass("CRC Error Injection");
}

/**
 * @brief Test: Input Buffer Overflow protection.
 *        Feeds a frame larger than the internal RX buffer.
 */
void test_input_buffer_overflow() {
  printf("TEST: Input Buffer Overflow\n");
  reset_test_state();
  
  atc_hdlc_context_t ctx;
  setup_test_context(&ctx); // uses mock_rx_buffer [16384]

  // Create a frame larger than buffer? Buffer is huge (16k in test).
  // Real world buffer might be small. 
  // We can't easily overflow 16k without looping huge data.
  // Instead, we initialize a context with SMALL buffer locally.
  
  atc_hdlc_u8 small_rx_buf[10];
  atc_hdlc_context_t small_ctx;
  atc_hdlc_init(&small_ctx, small_rx_buf, sizeof(small_rx_buf), 
                NULL, 0, 0, HDLC_DEFAULT_WINDOW_SIZE, 3,
                mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
  
  // Feed 20 bytes (Start + 20 bytes + End)
  atc_hdlc_input_byte(&small_ctx, 0x7E);
  for(int i=0; i<20; i++) atc_hdlc_input_byte(&small_ctx, 0xAA);
  atc_hdlc_input_byte(&small_ctx, 0x7E);
  
  // Should NOT callback (overflowed)
  if (on_frame_call_count != 0) {
      test_fail("Buffer Overflow", "Overflowed frame accepted");
  }
  
  test_pass("Input Buffer Overflow");
}

/**
 * @brief Test: Large Payload (Streaming).
 *        Tests streaming API for 1KB, 4KB, 8KB payloads.
 */
void test_streaming_large_payload(void) {
    printf("TEST: Streaming Large Payload\n");
    reset_test_state();

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);

    const int sizes[] = {1024, 4096, 8192};
    for (int s = 0; s < 3; s++) {
        int size = sizes[s];
        printf("   Testing payload size: %d bytes... ", size);
        
        atc_hdlc_output_frame_start(&ctx, 0xFF, 0x03);
        for (int i = 0; i < size; i++) {
            atc_hdlc_output_frame_information_byte(&ctx, (atc_hdlc_u8)(i & 0xFF));
        }
        atc_hdlc_output_frame_end(&ctx);

        // Feed back
        int loop_len = mock_output_len;
        for (int i = 0; i < loop_len; i++) {
            atc_hdlc_input_byte(&ctx, mock_output_buffer[i]);
        }

        if (on_frame_call_count != 1) {
            printf("[FAIL]\n");
            test_fail("Streaming", "Frame not received");
        }
        if (last_received_frame.information_len != size) {
            printf("[FAIL] Got %d\n", last_received_frame.information_len);
            test_fail("Streaming", "Length mismatch");
        }
        
        // Verify data content
        for(int i=0; i<size; i++) {
            if (last_received_frame.information[i] != (atc_hdlc_u8)(i & 0xFF)) {
                test_fail("Streaming", "Data corruption");
            }
        }

        printf("[OK]\n");
        reset_test_state(); // Reset for next size
    }
    
    test_pass("Streaming Large Payload");
}

// --- Control Field Tests ---

void test_control_field_i(void) {
  printf("TEST: Control Field Parsing (I-Frame)\n");
  reset_test_state();
  
  atc_hdlc_context_t ctx;
  setup_test_context(&ctx);

  /*
   * NOTE: We previously used loopbacks and the application callback for this test.
   * However, because the rigorous S-frame and sequence dispatcher drops unexpected 
   * or duplicate Sequence frames to protect the app, we bypass the context dispatcher 
   * here and test `atc_hdlc_frame_unpack` directly on the output buffer.
   */

  // Construct I-Frame: N(S)=3, N(R)=5, P=1.
  atc_hdlc_frame_t f = {.address=0xFF, .control=atc_hdlc_create_i_ctrl(3, 5, 1), .information=NULL, .information_len=0};
  
  mock_output_len = 0;
  atc_hdlc_output_frame(&ctx, &f);
  
  atc_hdlc_frame_t parsed_frame;
  atc_hdlc_u8 info_buf[256];
  if (atc_hdlc_frame_unpack(mock_output_buffer, mock_output_len, &parsed_frame, info_buf, sizeof(info_buf))) {
      if (parsed_frame.type == HDLC_FRAME_I &&
          parsed_frame.control.i_frame.ns == 3 &&
          parsed_frame.control.i_frame.pf == 1 &&
          parsed_frame.control.i_frame.nr == 5) {
          test_pass("Control Field I");
      } else {
          test_fail("Control Field I", "Parsed fields mismatch");
      }
  } else {
      test_fail("Control Field I", "Frame unpack failed");
  }
}

void test_control_field_s(void) {
  printf("TEST: Control Field Parsing (S-Frame)\n");
  reset_test_state();
  
  atc_hdlc_context_t ctx;
  setup_test_context(&ctx);

  // Construct S-Frame: REJ (S=10 -> 2), N(R)=7, P/F=0
  atc_hdlc_frame_t f = {.address=0xFF, .control=atc_hdlc_create_s_ctrl(0x02, 7, 0), .information=NULL, .information_len=0};
  
  atc_hdlc_output_frame(&ctx, &f);
  
  atc_hdlc_frame_t parsed_frame;
  atc_hdlc_u8 info_buf[256];
  if (atc_hdlc_frame_unpack(mock_output_buffer, mock_output_len, &parsed_frame, info_buf, sizeof(info_buf))) {
    if (parsed_frame.type == HDLC_FRAME_S &&
        parsed_frame.control.s_frame.s == 0x02 && // REJ
        parsed_frame.control.s_frame.nr == 7 &&
        parsed_frame.control.s_frame.pf == 0 &&
        atc_hdlc_get_s_frame_sub_type(&parsed_frame.control) == HDLC_S_FRAME_TYPE_REJ) {
      test_pass("Control Field S");
    } else {
      test_fail("Control Field S", "Parsed S-frame mismatch");
    }
  } else {
      test_fail("Control Field S", "Frame unpack failed");
  }
}

void test_ui_frame_transmission(void) {
    printf("TEST: UI Frame Transmission\n");
    reset_test_state();
    
    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02); // My=0x01, Peer=0x02

    const char *payload = "HELLO";
    bool res = atc_hdlc_output_frame_ui(&ctx, (const atc_hdlc_u8*)payload, 5);
    
    if (res && mock_output_len >= 11) {
         // Check Control Field for UI (0x03 or 0x13)
        // Addr=0x02 (Peer)
        if (mock_output_buffer[1] == 0x02 && (mock_output_buffer[2] & 0xEF) == 0x03) {
            test_pass("UI Frame Transmission");
        } else {
             test_fail("UI Frame Transmission", "Header mismatch");
        }
    } else {
        test_fail("UI Frame Transmission", "Send failed or length too short");
    }
}

void test_ui_frame_reception(void) {
    printf("TEST: UI Frame Reception\n");
    reset_test_state();
    
    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02); // My=0x01, Peer=0x02

    // Construct a valid UI frame addressed to ME (0x01)
    // Addr=0x01, Ctrl=0x03 (UI, P=0), Data="WORLD"
    atc_hdlc_output_frame_start(&ctx, 0x01, 0x03); 
    atc_hdlc_output_frame_information_bytes(&ctx, (atc_hdlc_u8*)"WORLD", 5);
    atc_hdlc_output_frame_end(&ctx);
    
    // Now Feed it back
    for (int i = 0, limit = mock_output_len; i < limit; i++) {
        atc_hdlc_input_byte(&ctx, mock_output_buffer[i]);
    }
    
    if (on_frame_call_count == 1 && 
        last_received_frame.type == HDLC_FRAME_U &&
        last_received_frame.address == 0x01 &&
        (last_received_frame.control.value & 0xEF) == 0x03 &&
        last_received_frame.information_len == 5 &&
        memcmp(last_received_frame.information, "WORLD", 5) == 0 &&
        atc_hdlc_get_u_frame_sub_type(&last_received_frame.control) == HDLC_U_FRAME_TYPE_UI) {
        test_pass("UI Frame Reception");
    } else {
        test_fail("UI Frame Reception", "Frame mismatch or not received");
    }
}

void test_test_frame(void) {
    printf("TEST: TEST Frame (Link Loopback)\n");
    
    reset_test_state();
    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);

    // --- 1. Send TEST command ---
    atc_hdlc_u8 test_data[] = "LOOPBACK";
    atc_hdlc_output_frame_test(&ctx, test_data, 8);

    if (mock_output_len == 0) test_fail("TEST Send", "No output produced");

    // Loopback
    reset_test_state(); // Clear counts for RX
    // Feed the output to input (simulate peer receiving it? No, simulate ME receiving it)
    // Wait, testing "Link Loopback" usually implies I send it, Peer echoes it.
    // Here, I am testing send/receive logic separately if I don't valid simulate peer logic.
    // But hdlc.c has hdlc_process_test which sends ECHO.
    // So if I feed a TEST frame to myself, I should reply with TEST response.
    
    // Create TEST command addressed to ME
    atc_hdlc_frame_t test_cmd = {
        .address = 0x01,
        .control = atc_hdlc_create_u_ctrl(0, 7, 1), // TEST P=1
        .information = (atc_hdlc_u8*)"PING",
        .information_len = 4,
        .type = HDLC_FRAME_U
    };
    
    // We need to pack it first
    atc_hdlc_u8 packed[256];
    atc_hdlc_u32 packed_len = 0;
    atc_hdlc_frame_pack(&test_cmd, packed, sizeof(packed), &packed_len);
    
    // Feed to input
    atc_hdlc_input_bytes(&ctx, packed, packed_len);
    
    // Verify:
    // 1. App callback? Usually TEST is processed internally and echoed.
    // Does hdlc_process_test trigger on_frame_cb? No, it just echoes.
    // So on_frame_call_count might be 0?
    // Let's check hdlc.c logic.
    // handle_u_frame -> hdlc_process_test.
    // It calls output_packet... to send response.
    // It does NOT call on_frame_cb.
    
    if (mock_output_len == 0) {
        test_fail("TEST Echo", "No echo response generated");
    }
    
    // Verify Echo content
    atc_hdlc_frame_t echo;
    atc_hdlc_u8 buf[256];
    if (atc_hdlc_frame_unpack(mock_output_buffer, mock_output_len, &echo, buf, sizeof(buf))) {
        if (echo.address == 0x01 && echo.information_len == 4 && memcmp(echo.information, "PING", 4) == 0) {
             test_pass("TEST Frame (Link Loopback)");
        } else {
             test_fail("TEST Echo", "Echo content mismatch");
        }
    } else {
        test_fail("TEST Echo", "Failed to unpack echo");
    }
}

int main(void) {
  printf("\n%sSTARTING HDLC CORE SUITE%s\n", COL_YELLOW, COL_RESET);
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
  test_streaming_large_payload();
  test_control_field_i();
  test_control_field_s();
  test_ui_frame_transmission();
  test_ui_frame_reception();
  test_test_frame();
  
  printf("\n%sALL HDLC CORE TESTS PASSED!%s\n", COL_GREEN, COL_RESET);
  return 0;
}
