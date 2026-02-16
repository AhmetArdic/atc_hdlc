#include "../inc/hdlc.h"
#include "test_common.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Mocking & Utilities (Duplicated from test_hdlc.c) ---
static atc_hdlc_u8 output_buffer[16384];
static atc_hdlc_u8 input_buffer[16384];
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

void test_reliable_transmission(void) {
    printf("========================================\n");
    printf("TEST: Reliable Transmission (I-Frame + ACK)\n");
    printf("========================================\n");
    atc_hdlc_context_t ctx;
    atc_hdlc_u8 retx_buf[128];
    atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), retx_buf, sizeof(retx_buf), HDLC_DEFAULT_RETRANSMIT_TIMEOUT_MS, HDLC_DEFAULT_WINDOW_SIZE, mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    reset_test();
    
    // Connect
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02); // Me=1, Peer=2
    ctx.current_state = 2; // CONNECTED
    
    // Send I-Frame
    output_len = 0;
    atc_hdlc_u8 data[] = {0xAA, 0xBB};
    bool res = atc_hdlc_output_i(&ctx, data, sizeof(data));
    
    if (!res) test_fail("Reliable I-Frame", "Failed to send");
    
    // Verify output (Check 3rd byte for Control 0x00)
    if (output_len > 0 && output_buffer[2] == 0x00) {
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
    reset_test();
    atc_hdlc_frame_t peer_frame = { .address=0x01, .control=atc_hdlc_create_i_ctrl(0, 1, 0), .information=NULL, .information_len=0 }; 
    atc_hdlc_u32 encoded_len = 0;
    atc_hdlc_frame_pack(&peer_frame, input_buffer, sizeof(input_buffer), &encoded_len);
    
    for(int i=0; i<encoded_len; i++) atc_hdlc_input_byte(&ctx, input_buffer[i]);
    
    if (ctx.va == ctx.vs && ctx.vr == 1) { // VR increments because we received valid I-frame
        test_pass("ACK Reception");
    } else {
        test_fail("ACK Reception", "State mismatch after ACK");
    }
}

void test_reliable_retransmission(void) {
    printf("========================================\n");
    printf("TEST: Reliable Retransmission (Timer)\n");
    printf("========================================\n");
    atc_hdlc_context_t ctx;
    atc_hdlc_u8 retx_buf[128];
    atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), retx_buf, sizeof(retx_buf), HDLC_DEFAULT_RETRANSMIT_TIMEOUT_MS, HDLC_DEFAULT_WINDOW_SIZE, mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    reset_test();
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = 2; // CONNECTED
    
    // Send I-Frame
    atc_hdlc_u8 data[] = {0xCA, 0xFE};
    atc_hdlc_output_i(&ctx, data, sizeof(data));
    output_len = 0; // Clear output
    
    // Tick Timer (1001 ticks of 1ms)
    for(int i=0; i<1001; i++) atc_hdlc_tick(&ctx, 1);
    
    // Verify Retransmission
    if (output_len > 0 && output_buffer[2] == 0x10) { // P=1
        test_pass("Retransmission Sent");
    } else {
        test_fail("Retransmission Sent", "No output or wrong control");
    }
}

void test_sequence_rollover(void) {
    printf("========================================\n");
    printf("TEST: Sequence Number Rollover (0->7->0)\n");
    printf("========================================\n");
    atc_hdlc_context_t ctx;
    atc_hdlc_u8 retx_buf[128];
    atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), retx_buf, sizeof(retx_buf), HDLC_DEFAULT_RETRANSMIT_TIMEOUT_MS, HDLC_DEFAULT_WINDOW_SIZE, mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = 2; // CONNECTED
    
    atc_hdlc_u8 data[] = {0x00};
    
    for (int i = 0; i < 9; i++) {
        // 1. Send Frame i (VS = i % 8)
        atc_hdlc_output_i(&ctx, data, 1);
        
        // Check V(S) incremented
        int expected_vs = (i + 1) % 8;
        if (ctx.vs != expected_vs) {
             test_fail("Sequence Rollover", "VS mismatch");
             printf("   Expected VS: %d, Got: %d (Iter %d)\n", expected_vs, ctx.vs, i);
             return;
        }
        
        // 2. Acknowledge it (Peer sends RR with NR = expected_vs)
        // We simulate peer sending RR
        atc_hdlc_frame_t rr_frame;
        rr_frame.address = 0x01;
        rr_frame.control = atc_hdlc_create_s_ctrl(0, expected_vs, 0); // RR, NR=VS
        rr_frame.information = NULL;
        rr_frame.information_len = 0;
        rr_frame.type = HDLC_FRAME_S;
        
        atc_hdlc_u32 len = 0;
        atc_hdlc_frame_pack(&rr_frame, input_buffer, sizeof(input_buffer), &len);
        atc_hdlc_input_bytes(&ctx, input_buffer, len);
        
        if (ctx.va != ctx.vs) {
             test_fail("Sequence Rollover", "Failed to ACK");
             return;
        }
    }
    test_pass("Sequence Rollover");
}

void test_duplicate_ack_ignored(void) {
    printf("========================================\n");
    printf("TEST: Duplicate ACK Ignored\n");
    printf("========================================\n");
    atc_hdlc_context_t ctx;
    atc_hdlc_u8 retx_buf[128];
    atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), retx_buf, sizeof(retx_buf), HDLC_DEFAULT_RETRANSMIT_TIMEOUT_MS, HDLC_DEFAULT_WINDOW_SIZE, mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = 2; // CONNECTED
    
    // Send I-Frame (VS becomes 1)
    atc_hdlc_output_i(&ctx, (atc_hdlc_u8*)"A", 1);
    
    // Receive ACK (RR NR=1)
    atc_hdlc_frame_t rr_frame = { .address=0x01, .control=atc_hdlc_create_s_ctrl(0, 1, 0) };
    atc_hdlc_u32 len = 0;
    atc_hdlc_frame_pack(&rr_frame, input_buffer, sizeof(input_buffer), &len);
    atc_hdlc_input_bytes(&ctx, input_buffer, len);
    
    if (ctx.va != ctx.vs) test_fail("Duplicate ACK", "First ACK failed");
    
    // Receive SAME ACK again
    atc_hdlc_input_bytes(&ctx, input_buffer, len);
    
    // Should stay not waiting, no error
    if (ctx.va != ctx.vs) test_fail("Duplicate ACK", "State regressed?");
    
    test_pass("Duplicate ACK Ignored");
}

void test_rej_retransmit(void) {
    printf("========================================\n");
    printf("TEST: REJ Triggers Retransmission\n");
    printf("========================================\n");
    atc_hdlc_context_t ctx;
    atc_hdlc_u8 retx_buf[128];
    atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), retx_buf, sizeof(retx_buf), HDLC_DEFAULT_RETRANSMIT_TIMEOUT_MS, HDLC_DEFAULT_WINDOW_SIZE, mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = 2; // CONNECTED
    
    // Send I-Frame [VS=0] -> VS becomes 1
    atc_hdlc_output_i(&ctx, (atc_hdlc_u8*)"XY", 2);
    output_len = 0; // Clear output
    
    // Receive REJ [NR=0] (Peer asking for frame 0 again)
    atc_hdlc_frame_t rej_frame = { .address=0x01, .control=atc_hdlc_create_s_ctrl(2, 0, 0) }; // REJ=2
    atc_hdlc_u32 len = 0;
    atc_hdlc_frame_pack(&rej_frame, input_buffer, sizeof(input_buffer), &len);
    atc_hdlc_input_bytes(&ctx, input_buffer, len);
    
    // Verify we retransmitted
    // The previous output was cleared. 
    // We expect retransmission in output buffer.
    if (output_len > 0) {
        // Verify content "XY"
        // I-frame structure: 7E Addr Ctrl Data FCS 7E
        // We can check if it looks like I-frame.
        test_pass("REJ Triggered Retransmit");
    } else {
        test_fail("REJ Triggered Retransmit", "No output generated");
    }
}

void test_piggyback_ack(void) {
    printf("========================================\n");
    printf("TEST: Piggyback ACK via I-Frame N(R)\n");
    printf("========================================\n");
    atc_hdlc_context_t ctx;
    atc_hdlc_u8 retx_buf[128];
    atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), retx_buf, sizeof(retx_buf), HDLC_DEFAULT_RETRANSMIT_TIMEOUT_MS, HDLC_DEFAULT_WINDOW_SIZE, mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = 2; // CONNECTED

    // --- Phase 1: Outgoing Piggyback ---
    // Receive an I-frame from peer: N(S)=0, N(R)=0 (peer expects our frame 0)
    printf("\n--- Phase 1: Outgoing Piggyback (I-frame carries ACK for received frame) ---\n");
    reset_test();

    // Build peer's I-frame: Addr=0x01 (to me), Ctrl: I-frame N(S)=0, N(R)=0, P=0
    atc_hdlc_frame_t peer_frame = {
        .address = 0x01,
        .control = atc_hdlc_create_i_ctrl(0, 0, 0),
        .information = (atc_hdlc_u8 *)"HI",
        .information_len = 2
    };

    // Encode and feed peer's frame
    atc_hdlc_u8 encoded[64];
    atc_hdlc_u32 encoded_len = 0;
    atc_hdlc_frame_pack(&peer_frame, encoded, sizeof(encoded), &encoded_len);
    atc_hdlc_input_bytes(&ctx, encoded, encoded_len);

    // After receiving, V(R) should be 1, ack_pending should be true
    // (library sent an immediate RR, so ack_pending is cleared)
    // But if we now send our own I-frame, its N(R) should carry V(R)=1
    printf("V(R) after receiving peer I-frame: %d\n", ctx.vr);
    if (ctx.vr != 1) {
        test_fail("Piggyback: V(R) incremented", "V(R) not 1");
        return;
    }
    test_pass("Piggyback: V(R) incremented to 1");

    // Send our own I-frame — this embeds N(R)=V(R)=1 as the piggyback ACK
    reset_test();
    atc_hdlc_u8 our_data[] = "OK";
    bool sent = atc_hdlc_output_i(&ctx, our_data, sizeof(our_data) - 1);
    if (!sent) {
        test_fail("Piggyback: Outgoing I-frame sent", "output_i returned false");
        return;
    }
    test_pass("Piggyback: Outgoing I-frame sent");

    // Verify: Our VS should have incremented and ack_pending should be false (piggybacked)
    if (ctx.ack_pending == false) {
        test_pass("Piggyback: ack_pending cleared by outgoing I-frame");
    } else {
        test_fail("Piggyback: ack_pending cleared", "ack_pending still true");
    }

    // --- Phase 2: Incoming Piggyback ---
    // Our I-frame is pending ACK (va != vs, vs=1).
    // Peer responds with its own I-frame containing N(R)=1 (ACKing our frame).
    printf("\n--- Phase 2: Incoming Piggyback (Peer I-frame ACKs our frame via N(R)) ---\n");

    if (ctx.va == ctx.vs) {
        test_fail("Piggyback: Pre-check outstanding frames", "Expected va != vs");
        return;
    }
    test_pass("Piggyback: Pre-check outstanding frames (va != vs)");

    // Peer sends I-frame: N(S)=1 (next seq), N(R)=1 (ACKing our frame 0)
    atc_hdlc_frame_t peer_frame2 = {
        .address = 0x01,
        .control = atc_hdlc_create_i_ctrl(1, 1, 0), // N(S)=1, N(R)=1=our VS
        .information = (atc_hdlc_u8 *)"RE",
        .information_len = 2
    };

    reset_test();
    atc_hdlc_frame_pack(&peer_frame2, encoded, sizeof(encoded), &encoded_len);
    atc_hdlc_input_bytes(&ctx, encoded, encoded_len);

    // After processing, all frames should be acknowledged (va == vs)
    if (ctx.va == ctx.vs) {
        test_pass("Piggyback: Incoming I-frame N(R) acknowledged all frames");
    } else {
        test_fail("Piggyback: Incoming ACK", "va != vs, frames still outstanding");
    }

    // V(R) should now be 2
    if (ctx.vr == 2) {
        test_pass("Piggyback: V(R) incremented to 2");
    } else {
        test_fail("Piggyback: V(R) check", "V(R) not 2");
    }

    // Retransmit timer should be cleared
    if (ctx.retransmit_timer_ms == 0) {
        test_pass("Piggyback: Retransmit timer cleared");
    } else {
        test_fail("Piggyback: Timer check", "retransmit_timer_ms not 0");
    }
}

/**
 * @brief Test: Window Size 2 — Send 2 I-frames, ACK both with single RR.
 */
void test_window_size_2_basic(void) {
    printf("\n========================================\n");
    printf("TEST: Window Size 2 — Basic\n");
    printf("========================================\n");
    atc_hdlc_context_t ctx;
    // Buffer large enough for 2 slots of 64 bytes each
    atc_hdlc_u8 retx_buf[128];
    atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), retx_buf, sizeof(retx_buf), HDLC_DEFAULT_RETRANSMIT_TIMEOUT_MS, 2, mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = 2; // CONNECTED
    reset_test();

    // 1. Send first I-frame (N(S)=0)
    atc_hdlc_u8 data1[] = "FRAME1";
    bool ok1 = atc_hdlc_output_i(&ctx, data1, sizeof(data1));
    if (!ok1) { test_fail("Window2: Send first", "output_i returned false"); return; }
    if (ctx.vs != 1) { test_fail("Window2: VS after first", "Expected VS=1"); return; }
    test_pass("Window2: Sent first I-frame (N(S)=0)");

    // 2. Send second I-frame (N(S)=1) — window still open
    atc_hdlc_u8 data2[] = "FRAME2";
    bool ok2 = atc_hdlc_output_i(&ctx, data2, sizeof(data2));
    if (!ok2) { test_fail("Window2: Send second", "output_i returned false"); return; }
    if (ctx.vs != 2) { test_fail("Window2: VS after second", "Expected VS=2"); return; }
    test_pass("Window2: Sent second I-frame (N(S)=1)");

    // 3. Window should now be full (2 outstanding, window_size=2)
    atc_hdlc_u8 data3[] = "BLOCKED";
    bool ok3 = atc_hdlc_output_i(&ctx, data3, sizeof(data3));
    if (ok3) { test_fail("Window2: Window full check", "Should have returned false"); return; }
    test_pass("Window2: Third send correctly blocked (window full)");

    // 4. ACK both with RR N(R)=2
    atc_hdlc_frame_t rr_frame = { .address=0x01, .control=atc_hdlc_create_s_ctrl(0, 2, 0) };
    atc_hdlc_u32 len = 0;
    atc_hdlc_frame_pack(&rr_frame, input_buffer, sizeof(input_buffer), &len);
    atc_hdlc_input_bytes(&ctx, input_buffer, len);

    if (ctx.va == ctx.vs) {
        test_pass("Window2: Cumulative ACK cleared all outstanding frames");
    } else {
        test_fail("Window2: Cumulative ACK", "va != vs after RR(2)");
        return;
    }

    // 5. Should be able to send again now
    bool ok4 = atc_hdlc_output_i(&ctx, data3, sizeof(data3));
    if (!ok4) { test_fail("Window2: Send after ACK", "Window should be open"); return; }
    test_pass("Window2: Send succeeds after ACK (window reopened)");
}

/**
 * @brief Test: Go-Back-N Retransmission on timeout.
 *        Send 3 frames with window=3, then trigger timeout — all 3 must retransmit.
 */
void test_gobackn_retransmit(void) {
    printf("\n========================================\n");
    printf("TEST: Go-Back-N Retransmission (Timeout)\n");
    printf("========================================\n");
    atc_hdlc_context_t ctx;
    atc_hdlc_u8 retx_buf[192]; // 3 slots of 64 bytes
    atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), retx_buf, sizeof(retx_buf), 500, 3, mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = 2; // CONNECTED
    reset_test();

    // Send 3 I-frames
    atc_hdlc_u8 d1[] = "AAA";
    atc_hdlc_u8 d2[] = "BBB";
    atc_hdlc_u8 d3[] = "CCC";
    atc_hdlc_output_i(&ctx, d1, sizeof(d1));
    atc_hdlc_output_i(&ctx, d2, sizeof(d2));
    atc_hdlc_output_i(&ctx, d3, sizeof(d3));

    if (ctx.vs != 3 || ctx.va != 0) {
        test_fail("GBN Retransmit: initial state", "Expected vs=3, va=0");
        return;
    }
    test_pass("GBN Retransmit: 3 frames sent (vs=3, va=0)");

    // Record output frame count before timeout
    atc_hdlc_u32 frames_before = ctx.stats_output_frames;

    // Trigger timeout (500ms)
    atc_hdlc_tick(&ctx, 500);

    atc_hdlc_u32 retransmitted = ctx.stats_output_frames - frames_before;
    if (retransmitted == 3) {
        test_pass("GBN Retransmit: All 3 frames retransmitted on timeout");
    } else {
        printf("  Expected 3 retransmitted, got %u\n", retransmitted);
        test_fail("GBN Retransmit: frame count", "Wrong number of retransmissions");
        return;
    }

    // va and vs should remain unchanged
    if (ctx.vs == 3 && ctx.va == 0) {
        test_pass("GBN Retransmit: State preserved (vs=3, va=0)");
    } else {
        test_fail("GBN Retransmit: state check", "vs or va changed");
    }

    // Timer should have been restarted
    if (ctx.retransmit_timer_ms == 500) {
        test_pass("GBN Retransmit: Timer restarted");
    } else {
        test_fail("GBN Retransmit: timer check", "Timer not restarted correctly");
    }
}

int main() {
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

  printf("\n%sALL RELIABLE TRANSMISSION TESTS PASSED SUCCESSFULLY!%s\n", COL_GREEN, COL_RESET);
  return 0;
}
