#include "../inc/hdlc.h"
#include "../src/hdlc_private.h"
#include "test_common.h"
#include <stdint.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Scratch buffer for packing frames to feed into RX
static atc_hdlc_u8 temp_input_buffer[2048];

// --- Tests ---

/**
 * @brief Test: Reliable Transmission (I-Frame + ACK).
 *        Verifies I-frame sending, state update, and ACK reception.
 */
void test_reliable_transmission(void) {
    printf("TEST: Reliable Transmission (I-Frame + ACK)\n");
    reset_test_state();
    
    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    
    // Connect (simulate connected state)
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02); // Me=1, Peer=2
    ctx.current_state = ATC_HDLC_PROTOCOL_STATE_CONNECTED;
    
    // Send I-Frame
    mock_output_len = 0;
    atc_hdlc_u8 data[] = {0xAA, 0xBB};
    bool res = atc_hdlc_output_frame_i(&ctx, data, sizeof(data));
    
    if (!res) test_fail("Reliable I-Frame", "Failed to send");
    
    // Verify output (Check 3rd byte for Control 0x00? Need to decode to be sure)
    // 0x7E, Addr, Ctl ...
    if (mock_output_len > 0 && mock_output_buffer[2] == 0x00) {
         test_pass("Reliable I-Frame TX");
    } else {
         test_fail("Reliable I-Frame TX", "Incorrect output");
    }
    
    // Verify State (VS=1, Waiting=1)
    if (ctx.vs == 1 && ctx.va != ctx.vs) {
        test_pass("State Update");
    } else {
        test_fail("State Update", "VS/Waiting incorrect");
    }
    
    // Ack Reception (Peer sends I-Frame with NR=1, NS=0)
    reset_test_state();
    atc_hdlc_frame_t peer_frame = { .address=0x01, .control=atc_hdlc_create_i_ctrl(0, 1, 0), .information=NULL, .information_len=0 }; 
    atc_hdlc_u32 encoded_len = 0;
    atc_hdlc_frame_pack(&peer_frame, temp_input_buffer, sizeof(temp_input_buffer), &encoded_len);
    
    for(int i=0; i<encoded_len; i++) atc_hdlc_input_byte(&ctx, temp_input_buffer[i]);
    
    if (ctx.va == ctx.vs && ctx.vr == 1) { // VR increments because we received valid I-frame
        test_pass("ACK Reception");
    } else {
        test_fail("ACK Reception", "State mismatch after ACK");
    }
}

void test_reliable_retransmission(void) {
    printf("TEST: Reliable Retransmission (Timer)\n");
    reset_test_state();
    
    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = ATC_HDLC_PROTOCOL_STATE_CONNECTED;
    
    // Send I-Frame
    atc_hdlc_u8 data[] = {0xCA, 0xFE};
    atc_hdlc_output_frame_i(&ctx, data, sizeof(data));
    mock_output_len = 0; // Clear output
    
    // Tick Timer (1001 ticks of 1ms)
    for(int i=0; i<1001; i++) atc_hdlc_tick(&ctx);
    
    // Verify Retransmission Enquiry
    // 0x11 is S-frame, RR, P=1, N(R)=0. (b0=1, b1=0, s=0, p=1, nr=0) => 00010001 = 0x11
    if (mock_output_len > 0 && mock_output_buffer[2] == 0x11) { 
        test_pass("Retransmission Enquiry Sent");
    } else {
        test_fail("Retransmission Sent", "No Enquiry output or wrong control");
    }
}

void test_sequence_rollover(void) {
    printf("TEST: Sequence Number Rollover (0->7->0)\n");
    reset_test_state();
    
    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = ATC_HDLC_PROTOCOL_STATE_CONNECTED;
    
    atc_hdlc_u8 data[] = {0x00};
    
    for (int i = 0; i < 9; i++) {
        // 1. Send Frame i (VS = i % 8)
        atc_hdlc_output_frame_i(&ctx, data, 1);
        
        // Check V(S) incremented
        int expected_vs = (i + 1) % 8;
        if (ctx.vs != expected_vs) {
             test_fail("Sequence Rollover", "VS mismatch");
             printf("   Expected VS: %d, Got: %d (Iter %d)\n", expected_vs, ctx.vs, i);
             return;
        }
        
        // 2. Acknowledge it (Peer sends RR with NR = expected_vs)
        atc_hdlc_frame_t rr_frame;
        rr_frame.address = 0x01;
        rr_frame.control = atc_hdlc_create_s_ctrl(0, expected_vs, 0); // RR, NR=VS
        rr_frame.information = NULL;
        rr_frame.information_len = 0;
        rr_frame.type = ATC_HDLC_FRAME_S;
        
        atc_hdlc_u32 len = 0;
        atc_hdlc_frame_pack(&rr_frame, temp_input_buffer, sizeof(temp_input_buffer), &len);
        atc_hdlc_input_bytes(&ctx, temp_input_buffer, len);
        
        if (ctx.va != ctx.vs) {
             test_fail("Sequence Rollover", "Failed to ACK");
             return;
        }
    }
    test_pass("Sequence Rollover");
}

void test_duplicate_ack_ignored(void) {
    printf("TEST: Duplicate ACK Ignored\n");
    reset_test_state();
    
    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = ATC_HDLC_PROTOCOL_STATE_CONNECTED;
    
    // Send I-Frame (VS becomes 1)
    atc_hdlc_output_frame_i(&ctx, (atc_hdlc_u8*)"A", 1);
    
    // Receive ACK (RR NR=1)
    atc_hdlc_frame_t rr_frame = { .address=0x01, .control=atc_hdlc_create_s_ctrl(0, 1, 0) };
    atc_hdlc_u32 len = 0;
    atc_hdlc_frame_pack(&rr_frame, temp_input_buffer, sizeof(temp_input_buffer), &len);
    atc_hdlc_input_bytes(&ctx, temp_input_buffer, len);
    
    if (ctx.va != ctx.vs) test_fail("Duplicate ACK", "First ACK failed");
    
    // Receive SAME ACK again
    atc_hdlc_input_bytes(&ctx, temp_input_buffer, len);
    
    // Should stay not waiting, no error
    if (ctx.va != ctx.vs) test_fail("Duplicate ACK", "State regressed?");
    
    test_pass("Duplicate ACK Ignored");
}

void test_rej_retransmit(void) {
    printf("TEST: REJ Triggers Retransmission\n");
    reset_test_state();
    
    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = ATC_HDLC_PROTOCOL_STATE_CONNECTED;
    
    // Send I-Frame [VS=0] -> VS becomes 1
    atc_hdlc_output_frame_i(&ctx, (atc_hdlc_u8*)"XY", 2);
    mock_output_len = 0; // Clear output
    
    // Receive REJ [NR=0] (Peer asking for frame 0 again)
    atc_hdlc_frame_t rej_frame = { .address=0x01, .control=atc_hdlc_create_s_ctrl(2, 0, 0) }; // REJ=2
    atc_hdlc_u32 len = 0;
    atc_hdlc_frame_pack(&rej_frame, temp_input_buffer, sizeof(temp_input_buffer), &len);
    atc_hdlc_input_bytes(&ctx, temp_input_buffer, len);
    
    // Verify we retransmitted
    if (mock_output_len > 0) {
        test_pass("REJ Triggered Retransmit");
    } else {
        test_fail("REJ Triggered Retransmit", "No output generated");
    }
}

void test_piggyback_ack(void) {
    printf("TEST: Piggyback ACK via I-Frame N(R)\n");
    reset_test_state();
    
    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = ATC_HDLC_PROTOCOL_STATE_CONNECTED;

    // --- Phase 1: Outgoing Piggyback ---
    // Receive an I-frame from peer: N(S)=0, N(R)=0 (peer expects our frame 0)
    printf("\n--- Phase 1: Outgoing Piggyback ---\n");
    
    // Build peer's I-frame: Addr=0x01 (to me), Ctrl: I-frame N(S)=0, N(R)=0, P=0
    atc_hdlc_frame_t peer_frame = {
        .address = 0x01,
        .control = atc_hdlc_create_i_ctrl(0, 0, 0),
        .information = (atc_hdlc_u8 *)"HI",
        .information_len = 2
    };

    // Encode and feed peer's frame
    atc_hdlc_u32 encoded_len = 0;
    atc_hdlc_frame_pack(&peer_frame, temp_input_buffer, sizeof(temp_input_buffer), &encoded_len);
    atc_hdlc_input_bytes(&ctx, temp_input_buffer, encoded_len);

    // After receiving, V(R) should be 1
    if (ctx.vr != 1) {
        test_fail("Piggyback", "V(R) not 1");
        return;
    }

    // Send our own I-frame — this embeds N(R)=V(R)=1 as the piggyback ACK
    mock_output_len = 0;
    atc_hdlc_u8 our_data[] = "OK";
    bool sent = atc_hdlc_output_frame_i(&ctx, our_data, sizeof(our_data) - 1);
    if (!sent) {
        test_fail("Piggyback", "output_i returned false");
        return;
    }

    // Verify: Our VS should have incremented and ack_timer should be 0 (piggybacked)
    if (ctx.ack_timer == 0) {
        test_pass("Piggyback: ack_timer cleared by outgoing I-frame");
    } else {
        test_fail("Piggyback", "ack_timer not cleared");
    }

    // --- Phase 2: Incoming Piggyback ---
    printf("\n--- Phase 2: Incoming Piggyback ---\n");

    if (ctx.va == ctx.vs) {
        test_fail("Piggyback", "Expected va != vs");
        return;
    }

    // Peer sends I-frame: N(S)=1 (next seq), N(R)=1 (ACKing our frame 0)
    atc_hdlc_frame_t peer_frame2 = {
        .address = 0x01,
        .control = atc_hdlc_create_i_ctrl(1, 1, 0), // N(S)=1, N(R)=1=our VS
        .information = (atc_hdlc_u8 *)"RE",
        .information_len = 2
    };

    atc_hdlc_frame_pack(&peer_frame2, temp_input_buffer, sizeof(temp_input_buffer), &encoded_len);
    atc_hdlc_input_bytes(&ctx, temp_input_buffer, encoded_len);

    // After processing, all frames should be acknowledged (va == vs)
    if (ctx.va == ctx.vs) {
        test_pass("Piggyback: Incoming I-frame N(R) acknowledged all frames");
    } else {
        test_fail("Piggyback", "va != vs, frames still outstanding");
    }

    if (ctx.vr == 2) {
        test_pass("Piggyback: V(R) updated");
    } else {
        test_fail("Piggyback", "V(R) mismatch");
    }
}

/**
 * @brief Test: Window Size 2 — Send 2 I-frames, ACK both with single RR.
 */
void test_window_size_2_basic(void) {
    printf("\nTEST: Window Size 2 — Basic\n");
    reset_test_state();
    
    atc_hdlc_context_t ctx;
    // Manual init for custom window size 2
    // We use mock buffers from common where possible, but context needs its own pointers if we don't use setup_test_context
    // We can use mock_rx_buffer for rx
    atc_hdlc_u8 retx_buf[128];
    atc_hdlc_init(&ctx, mock_rx_buffer, sizeof(mock_rx_buffer), retx_buf, sizeof(retx_buf), ATC_HDLC_DEFAULT_RETRANSMIT_TIMEOUT, ATC_HDLC_DEFAULT_ACK_DELAY_TIMEOUT, 2, 3, mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = ATC_HDLC_PROTOCOL_STATE_CONNECTED;

    // 1. Send first I-frame (N(S)=0)
    atc_hdlc_u8 data1[] = "FRAME1";
    bool ok1 = atc_hdlc_output_frame_i(&ctx, data1, sizeof(data1));
    if (!ok1) { test_fail("Window2", "Send first failed"); return; }
    if (ctx.vs != 1) { test_fail("Window2", "VS mismatch 1"); return; }

    // 2. Send second I-frame (N(S)=1) — window still open
    atc_hdlc_u8 data2[] = "FRAME2";
    bool ok2 = atc_hdlc_output_frame_i(&ctx, data2, sizeof(data2));
    if (!ok2) { test_fail("Window2", "Send second failed"); return; }
    if (ctx.vs != 2) { test_fail("Window2", "VS mismatch 2"); return; }

    // 3. Window should now be full (2 outstanding, window_size=2)
    atc_hdlc_u8 data3[] = "BLOCKED";
    bool ok3 = atc_hdlc_output_frame_i(&ctx, data3, sizeof(data3));
    if (ok3) { test_fail("Window2", "Window overflow allowed!"); return; }
    test_pass("Window2: Window full blocked send");

    // 4. ACK both with RR N(R)=2
    atc_hdlc_frame_t rr_frame = { .address=0x01, .control=atc_hdlc_create_s_ctrl(0, 2, 0) };
    atc_hdlc_u32 len = 0;
    atc_hdlc_frame_pack(&rr_frame, temp_input_buffer, sizeof(temp_input_buffer), &len);
    atc_hdlc_input_bytes(&ctx, temp_input_buffer, len);

    if (ctx.va == ctx.vs) {
        test_pass("Window2: Cumulative ACK cleared");
    } else {
        test_fail("Window2", "Cumulative ACK failed");
        return;
    }

    // 5. Should be able to send again now
    bool ok4 = atc_hdlc_output_frame_i(&ctx, data3, sizeof(data3));
    if (!ok4) { test_fail("Window2", "Send after ACK failed"); return; }
    test_pass("Window2: Window reopened");
}

/**
 * @brief Test: Go-Back-N Retransmission on timeout.
 *        Send 3 frames with window=3, then trigger timeout — all 3 must retransmit.
 */
void test_gobackn_retransmit(void) {
    printf("\nTEST: Go-Back-N Retransmission (Timeout)\n");
    reset_test_state();
    
    atc_hdlc_context_t ctx;
    atc_hdlc_u8 retx_buf[192];
    // Manual init for custom window size 3 and timeout 500
    atc_hdlc_init(&ctx, mock_rx_buffer, sizeof(mock_rx_buffer), retx_buf, sizeof(retx_buf), 500, ATC_HDLC_DEFAULT_ACK_DELAY_TIMEOUT, 3, 3, mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = ATC_HDLC_PROTOCOL_STATE_CONNECTED;

    // Send 3 I-frames
    atc_hdlc_u8 d1[] = "AAA";
    atc_hdlc_u8 d2[] = "BBB";
    atc_hdlc_u8 d3[] = "CCC";
    atc_hdlc_output_frame_i(&ctx, d1, sizeof(d1));
    atc_hdlc_output_frame_i(&ctx, d2, sizeof(d2));
    atc_hdlc_output_frame_i(&ctx, d3, sizeof(d3));

    if (ctx.vs != 3 || ctx.va != 0) {
        test_fail("GBN Retransmit", "Setup failed");
        return;
    }

    // Record output frame count before timeout
    atc_hdlc_u32 frames_before = ctx.stats_output_frames;

    // Trigger timeout (500 ticks)
    for(int _t=0; _t<500; _t++) atc_hdlc_tick(&ctx);

    atc_hdlc_u32 enquiry_frames = ctx.stats_output_frames - frames_before;
    if (enquiry_frames == 1) {
        test_pass("GBN Retransmit: Enquiry RR Sent");
    } else {
        test_fail("GBN Retransmit", "Enquiry RR not sent on timeout");
        return;
    }

    // Now peer replies with RR and F=1 (Response uses peer's own address = 0x02)
    atc_hdlc_frame_t f1_response = { .address=0x02, .control=atc_hdlc_create_s_ctrl(0x00, 0, 1) }; // RR, NR=0, F=1
    atc_hdlc_u32 rr_len = 0;
    atc_hdlc_frame_pack(&f1_response, temp_input_buffer, sizeof(temp_input_buffer), &rr_len);
    
    frames_before = ctx.stats_output_frames;
    atc_hdlc_input_bytes(&ctx, temp_input_buffer, rr_len);

    atc_hdlc_u32 retransmitted = ctx.stats_output_frames - frames_before;
    if (retransmitted == 3) {
        test_pass("GBN Retransmit: All 3 frames retransmitted after F=1");
    } else {
        printf("  Expected 3 retransmitted, got %u\n", retransmitted);
        test_fail("GBN Retransmit", "Wrong retransmit count");
        return;
    }

    // va and vs should remain unchanged
    if (ctx.vs == 3 && ctx.va == 0) {
        test_pass("GBN Retransmit: State preserved");
    } else {
        test_fail("GBN Retransmit", "State corrupted");
    }

    // Timer should have been restarted
    if (ctx.retransmit_timer == 500) {
        test_pass("GBN Retransmit: Timer restarted");
    } else {
        test_fail("GBN Retransmit", "Timer check failed");
    }
}

/**
 * @brief Test: Window Size 7 — Full window + REJ on middle frame.
 *        Sends 7 I-frames (filling the window completely), then the peer
 *        sends REJ for frame #3, triggering Go-Back-N retransmission of
 *        frames 3, 4, 5, 6 (4 frames total).
 */
void test_window7_mid_rej(void) {
    printf("\nTEST: Window Size 7 — Mid-Window REJ\n");
    reset_test_state();

    atc_hdlc_context_t ctx;
    atc_hdlc_u8 retx_buf[512];
    atc_hdlc_init(&ctx, mock_rx_buffer, sizeof(mock_rx_buffer),
                  retx_buf, sizeof(retx_buf),
                  ATC_HDLC_DEFAULT_RETRANSMIT_TIMEOUT, ATC_HDLC_DEFAULT_ACK_DELAY_TIMEOUT, 7,
                  3,
                  mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = ATC_HDLC_PROTOCOL_STATE_CONNECTED;

    // --- Phase 1: Send 7 I-frames (fill the entire window) ---
    printf("   Phase 1: Sending 7 I-frames...\n");
    char payload[8];
    for (int i = 0; i < 7; i++) {
        sprintf(payload, "PKT_%d", i);
        bool ok = atc_hdlc_output_frame_i(&ctx, (atc_hdlc_u8 *)payload, (atc_hdlc_u16)strlen(payload));
        if (!ok) {
            char msg[64];
            sprintf(msg, "Failed to send frame %d", i);
            test_fail("Window7 REJ", msg);
            return;
        }
    }

    // Verify: VS should be 7, VA should be 0 (nothing ACK'd yet)
    if (ctx.vs != 7) {
        printf("   Expected VS=7, got VS=%d\n", ctx.vs);
        test_fail("Window7 REJ", "VS mismatch after sending 7 frames");
        return;
    }
    if (ctx.va != 0) {
        test_fail("Window7 REJ", "VA should be 0 (no ACKs received)");
        return;
    }
    test_pass("Window7 REJ: All 7 frames sent successfully");

    // --- Phase 2: Window should be full (8th frame must be blocked) ---
    printf("   Phase 2: Verifying window is full...\n");
    bool overflow = atc_hdlc_output_frame_i(&ctx, (atc_hdlc_u8 *)"BLOCKED", 7);
    if (overflow) {
        test_fail("Window7 REJ", "Window overflow allowed — 8th frame should be blocked!");
        return;
    }
    test_pass("Window7 REJ: Window full, 8th frame correctly blocked");

    // --- Phase 3: Peer sends REJ N(R)=3 (requesting retransmission from frame 3) ---
    printf("   Phase 3: Peer sends REJ N(R)=3...\n");
    mock_output_len = 0; // Clear TX buffer to capture retransmissions
    atc_hdlc_u32 frames_before = ctx.stats_output_frames;

    atc_hdlc_frame_t rej_frame = {
        .address = 0x01,
        .control = atc_hdlc_create_s_ctrl(0x02, 3, 0), // REJ, N(R)=3, F=0
        .information = NULL,
        .information_len = 0,
        .type = ATC_HDLC_FRAME_S
    };

    atc_hdlc_u32 rej_len = 0;
    atc_hdlc_frame_pack(&rej_frame, temp_input_buffer, sizeof(temp_input_buffer), &rej_len);
    atc_hdlc_input_bytes(&ctx, temp_input_buffer, rej_len);

    // REJ N(R)=3 means:
    //   - Frames 0, 1, 2 are ACK'd (VA advances to 3)
    //   - Frames 3, 4, 5, 6 must be retransmitted (Go-Back-N)
    atc_hdlc_u32 retransmitted = ctx.stats_output_frames - frames_before;

    printf("   Retransmitted frame count: %u (expected 4)\n", retransmitted);

    if (ctx.va != 3) {
        printf("   Expected VA=3, got VA=%d\n", ctx.va);
        test_fail("Window7 REJ", "VA not advanced to 3 after REJ");
        return;
    }
    test_pass("Window7 REJ: VA advanced to 3 (frames 0-2 acknowledged)");

    if (retransmitted != 4) {
        printf("   Expected 4 retransmissions, got %u\n", retransmitted);
        test_fail("Window7 REJ", "Wrong retransmit count");
        return;
    }
    test_pass("Window7 REJ: Frames 3-6 retransmitted (4 frames)");

    // VS should remain 7 (no new frames sent, just retransmissions)
    if (ctx.vs != 7) {
        test_fail("Window7 REJ", "VS changed after retransmission");
        return;
    }
    test_pass("Window7 REJ: VS preserved at 7");

    // --- Phase 4: ACK all remaining frames, verify window reopens ---
    printf("   Phase 4: Peer ACKs all frames (RR N(R)=7)...\n");
    atc_hdlc_frame_t rr_frame = {
        .address = 0x01,
        .control = atc_hdlc_create_s_ctrl(0x00, 7, 0), // RR, N(R)=7, F=0
        .information = NULL,
        .information_len = 0,
        .type = ATC_HDLC_FRAME_S
    };

    atc_hdlc_u32 rr_len = 0;
    atc_hdlc_frame_pack(&rr_frame, temp_input_buffer, sizeof(temp_input_buffer), &rr_len);
    atc_hdlc_input_bytes(&ctx, temp_input_buffer, rr_len);

    if (ctx.va != ctx.vs) {
        test_fail("Window7 REJ", "VA != VS after full ACK");
        return;
    }
    test_pass("Window7 REJ: All frames acknowledged (VA == VS == 7)");

    // Window should be open again — sending a new frame should succeed
    bool reopened = atc_hdlc_output_frame_i(&ctx, (atc_hdlc_u8 *)"REOPEN", 6);
    if (!reopened) {
        test_fail("Window7 REJ", "Window did not reopen after full ACK");
        return;
    }
    test_pass("Window7 REJ: Window reopened, new frame sent successfully");
}

/**
 * @brief Throughput Benchmark: Window Size 1 vs 7.
 *        Sends 50 I-frame chunks (64 bytes each) with simulated round-trip
 *        ACKs. Compares the number of round-trips and total TX bytes needed
 *        for each window size to demonstrate the efficiency gain of larger
 *        sliding windows.
 */

#define BENCH_TOTAL_CHUNKS   50
#define BENCH_CHUNK_SIZE     64

typedef struct {
    int window_size;
    int round_trips;
    int total_tx_bytes;
    int frames_sent;
} bench_result_t;

static bench_result_t run_throughput_bench(int window_size) {
    bench_result_t result = {0};
    result.window_size = window_size;

    reset_test_state();

    atc_hdlc_context_t ctx;
    atc_hdlc_u8 retx_buf[4096];
    atc_hdlc_init(&ctx, mock_rx_buffer, sizeof(mock_rx_buffer),
                  retx_buf, sizeof(retx_buf),
                  ATC_HDLC_DEFAULT_RETRANSMIT_TIMEOUT, ATC_HDLC_DEFAULT_ACK_DELAY_TIMEOUT, (atc_hdlc_u8)window_size,
                  3,
                  mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = ATC_HDLC_PROTOCOL_STATE_CONNECTED;

    // Prepare chunk payload (repeating pattern)
    atc_hdlc_u8 chunk[BENCH_CHUNK_SIZE];
    for (int i = 0; i < BENCH_CHUNK_SIZE; i++) chunk[i] = (atc_hdlc_u8)(i & 0xFF);

    int sent = 0;

    while (sent < BENCH_TOTAL_CHUNKS) {
        // Send as many frames as the window allows
        int batch = 0;
        while (sent < BENCH_TOTAL_CHUNKS) {
            bool ok = atc_hdlc_output_frame_i(&ctx, chunk, BENCH_CHUNK_SIZE);
            if (!ok) break; // Window full
            sent++;
            batch++;
        }

        if (batch == 0) {
            // Should not happen if we ACK properly - safety break
            break;
        }

        // Simulate one round-trip: peer ACKs all outstanding frames
        result.round_trips++;

        atc_hdlc_frame_t rr_frame = {
            .address = 0x01,
            .control = atc_hdlc_create_s_ctrl(0x00, ctx.vs, 0), // RR, N(R)=VS
            .information = NULL,
            .information_len = 0,
            .type = ATC_HDLC_FRAME_S
        };

        atc_hdlc_u32 rr_len = 0;
        atc_hdlc_frame_pack(&rr_frame, temp_input_buffer, sizeof(temp_input_buffer), &rr_len);
        atc_hdlc_input_bytes(&ctx, temp_input_buffer, rr_len);
    }

    result.total_tx_bytes = mock_output_len;
    result.frames_sent = sent;
    return result;
}

void test_throughput_benchmark(void) {
    printf("\nTEST: Throughput Benchmark — All Window Sizes (1-7)\n");
    printf("   Payload: %d chunks x %d bytes = %d bytes total\n\n",
           BENCH_TOTAL_CHUNKS, BENCH_CHUNK_SIZE, BENCH_TOTAL_CHUNKS * BENCH_CHUNK_SIZE);

    bench_result_t results[7];

    // --- Run benchmark for each window size ---
    for (int w = 1; w <= 7; w++) {
        printf("   %s[Window Size = %d]%s Running... ", COL_CYAN, w, COL_RESET);
        results[w - 1] = run_throughput_bench(w);
        printf("Frames: %d | Round-trips: %d | TX Bytes: %d\n",
               results[w - 1].frames_sent, results[w - 1].round_trips, results[w - 1].total_tx_bytes);
    }
    printf("\n");

    // --- Comparison Table ---
    printf("   %s┌────────────────────────────────────────────────────────────────────┐%s\n", COL_YELLOW, COL_RESET);
    printf("   %s│              THROUGHPUT COMPARISON — ALL WINDOW SIZES              │%s\n", COL_YELLOW, COL_RESET);
    printf("   %s├──────────┬──────────────┬──────────────┬───────────┬───────────────┤%s\n", COL_YELLOW, COL_RESET);
    printf("   %s│  Window  │  Frames Sent │  Round-Trips │  TX Bytes │  Speedup      │%s\n", COL_YELLOW, COL_RESET);
    printf("   %s├──────────┼──────────────┼──────────────┼───────────┼───────────────┤%s\n", COL_YELLOW, COL_RESET);

    for (int w = 0; w < 7; w++) {
        float speedup = (results[0].round_trips > 0)
            ? (float)results[0].round_trips / (float)results[w].round_trips
            : 0.0f;

        if (w == 0) {
            printf("   %s│    %d     │     %3d      │     %3d      │   %5d   │  (baseline)   │%s\n",
                   COL_YELLOW, results[w].window_size, results[w].frames_sent,
                   results[w].round_trips, results[w].total_tx_bytes, COL_RESET);
        } else {
            printf("   %s│    %d     │     %3d      │     %3d      │   %5d   │%s  %s%.1fx faster%s  %s│%s\n",
                   COL_YELLOW, results[w].window_size, results[w].frames_sent,
                   results[w].round_trips, results[w].total_tx_bytes, COL_RESET,
                   COL_GREEN, speedup, COL_RESET,
                   COL_YELLOW, COL_RESET);
        }
    }

    printf("   %s└──────────┴──────────────┴──────────────┴───────────┴───────────────┘%s\n\n", COL_YELLOW, COL_RESET);

    // --- Assertions ---
    for (int w = 0; w < 7; w++) {
        int ws = w + 1;
        if (results[w].frames_sent != BENCH_TOTAL_CHUNKS) {
            char msg[100];
            sprintf(msg, "W=%d did not send all %d frames (got %d)", ws, BENCH_TOTAL_CHUNKS, results[w].frames_sent);
            test_fail("Throughput Bench", msg);
            return;
        }

        // Expected round-trips = ceil(50 / window_size)
        int expected_rt = (BENCH_TOTAL_CHUNKS + ws - 1) / ws;
        if (results[w].round_trips != expected_rt) {
            char msg[100];
            sprintf(msg, "W=%d expected %d round-trips, got %d", ws, expected_rt, results[w].round_trips);
            test_fail("Throughput Bench", msg);
            return;
        }
    }

    // Larger windows must always use fewer or equal round-trips
    for (int w = 1; w < 7; w++) {
        if (results[w].round_trips > results[w - 1].round_trips) {
            char msg[100];
            sprintf(msg, "W=%d slower than W=%d!", w + 1, w);
            test_fail("Throughput Bench", msg);
            return;
        }
    }

    test_pass("Throughput Benchmark: All window sizes validated (W=1..7)");
}
typedef enum {
    TX_IDLE,
    TX_SENDING,
    TX_DONE
} TxState_t;

TxState_t process_tx_task(atc_hdlc_context_t *ctx, const uint8_t* data, size_t total_size, size_t max_chunk_size) {
    static size_t bytes_sent = 0; // Durumu korumak için static değişken

    if (total_size == 0) return TX_IDLE;

    // Tüm veri gönderildiyse sıfırla ve bitir
    if (bytes_sent >= total_size) {
        bytes_sent = 0; 
        return TX_DONE;
    }

    size_t chunk_size = total_size - bytes_sent;
    if (chunk_size > max_chunk_size) {
        chunk_size = max_chunk_size;
    }

    // Gönderim başarılı olursa veriyi ilerlet
    if (atc_hdlc_output_frame_i(ctx, (atc_hdlc_u8*)&data[bytes_sent], (atc_hdlc_u16)chunk_size)) {
        bytes_sent += chunk_size;
        
        // Bu paketten sonra veri bittiyse anında DONE dönebiliriz
        if (bytes_sent >= total_size) {
            bytes_sent = 0;
            return TX_DONE;
        }
    }

    // Gönderim başarısızsa (ACK bekleniyorsa) işlemciyi meşgul etmeden
    // fonksiyondan çıkar. Bir sonraki ana döngüde aynı paketi tekrar dener.
    return TX_SENDING;
}

void test_process_tx_task_simulation(void) {
    printf("\nTEST: Process TX Task Simulation\n");
    reset_test_state();
    
    atc_hdlc_context_t ctx;
    atc_hdlc_u8 retx_buf[256];
    // We use window size 2 so the task hits a "Window Full" state and returns TX_SENDING
    atc_hdlc_init(&ctx, mock_rx_buffer, sizeof(mock_rx_buffer), 
                  retx_buf, sizeof(retx_buf), 
                  ATC_HDLC_DEFAULT_RETRANSMIT_TIMEOUT, ATC_HDLC_DEFAULT_ACK_DELAY_TIMEOUT, 2, 
                  3,
                  mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = ATC_HDLC_PROTOCOL_STATE_CONNECTED;

    // We have 100 bytes. Chunk size is 32. It should take 4 chunks (32, 32, 32, 4)
    uint8_t test_data[100];
    for (int i = 0; i < 100; i++) test_data[i] = (uint8_t)(i & 0xFF);

    TxState_t state = TX_IDLE;
    int loop_count = 0;
    while (state != TX_DONE && loop_count < 1000) {
        state = process_tx_task(&ctx, test_data, sizeof(test_data), 32);
        
        // Simüle edilmiş peer: Window dolduğunda ve TX_SENDING döndüğünde (veya her döngüde)
        // Eğer ctx'te bekleyen ACK varsa, bir ACK frame üretip kontekste verelim.
        if (ctx.vs != ctx.va) {
            atc_hdlc_frame_t rr_frame = {
                .address = 0x01,
                .control = atc_hdlc_create_s_ctrl(0x00, ctx.vs, 0), // RR, N(R)=VS 
                .information = NULL,
                .information_len = 0,
                .type = ATC_HDLC_FRAME_S
            };
            atc_hdlc_u32 rr_len = 0;
            atc_hdlc_frame_pack(&rr_frame, temp_input_buffer, sizeof(temp_input_buffer), &rr_len);
            atc_hdlc_input_bytes(&ctx, temp_input_buffer, rr_len);
        }
        loop_count++;
    }

    if (state == TX_DONE) {
        test_pass("Process TX Task Simulation - Transmission");
    } else {
        test_fail("Process TX Task Simulation - Transmission", "Task did not complete within loop limit");
    }

    // Verify reset to idle
    if (process_tx_task(&ctx, test_data, 0, 32) == TX_IDLE) {
        test_pass("Process TX Task Simulation - Reset check");
    } else {
        test_fail("Process TX Task Simulation - Reset check", "Did not return TX_IDLE for 0 size");
    }
}

void test_nr_modulo_validation(void) {
    printf("\nTEST: N(R) Modulo Validation Bug\n");
    reset_test_state();

    atc_hdlc_context_t ctx;
    atc_hdlc_u8 retx_buf[256];
    atc_hdlc_init(&ctx, mock_rx_buffer, sizeof(mock_rx_buffer), 
                  retx_buf, sizeof(retx_buf), 
                  ATC_HDLC_DEFAULT_RETRANSMIT_TIMEOUT, ATC_HDLC_DEFAULT_ACK_DELAY_TIMEOUT, 7, 
                  3, mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = ATC_HDLC_PROTOCOL_STATE_CONNECTED;

    // 1. Send 6 frames (0 to 5)
    char payload[8] = "DATA";
    for (int i = 0; i < 6; i++) {
        atc_hdlc_output_frame_i(&ctx, (atc_hdlc_u8 *)payload, 4);
    }
    // 2. Peer ACKs them: N(R)=6
    atc_hdlc_frame_t rr_frame = { .address=0x01, .control=atc_hdlc_create_s_ctrl(0x00, 6, 0) };
    atc_hdlc_u32 len = 0;
    atc_hdlc_frame_pack(&rr_frame, temp_input_buffer, sizeof(temp_input_buffer), &len);
    atc_hdlc_input_bytes(&ctx, temp_input_buffer, len);
    
    if (ctx.va != 6 || ctx.vs != 6) { test_fail("NR Bug Test", "Setup failed"); return; }
    
    // 3. Send 2 more frames (Seq 6, and Seq 7)
    atc_hdlc_output_frame_i(&ctx, (atc_hdlc_u8 *)"DATA6", 5);
    atc_hdlc_output_frame_i(&ctx, (atc_hdlc_u8 *)"DATA7", 5);
    
    // Now V(A)=6, V(S)=0 (wrap around). Outstanding: 6, 7.
    if (ctx.va != 6 || ctx.vs != 0) { test_fail("NR Bug Test", "Wrap around setup failed"); return; }

    // Tick the timer so we can observe if it resets
    for(int _t=0; _t<100; _t++) atc_hdlc_tick(&ctx);
    atc_hdlc_u32 timer_before = ctx.retransmit_timer;

    // 4. Peer sends RR N(R)=6. This is perfectly valid (peer acknowledging up to 5, waiting for 6)
    // If hdlc_nr_valid has the bug, it will reject N(R)=6 because it thinks 6 is outside [6, 0].
    atc_hdlc_frame_t rr_frame2 = { .address=0x01, .control=atc_hdlc_create_s_ctrl(0x00, 6, 0) };
    atc_hdlc_frame_pack(&rr_frame2, temp_input_buffer, sizeof(temp_input_buffer), &len);
    atc_hdlc_input_bytes(&ctx, temp_input_buffer, len);

    // If valid, the timer should be reset to timeout. If ignored, the timer continues ticking down.
    if (ctx.retransmit_timer == 0 || ctx.retransmit_timer == timer_before) {
        test_fail("NR Modulo Bug", "Ignored valid N(R)=6 due to modulo arithmetic bug");
        return;
    }

    test_pass("NR Modulo Bug Fixed");
}

void test_nr_edge_cases(void) {
    printf("\nTEST: N(R) Edge Cases\n");
    reset_test_state();

    atc_hdlc_context_t ctx;
    atc_hdlc_u8 retx_buf[256];
    atc_hdlc_init(&ctx, mock_rx_buffer, sizeof(mock_rx_buffer), 
                  retx_buf, sizeof(retx_buf), 
                  1000, ATC_HDLC_DEFAULT_ACK_DELAY_TIMEOUT, 7, 
                  3, mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = ATC_HDLC_PROTOCOL_STATE_CONNECTED;

    typedef struct {
        uint8_t va;
        uint8_t vs;
        uint8_t nr;
        bool expect_valid;
        const char *desc;
    } nr_test_case_t;

    nr_test_case_t cases[] = {
        // Normal window [va=0, vs=3)
        { 0, 3, 0, true,  "Normal window, ACK none" },
        { 0, 3, 1, true,  "Normal window, ACK 0" },
        { 0, 3, 3, true,  "Normal window, ACK all" },
        { 0, 3, 4, false, "Normal window, out of bounds future" },
        { 0, 3, 7, false, "Normal window, out of bounds past" },

        // Wrap-around window [va=6, vs=1) 
        { 6, 1, 6, true,  "Wrap window, ACK none" },
        { 6, 1, 7, true,  "Wrap window, ACK 6" },
        { 6, 1, 0, true,  "Wrap window, ACK 6,7" },
        { 6, 1, 1, true,  "Wrap window, ACK all" },
        { 6, 1, 2, false, "Wrap window, out of bounds future" },
        { 6, 1, 5, false, "Wrap window, out of bounds past" },

        // Full window [va=2, vs=1)
        { 2, 1, 2, true,  "Full window wrap, ACK none" },
        { 2, 1, 0, true,  "Full window wrap, ACK past wrap" },
        { 2, 1, 1, true,  "Full window wrap, ACK all" },

        // Empty window [va=4, vs=4)
        { 4, 4, 4, true,  "Empty window, ACK none/all" },
        { 4, 4, 5, false, "Empty window, invalid future" },
        { 4, 4, 3, false, "Empty window, invalid past" }
    };

    for (int i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        ctx.va = cases[i].va;
        ctx.vs = cases[i].vs;
        ctx.retransmit_timer = 100; // arbitrary tick value to observe change

        atc_hdlc_frame_t rr_frame = { .address=0x01, .control=atc_hdlc_create_s_ctrl(0x00, cases[i].nr, 0) };
        atc_hdlc_u32 len = 0;
        atc_hdlc_frame_pack(&rr_frame, temp_input_buffer, sizeof(temp_input_buffer), &len);
        
        atc_hdlc_input_bytes(&ctx, temp_input_buffer, len);

        bool was_treated_as_valid = false;
        
        // If the frame is valid, V(A) becomes N(R).
        // If N(R) == V(A), V(A) doesn't change, but processing a valid N(R) updates the retransmit timer.
        if (ctx.va != cases[i].va) {
            was_treated_as_valid = true;
        } else if (cases[i].nr == cases[i].va && ctx.retransmit_timer != 100) {
            was_treated_as_valid = true;
        }

        if (was_treated_as_valid != cases[i].expect_valid) {
            char fail_msg[256];
            snprintf(fail_msg, sizeof(fail_msg), "%s. Case: %s [va=%u vs=%u nr=%u]", 
                cases[i].expect_valid ? "Expected Valid, but rejected" : "Expected Invalid, but accepted", 
                cases[i].desc, cases[i].va, cases[i].vs, cases[i].nr);
            test_fail("NR Edge Cases", fail_msg);
            return;
        }
    }
    
    test_pass("NR Edge Cases Evaluated Successfully");
}

void test_state_initialization(void) {
    printf("\nTEST: SABM/UA State Initialization\n");
    reset_test_state();

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = ATC_HDLC_PROTOCOL_STATE_CONNECTED;

    // 1. Send an I-frame so that V(S) increments to 1
    atc_hdlc_output_frame_i(&ctx, (atc_hdlc_u8 *)"TEST", 4);
    
    // Simulate peer sending an I-frame so V(R) increments to 1
    atc_hdlc_frame_t peer_i_frame = {
        .address = 0x01,
        .control = atc_hdlc_create_i_ctrl(0, 0, 0),
        .information = (atc_hdlc_u8 *)"PEER",
        .information_len = 4
    };
    atc_hdlc_u32 len = 0;
    atc_hdlc_frame_pack(&peer_i_frame, temp_input_buffer, sizeof(temp_input_buffer), &len);
    atc_hdlc_input_bytes(&ctx, temp_input_buffer, len);
    
    // Simulate peer sending RR N(R)=1 so V(A) increments to 1
    atc_hdlc_frame_t peer_rr = { .address = 0x01, .control = atc_hdlc_create_s_ctrl(0x00, 1, 0) };
    atc_hdlc_frame_pack(&peer_rr, temp_input_buffer, sizeof(temp_input_buffer), &len);
    atc_hdlc_input_bytes(&ctx, temp_input_buffer, len);

    if (ctx.vs == 0 || ctx.vr == 0 || ctx.va == 0) {
        test_fail("State Init Setup", "Failed to advance state variables before reset");
        return;
    }

    // 2. Peer sends SABM to reset connection
    printf("   Peer sends SABM to trigger reset.\n");
    atc_hdlc_frame_t sabm_frame = {
        .address = 0x01,
        .control = atc_hdlc_create_u_ctrl(3, 1, 1), // SABM modifier lo=3, hi=1
    };
    atc_hdlc_frame_pack(&sabm_frame, temp_input_buffer, sizeof(temp_input_buffer), &len);
    atc_hdlc_input_bytes(&ctx, temp_input_buffer, len);

    // After processing SABM, we should be connected and state variables MUST be 0
    if (ctx.vs != 0 || ctx.vr != 0 || ctx.va != 0) {
        char msg[128];
        sprintf(msg, "Variables not reset: V(S)=%d, V(R)=%d, V(A)=%d", ctx.vs, ctx.vr, ctx.va);
        test_fail("State Init", msg);
        return;
    }

    test_pass("SABM/UA State Initialization Working");
}

int main(void) {
  printf("\n%sSTARTING RELIABLE TRANSMISSION TEST SUITE%s\n", COL_YELLOW,
         COL_RESET);
  printf("----------------------------------------\n\n");

  test_reliable_transmission();
  test_reliable_retransmission();
  test_sequence_rollover();
  test_duplicate_ack_ignored();
  test_rej_retransmit();
  test_piggyback_ack();
  test_window_size_2_basic();
  test_gobackn_retransmit();
  test_window7_mid_rej();
  test_throughput_benchmark();
  test_process_tx_task_simulation();
  test_nr_modulo_validation();
  test_nr_edge_cases();
  test_state_initialization();

  printf("\n%sALL RELIABLE TRANSMISSION TESTS PASSED SUCCESSFULLY!%s\n", COL_GREEN, COL_RESET);
  return 0;
}
