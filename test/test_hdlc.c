#include "../inc/hdlc.h"
#include "../src/hdlc_frame.h"
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
    ctx.current_state = ATC_HDLC_STATE_CONNECTED; /* frame tests bypass state machine */

    atc_hdlc_u8 payload[] = "TEST";
    /* Address = my_address (0x01) so I-frame is accepted in CONNECTED state */
    frame_begin(&ctx, 0x01, 0x00);
    atc_hdlc_transmit_ui_data(&ctx, payload, 4);
    atc_hdlc_transmit_ui_end(&ctx);
    print_hexdump("Output Buffer", mock_output_buffer, mock_output_len);

    // Validate Output Size
    // Flag(1) + Addr(1) + Ctrl(1) + Data(4) + FCS(2) + Flag(1) = 10 bytes
    // If escaping occurs, it might be more. "TEST" = 54 45 53 54. No escaping needed.
    if (mock_output_len < 10) {
        test_fail("Basic Frame", "Output length too short");
    }

    // Loopback: Feed output to input
    int loop_len = mock_output_len;
    atc_hdlc_data_in(&ctx, mock_output_buffer, loop_len);

    // Verify Reception
    if (on_data_call_count != 1) {
        test_fail("Basic Frame", "Callback not triggered");
    }

    if (memcmp(last_data_payload, payload, 4) != 0) {
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
    ctx.current_state = ATC_HDLC_STATE_CONNECTED; /* frame tests bypass state machine */

    frame_begin(&ctx, 0x01, 0x00);
    atc_hdlc_transmit_ui_end(&ctx);

    int loop_len = mock_output_len;
    atc_hdlc_data_in(&ctx, mock_output_buffer, loop_len);

    if (on_data_call_count != 1) {
        test_fail("Empty Frame", "Frame not received");
    }
    if (last_data_len != 0) {
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
    ctx.current_state = ATC_HDLC_STATE_CONNECTED; /* frame tests bypass state machine */

    /* Use UI frames (0x03) — I-frame dispatcher would reject duplicate N(S)=0. */
    // Payload with special characters
    atc_hdlc_u8 special[] = {0x7E, 0x7D, 0x7E, 0x7D, 0x00, 0xFF, 0x7E};

    frame_begin(&ctx, 0xFF, 0x03);
    atc_hdlc_transmit_ui_data(&ctx, special, sizeof(special));
    atc_hdlc_transmit_ui_end(&ctx);
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
    atc_hdlc_data_in(&ctx, mock_output_buffer, loop_len);

    if (on_data_call_count != 1) {
        test_fail("Stuffing", "Frame decode failed");
    }

    if (last_data_len != sizeof(special) ||
        memcmp(last_data_payload, special, sizeof(special)) != 0) {
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
    ctx.current_state = ATC_HDLC_STATE_CONNECTED; /* frame tests bypass state machine */

    // Generate valid frame first
    mock_output_len = 0;
    frame_begin(&ctx, 0xFF, 0x03);
    atc_hdlc_transmit_ui_data(&ctx, (atc_hdlc_u8*)"DATA", 4);
    atc_hdlc_transmit_ui_end(&ctx);

    // Store valid frame bytes
    int valid_len = mock_output_len;
    atc_hdlc_u8 valid_bytes[256];
    memcpy(valid_bytes, mock_output_buffer, valid_len);

    // 1. Feed Garbage
    atc_hdlc_u8 noise[] = {0x00, 0x01, 0xFF, 0x55, 0xAA, 0x7D}; // No 0x7E
    atc_hdlc_data_in(&ctx, noise, sizeof(noise));

    // 2. Feed Valid Frame
    atc_hdlc_data_in(&ctx, valid_bytes, valid_len);

    // 3. Feed More Garbage
    atc_hdlc_data_in(&ctx, noise, sizeof(noise));

    if (on_data_call_count != 1) {
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
    ctx.current_state = ATC_HDLC_STATE_CONNECTED; /* frame tests bypass state machine */

    mock_output_len = 0;
    frame_begin(&ctx, 0xFF, 0x03);
    atc_hdlc_transmit_ui_end(&ctx);

    int frame_len = mock_output_len;
    atc_hdlc_u8 frame_bytes[256];
    memcpy(frame_bytes, mock_output_buffer, frame_len);

    // Input: 7E 7E 7E [Frame] 7E 7E [Frame] 7E
    atc_hdlc_u8 flag[] = {0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E};
    atc_hdlc_data_in(&ctx, flag, 2);
    atc_hdlc_data_in(&ctx, frame_bytes, frame_len);
    atc_hdlc_data_in(&ctx, flag, 2);
    atc_hdlc_data_in(&ctx, frame_bytes, frame_len);
    atc_hdlc_data_in(&ctx, flag, 1);

    if (on_data_call_count != 2) {
        char msg[64];
        sprintf(msg, "Expected 2 frames, got %d", on_data_call_count);
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
    ctx.current_state = ATC_HDLC_STATE_CONNECTED; /* frame tests bypass state machine */

    // Min size is usually ~4 bytes (Addr+Ctrl+FCS).
    // Send 0x7E 0xFF 0x7E (Too short)
    atc_hdlc_u8 short_packet[] = {0x7E, 0xFF, 0x7E}; // 0xFF is just address
    atc_hdlc_data_in(&ctx, short_packet, 3);

    if (on_data_call_count != 0) {
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
    ctx.current_state = ATC_HDLC_STATE_CONNECTED; /* frame tests bypass state machine */

    // Send start of frame: 7E Addr Ctrl Data...
    atc_hdlc_u8 packet[] = {0x7E, 0xFF, 0x00, 'A', 'B'};
    atc_hdlc_data_in(&ctx, packet, 5);

    // Then unexpected Flag (Terminates previous, starts new or empty)
    // The previous frame 'AB' is incomplete (no FCS). Should be discarded.
    atc_hdlc_data_in(&ctx, packet, 1); // 0x7E

    if (on_data_call_count != 0) {
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
    ctx.current_state = ATC_HDLC_STATE_CONNECTED; /* frame tests bypass state machine */

    // Generate valid frame
    frame_begin(&ctx, 0xFF, 0x00);
    atc_hdlc_transmit_ui_data(&ctx, (atc_hdlc_u8*)"123", 3);
    atc_hdlc_transmit_ui_end(&ctx);

    // Corrupt it: Flip bit in data
    // 7E Addr Ctrl [Data] FCS FCS 7E
    // 0  1    2     3...
    if (mock_output_len > 4) {
        mock_output_buffer[3] ^= 0xFF; // Invert first data byte
    }

    // Feed back
    int loop_len = mock_output_len;
    atc_hdlc_data_in(&ctx, mock_output_buffer, loop_len);

    if (on_data_call_count != 0) {
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
    setup_test_context(&ctx);
    ctx.current_state = ATC_HDLC_STATE_CONNECTED;
        /* frame tests bypass state machine */ // uses mock_rx_buffer [16384]

    // Create a frame larger than buffer? Buffer is huge (16k in test).
    // Real world buffer might be small.
    // We can't easily overflow 16k without looping huge data.
    // Instead, we initialize a context with SMALL buffer locally.

    /* RX buffer must hold: max_info_size(8) + addr(1)+ctrl(1)+fcs(2) = 12 bytes minimum */
    atc_hdlc_u8 small_rx_buf[12];
    atc_hdlc_context_t small_ctx;
    static const atc_hdlc_config_t small_cfg = {
        .mode = ATC_HDLC_MODE_ABM,
        .address = 0x01,
        .max_info_size = 8,
        .max_retries = 3,
        .t1_ms = 1000,
        .t2_ms = 10,
    };
    static const atc_hdlc_platform_t small_plat = {
        .on_send = mock_send_cb,
        .on_data = mock_on_data_cb,
        .on_event = NULL,
        .user_ctx = NULL,
    };
    atc_hdlc_rx_buffer_t small_rx = {.buffer = small_rx_buf, .capacity = sizeof(small_rx_buf)};
    atc_hdlc_params_t small_p = {
        .config = &small_cfg, .platform = &small_plat, .tx_window = NULL, .rx_buf = &small_rx};
    atc_hdlc_error_t init_err = atc_hdlc_init(&small_ctx, small_p);
    if (init_err != ATC_HDLC_OK)
        test_fail("Buffer Overflow", "small_ctx init failed unexpectedly");

    /* Feed 20 bytes of payload (exceeds small_ctx's 8-byte max_info_size) */
    atc_hdlc_u8 flag = 0x7E;
    atc_hdlc_u8 byte = 0xAA;
    atc_hdlc_data_in(&small_ctx, &flag, 1);
    for (int i = 0; i < 20; i++)
        atc_hdlc_data_in(&small_ctx, &byte, 1);
    atc_hdlc_data_in(&small_ctx, &flag, 1);

    /* Should NOT callback (overflowed) */
    if (on_data_call_count != 0) {
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
    ctx.current_state = ATC_HDLC_STATE_CONNECTED; /* frame tests bypass state machine */

    const int sizes[] = {1024, 4096, 8192};
    for (int s = 0; s < 3; s++) {
        int size = sizes[s];
        printf("   Testing payload size: %d bytes... ", size);

        atc_hdlc_transmit_ui_start(&ctx, 0xFF);
        for (int i = 0; i < size; i++) {
            atc_hdlc_u8 byte = (atc_hdlc_u8)(i & 0xFF);
            atc_hdlc_transmit_ui_data(&ctx, &byte, 1);
        }
        atc_hdlc_transmit_ui_end(&ctx);

        // Feed back
        int loop_len = mock_output_len;
        atc_hdlc_data_in(&ctx, mock_output_buffer, loop_len);

        if (on_data_call_count != 1) {
            printf("[FAIL]\n");
            test_fail("Streaming", "Frame not received");
        }
        if (last_data_len != size) {
            printf("[FAIL] Got %d\n", last_data_len);
            test_fail("Streaming", "Length mismatch");
        }

        // Verify data content
        for (int i = 0; i < size; i++) {
            if (last_data_payload[i] != (atc_hdlc_u8)(i & 0xFF)) {
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
    ctx.current_state = ATC_HDLC_STATE_CONNECTED; /* frame tests bypass state machine */

    /*
     * NOTE: We previously used loopbacks and the application callback for this test.
     * However, because the rigorous S-frame and sequence dispatcher drops unexpected
     * or duplicate Sequence frames to protect the app, we bypass the context dispatcher
     * here and test `atc_hdlc_frame_unpack` directly on the output buffer.
     */

    // Construct I-Frame: N(S)=3, N(R)=5, P=1.
    mock_output_len = 0;
    frame_begin(&ctx, 0xFF, I_CTRL(3, 5, 1));
    atc_hdlc_transmit_ui_end(&ctx);

    atc_hdlc_u8 info_buf[256];
    test_frame_t parsed_frame =
        test_unpack_frame(mock_output_buffer, mock_output_len, info_buf, sizeof(info_buf));
    if (parsed_frame.valid) {
        if (is_iframe(parsed_frame.control) && CTRL_NS(parsed_frame.control) == 3 &&
            CTRL_PF(parsed_frame.control) == 1 && CTRL_NR(parsed_frame.control) == 5) {
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
    ctx.current_state = ATC_HDLC_STATE_CONNECTED; /* frame tests bypass state machine */

    // Construct S-Frame: REJ (S=10 -> 2), N(R)=7, P/F=0
    frame_begin(&ctx, 0xFF, S_CTRL(0x02, 7, 0));
    atc_hdlc_transmit_ui_end(&ctx);

    atc_hdlc_u8 info_buf[256];
    test_frame_t parsed_frame =
        test_unpack_frame(mock_output_buffer, mock_output_len, info_buf, sizeof(info_buf));
    if (parsed_frame.valid) {
        if (is_sframe(parsed_frame.control) && CTRL_S(parsed_frame.control) == 0x02 && // REJ
            CTRL_NR(parsed_frame.control) == 7 && CTRL_PF(parsed_frame.control) == 0 &&
            CTRL_S(parsed_frame.control) == S_REJ) {
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
    ctx.current_state = ATC_HDLC_STATE_CONNECTED; /* frame tests bypass state machine */
    ctx.peer_address = 0x02;                      /* peer address set directly for test */

    const char* payload = "HELLO";
    atc_hdlc_error_t res =
        atc_hdlc_transmit_ui(&ctx, ATC_HDLC_BROADCAST_ADDRESS, (const atc_hdlc_u8*)payload, 5);

    if (res == ATC_HDLC_OK && mock_output_len >= 11) {
        // Check Control Field for UI (0x03 or 0x13)
        // Addr=0xFF (Broadcast)
        if (mock_output_buffer[1] == ATC_HDLC_BROADCAST_ADDRESS &&
            (mock_output_buffer[2] & 0xEF) == 0x03) {
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
    ctx.current_state = ATC_HDLC_STATE_CONNECTED; /* frame tests bypass state machine */
    ctx.peer_address = 0x02;                      /* peer address set directly for test */

    // Construct a valid UI frame addressed to ME (0x01)
    // Addr=0x01, Ctrl=0x03 (UI, P=0), Data="WORLD"
    atc_hdlc_transmit_ui_start(&ctx, 0x01);
    atc_hdlc_transmit_ui_data(&ctx, (atc_hdlc_u8*)"WORLD", 5);
    atc_hdlc_transmit_ui_end(&ctx);

    // Now Feed it back
    int loop_len = mock_output_len;
    atc_hdlc_data_in(&ctx, mock_output_buffer, loop_len);

    /* UI frame: payload should be delivered via on_data */
    if (on_data_call_count == 1 && last_data_len == 5 &&
        memcmp(last_data_payload, "WORLD", 5) == 0) {
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
    ctx.current_state = ATC_HDLC_STATE_CONNECTED; /* frame tests bypass state machine */
    ctx.peer_address = 0x02;                      /* peer address set directly for test */

    // --- 1. Send TEST command ---
    atc_hdlc_u8 test_data[] = "LOOPBACK";
    atc_hdlc_transmit_test(&ctx, ctx.peer_address, test_data, 8);

    if (mock_output_len == 0)
        test_fail("TEST Send", "No output produced");

    reset_test_state();
    /* Feed a TEST command addressed to me — expect echo response */
    // We need to pack it first
    atc_hdlc_u8 packed[256];
    int packed_len =
        test_pack_frame(0x01, U_CTRL(U_TEST, 1), (atc_hdlc_u8*)"PING", 4, packed, sizeof(packed));

    // Feed to input
    atc_hdlc_data_in(&ctx, packed, packed_len);

    if (mock_output_len == 0) {
        test_fail("TEST Echo", "No echo response generated");
    }

    // Verify Echo content
    atc_hdlc_u8 buf[256];
    test_frame_t echo = test_unpack_frame(mock_output_buffer, mock_output_len, buf, sizeof(buf));
    if (echo.valid) {
        if (echo.address == 0x01 && echo.info_len == 4 && memcmp(echo.info, "PING", 4) == 0) {
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
