#include "../inc/hdlc.h"
#include "../src/hdlc_private.h"
#include "test_common.h"
#include <stdint.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Scratch buffer for packing frames to feed into RX */
static atc_hdlc_u8 temp_input_buffer[2048];

/*
 * make_ctx() — build a context with the given window_size and t1_ms,
 * then set peer_address and mark as CONNECTED for unit tests.
 */
static atc_hdlc_u8  s_slots_rt[7 * 1024];
static atc_hdlc_u32 s_lens_rt[7];
static atc_hdlc_u8  s_seq_rt[8]; /* mod-8: indexed by V(S) 0..7 */

static atc_hdlc_config_t    s_make_ctx_cfg;
static atc_hdlc_tx_window_t s_make_ctx_tw;
static atc_hdlc_rx_buffer_t s_make_ctx_rx;

static const atc_hdlc_platform_t s_make_ctx_plat = {
    .on_send = mock_send_cb, .on_data = mock_on_data_cb,
    .on_event = NULL, .user_ctx = NULL,
};

static void make_ctx(atc_hdlc_context_t *ctx,
                     atc_hdlc_u8 window_size,
                     atc_hdlc_u32 t1_ms) {
    s_make_ctx_cfg.mode           = ATC_HDLC_MODE_ABM;
    s_make_ctx_cfg.address        = 0x01;
    s_make_ctx_cfg.window_size    = window_size;
    s_make_ctx_cfg.max_frame_size = 1024;
    s_make_ctx_cfg.max_retries    = 3;
    s_make_ctx_cfg.t1_ms          = t1_ms;
    s_make_ctx_cfg.t2_ms          = ATC_HDLC_DEFAULT_T2_TIMEOUT;
    s_make_ctx_cfg.use_extended   = false;

    s_make_ctx_tw.slots         = s_slots_rt;
    s_make_ctx_tw.slot_lens     = s_lens_rt;
    s_make_ctx_tw.seq_to_slot   = s_seq_rt;
    s_make_ctx_tw.slot_capacity = 1024;
    s_make_ctx_tw.slot_count    = window_size;

    s_make_ctx_rx.buffer   = mock_rx_buffer;
    s_make_ctx_rx.capacity = sizeof(mock_rx_buffer);

    atc_hdlc_params_t p = { .config = &s_make_ctx_cfg, .platform = &s_make_ctx_plat,
                             .tx_window = &s_make_ctx_tw, .rx_buf = &s_make_ctx_rx };
    atc_hdlc_init(ctx, p);
    ctx->peer_address  = 0x02;
    ctx->current_state = ATC_HDLC_STATE_CONNECTED;
}

/* --- Tests --- */

/**
 * @brief Test: Reliable Transmission (I-Frame + ACK).
 *        Verifies I-frame sending, state update, and ACK reception.
 */
void test_reliable_transmission(void) {
    printf("TEST: Reliable Transmission (I-Frame + ACK)\n");
    reset_test_state();
    
    atc_hdlc_context_t ctx;
    make_ctx(&ctx, ATC_HDLC_DEFAULT_WINDOW_SIZE, ATC_HDLC_DEFAULT_T1_TIMEOUT);
    
    // Send I-Frame
    mock_output_len = 0;
    atc_hdlc_u8 data[] = {0xAA, 0xBB};
    atc_hdlc_error_t res = atc_hdlc_transmit_i(&ctx, data, sizeof(data));
    
    if (res != ATC_HDLC_OK) test_fail("Reliable I-Frame", "Failed to send");
    if (mock_output_len == 0 || mock_output_buffer[2] != 0x00)
        test_fail("Reliable I-Frame", "Incorrect output");
    if (ctx.vs != 1 || ctx.va == ctx.vs)
        test_fail("Reliable I-Frame", "VS/VA incorrect after send");

    /* ACK: peer sends I-frame N(S)=0, N(R)=1 */
    reset_test_state();
    atc_hdlc_u32 encoded_len = (atc_hdlc_u32)test_pack_frame(0x01, I_CTRL(0, 1, 0),
                                                               NULL, 0,
                                                               temp_input_buffer, sizeof(temp_input_buffer));
    atc_hdlc_data_in(&ctx, temp_input_buffer, encoded_len);

    if (ctx.va != ctx.vs || ctx.vr != 1)
        test_fail("Reliable I-Frame", "State mismatch after ACK");

    test_pass("Reliable Transmission");
}

void test_reliable_retransmission(void) {
    printf("TEST: Reliable Retransmission (Timer)\n");
    reset_test_state();
    
    atc_hdlc_context_t ctx;
make_ctx(&ctx, ATC_HDLC_DEFAULT_WINDOW_SIZE, ATC_HDLC_DEFAULT_T1_TIMEOUT);
    
    // Send I-Frame
    atc_hdlc_u8 data[] = {0xCA, 0xFE};
    atc_hdlc_transmit_i(&ctx, data, sizeof(data));
    mock_output_len = 0; // Clear output
    
    // Tick Timer (1001 ticks of 1ms)
    atc_hdlc_t1_expired(&ctx); /* simulated T1 expiry */
    
    // Verify Retransmission Enquiry
    // 0x11 is S-frame, RR, P=1, N(R)=0. (b0=1, b1=0, s=0, p=1, nr=0) => 00010001 = 0x11
    if (mock_output_len > 0 && mock_output_buffer[2] == 0x11) { 
        test_pass("Retransmission Enquiry Sent");
    } else {
        test_fail("Retransmission Sent", "No Enquiry output or wrong control");
    }
}

/**
 * @brief Test: N2 retry exhaustion in CONNECTED state.
 *        Send an I-frame, exhaust T1 retries without ACK → LINK_FAILURE event,
 *        DISCONNECTED state. Also verifies retries below N2 do not fail prematurely.
 */
void test_n2_retry_connected(void) {
    printf("TEST: N2 Retry Exhaustion in CONNECTED State\n");

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx); /* max_retries = 3, on_event = mock_on_event_cb */
    ctx.current_state = ATC_HDLC_STATE_CONNECTED;
    ctx.peer_address  = 0x02;

    atc_hdlc_u8 data[] = {0xAA};
    if (atc_hdlc_transmit_i(&ctx, data, 1) != ATC_HDLC_OK)
        test_fail("N2 Connected", "Failed to send I-frame");

    /* Each of the first max_retries expirations must send RR enquiry and restart T1 */
    for (int i = 0; i < (int)ctx.config->max_retries; i++) {
        mock_output_len = 0;
        atc_hdlc_t1_expired(&ctx);
        if (mock_output_len == 0)
            test_fail("N2 Connected", "T1 expiry did not send RR enquiry");
        if (!ctx.t1_active)
            test_fail("N2 Connected", "T1 not restarted after enquiry");
        if (ctx.current_state == ATC_HDLC_STATE_DISCONNECTED) {
            char msg[64];
            sprintf(msg, "Premature link failure on retry %d (max=%u)", i + 1, ctx.config->max_retries);
            test_fail("N2 Connected", msg);
        }
    }

    /* One more expiry exceeds N2 → LINK_FAILURE */
    atc_hdlc_t1_expired(&ctx);

    if (ctx.current_state != ATC_HDLC_STATE_DISCONNECTED)
        test_fail("N2 Connected", "State not DISCONNECTED after N2 exceeded");
    if (last_event != ATC_HDLC_EVENT_LINK_FAILURE)
        test_fail("N2 Connected", "LINK_FAILURE event not fired");
    if (ctx.t1_active)
        test_fail("N2 Connected", "T1 should be stopped after link failure");

    test_pass("N2 Retry Exhaustion in CONNECTED State");
}

void test_sequence_rollover(void) {
    printf("TEST: Sequence Number Rollover (0->7->0)\n");
    reset_test_state();
    
    atc_hdlc_context_t ctx;
make_ctx(&ctx, ATC_HDLC_DEFAULT_WINDOW_SIZE, ATC_HDLC_DEFAULT_T1_TIMEOUT);
    
    atc_hdlc_u8 data[] = {0x00};
    
    for (int i = 0; i < 9; i++) {
        // 1. Send Frame i (VS = i % 8)
        atc_hdlc_transmit_i(&ctx, data, 1);
        
        // Check V(S) incremented
        int expected_vs = (i + 1) % 8;
        if (ctx.vs != expected_vs) {
             test_fail("Sequence Rollover", "VS mismatch");
             printf("   Expected VS: %d, Got: %d (Iter %d)\n", expected_vs, ctx.vs, i);
             return;
        }
        
        // 2. Acknowledge it (Peer sends RR with NR = expected_vs)
        atc_hdlc_u32 len = (atc_hdlc_u32)test_pack_frame(0x01, S_CTRL(0, expected_vs, 0),
                                                           NULL, 0,
                                                           temp_input_buffer, sizeof(temp_input_buffer));
        atc_hdlc_data_in(&ctx, temp_input_buffer, len);
        
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
make_ctx(&ctx, ATC_HDLC_DEFAULT_WINDOW_SIZE, ATC_HDLC_DEFAULT_T1_TIMEOUT);
    
    // Send I-Frame (VS becomes 1)
    atc_hdlc_transmit_i(&ctx, (atc_hdlc_u8*)"A", 1);
    
    // Receive ACK (RR NR=1)
    atc_hdlc_u32 len = (atc_hdlc_u32)test_pack_frame(0x01, S_CTRL(0, 1, 0),
                                                       NULL, 0,
                                                       temp_input_buffer, sizeof(temp_input_buffer));
    atc_hdlc_data_in(&ctx, temp_input_buffer, len);
    
    if (ctx.va != ctx.vs) test_fail("Duplicate ACK", "First ACK failed");
    
    // Receive SAME ACK again
    atc_hdlc_data_in(&ctx, temp_input_buffer, len);
    
    // Should stay not waiting, no error
    if (ctx.va != ctx.vs) test_fail("Duplicate ACK", "State regressed?");
    
    test_pass("Duplicate ACK Ignored");
}

void test_rej_retransmit(void) {
    printf("TEST: REJ Triggers Retransmission\n");
    reset_test_state();
    
    atc_hdlc_context_t ctx;
make_ctx(&ctx, ATC_HDLC_DEFAULT_WINDOW_SIZE, ATC_HDLC_DEFAULT_T1_TIMEOUT);
    
    // Send I-Frame [VS=0] -> VS becomes 1
    atc_hdlc_transmit_i(&ctx, (atc_hdlc_u8*)"XY", 2);
    mock_output_len = 0; // Clear output
    
    // Receive REJ [NR=0] (Peer asking for frame 0 again)
    atc_hdlc_u32 len = (atc_hdlc_u32)test_pack_frame(0x01, S_CTRL(2, 0, 0),
                                                       NULL, 0,
                                                       temp_input_buffer, sizeof(temp_input_buffer));
    atc_hdlc_data_in(&ctx, temp_input_buffer, len);
    
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
make_ctx(&ctx, ATC_HDLC_DEFAULT_WINDOW_SIZE, ATC_HDLC_DEFAULT_T1_TIMEOUT);

    // --- Phase 1: Outgoing Piggyback ---
    // Receive an I-frame from peer: N(S)=0, N(R)=0 (peer expects our frame 0)
    printf("\n--- Phase 1: Outgoing Piggyback ---\n");
    
    // Build peer's I-frame: Addr=0x01 (to me), Ctrl: I-frame N(S)=0, N(R)=0, P=0
    atc_hdlc_u32 encoded_len = (atc_hdlc_u32)test_pack_frame(0x01, I_CTRL(0, 0, 0),
                                                               (atc_hdlc_u8 *)"HI", 2,
                                                               temp_input_buffer, sizeof(temp_input_buffer));
    atc_hdlc_data_in(&ctx, temp_input_buffer, encoded_len);

    // After receiving, V(R) should be 1
    if (ctx.vr != 1) {
        test_fail("Piggyback", "V(R) not 1");
        return;
    }

    // Send our own I-frame — this embeds N(R)=V(R)=1 as the piggyback ACK
    mock_output_len = 0;
    atc_hdlc_u8 our_data[] = "OK";
    atc_hdlc_error_t sent = atc_hdlc_transmit_i(&ctx, our_data, sizeof(our_data) - 1);
    if (sent != ATC_HDLC_OK) {
        test_fail("Piggyback", "output_i returned false");
        return;
    }

    if (ctx.t2_active)
        test_fail("Piggyback", "T2 not cleared by outgoing I-frame (piggyback)");
    if (ctx.va == ctx.vs)
        test_fail("Piggyback", "Expected va != vs before peer ACK");

    /* Phase 2: peer's I-frame N(S)=1, N(R)=1 ACKs our frame 0 */
    encoded_len = (atc_hdlc_u32)test_pack_frame(0x01, I_CTRL(1, 1, 0),
                                                  (atc_hdlc_u8 *)"RE", 2,
                                                  temp_input_buffer, sizeof(temp_input_buffer));
    atc_hdlc_data_in(&ctx, temp_input_buffer, encoded_len);

    if (ctx.va != ctx.vs)
        test_fail("Piggyback", "va != vs, frames still outstanding");
    if (ctx.vr != 2)
        test_fail("Piggyback", "V(R) not updated");

    test_pass("Piggyback ACK");
}

/**
 * @brief Test: Window Size 2 — Send 2 I-frames, ACK both with single RR.
 */
void test_window_size_2_basic(void) {
    printf("\nTEST: Window Size 2 — Basic\n");
    reset_test_state();
    
    atc_hdlc_context_t ctx;
    make_ctx(&ctx, 2, ATC_HDLC_DEFAULT_T1_TIMEOUT);

    atc_hdlc_u8 data1[] = "FRAME1";
    atc_hdlc_u8 data2[] = "FRAME2";
    atc_hdlc_u8 data3[] = "FRAME3";

    if (atc_hdlc_transmit_i(&ctx, data1, sizeof(data1)) != ATC_HDLC_OK)
        { test_fail("Window2", "Send first failed"); return; }
    if (ctx.vs != 1) { test_fail("Window2", "VS mismatch after frame 1"); return; }

    if (atc_hdlc_transmit_i(&ctx, data2, sizeof(data2)) != ATC_HDLC_OK)
        { test_fail("Window2", "Send second failed"); return; }
    if (ctx.vs != 2) { test_fail("Window2", "VS mismatch after frame 2"); return; }

    /* Window full — third send must be blocked */
    if (atc_hdlc_transmit_i(&ctx, data3, sizeof(data3)) == ATC_HDLC_OK)
        { test_fail("Window2", "Window overflow allowed"); return; }

    /* Cumulative ACK N(R)=2 */
    atc_hdlc_u32 len = (atc_hdlc_u32)test_pack_frame(0x01, S_CTRL(0, 2, 0),
                                                       NULL, 0,
                                                       temp_input_buffer, sizeof(temp_input_buffer));
    atc_hdlc_data_in(&ctx, temp_input_buffer, len);

    if (ctx.va != ctx.vs) { test_fail("Window2", "Cumulative ACK failed"); return; }

    /* Window reopened */
    if (atc_hdlc_transmit_i(&ctx, data3, sizeof(data3)) != ATC_HDLC_OK)
        { test_fail("Window2", "Send after ACK failed"); return; }

    test_pass("Window Size 2 Basic");
}

/**
 * @brief Test: Go-Back-N Retransmission on timeout.
 *        Send 3 frames with window=3, then trigger timeout — all 3 must retransmit.
 */
void test_gobackn_retransmit(void) {
    printf("\nTEST: Go-Back-N Retransmission (Timeout)\n");
    reset_test_state();
    
    atc_hdlc_context_t ctx;
    make_ctx(&ctx, 3, 500);

    // Send 3 I-frames
    atc_hdlc_u8 d1[] = "AAA";
    atc_hdlc_u8 d2[] = "BBB";
    atc_hdlc_u8 d3[] = "CCC";
    atc_hdlc_transmit_i(&ctx, d1, sizeof(d1));
    atc_hdlc_transmit_i(&ctx, d2, sizeof(d2));
    atc_hdlc_transmit_i(&ctx, d3, sizeof(d3));

    if (ctx.vs != 3 || ctx.va != 0) {
        test_fail("GBN Retransmit", "Setup failed");
        return;
    }

    // Trigger timeout
    atc_hdlc_t1_expired(&ctx); /* simulated T1 expiry */

    if (mock_output_len == 0) {
        test_fail("GBN Retransmit", "Enquiry RR not sent on timeout");
        return;
    }

    // Now peer replies with RR and F=1 (Response uses peer's own address = 0x02)
    atc_hdlc_u32 rr_len = (atc_hdlc_u32)test_pack_frame(0x02, S_CTRL(0x00, 0, 1),
                                                          NULL, 0,
                                                          temp_input_buffer, sizeof(temp_input_buffer));

    int frames_before = mock_frame_count;
    atc_hdlc_data_in(&ctx, temp_input_buffer, rr_len);

    int retransmitted = mock_frame_count - frames_before;
    if (retransmitted != 3) {
        printf("  Expected 3 retransmitted, got %d\n", retransmitted);
        test_fail("GBN Retransmit", "Wrong retransmit count");
        return;
    }
    if (ctx.vs != 3 || ctx.va != 0)
        test_fail("GBN Retransmit", "State corrupted after retransmit");
    if (!ctx.t1_active)
        test_fail("GBN Retransmit", "T1 not restarted after retransmit");

    test_pass("Go-Back-N Retransmission");
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
    make_ctx(&ctx, 7, ATC_HDLC_DEFAULT_T1_TIMEOUT);

    /* Phase 1: Fill window with 7 I-frames */
    char payload[8];
    for (int i = 0; i < 7; i++) {
        sprintf(payload, "PKT_%d", i);
        if (atc_hdlc_transmit_i(&ctx, (atc_hdlc_u8 *)payload, (atc_hdlc_u16)strlen(payload)) != ATC_HDLC_OK) {
            char msg[64];
            sprintf(msg, "Failed to send frame %d", i);
            test_fail("Window7 REJ", msg);
            return;
        }
    }
    if (ctx.vs != 7 || ctx.va != 0) {
        test_fail("Window7 REJ", "VS/VA mismatch after sending 7 frames");
        return;
    }

    /* Phase 2: Window full — 8th frame blocked */
    if (atc_hdlc_transmit_i(&ctx, (atc_hdlc_u8 *)"BLOCKED", 7) == ATC_HDLC_OK) {
        test_fail("Window7 REJ", "Window overflow allowed");
        return;
    }

    /* Phase 3: REJ N(R)=3 — ACKs 0-2, retransmits 3-6 */
    mock_output_len = 0;
    int frames_before = mock_frame_count;

    atc_hdlc_u32 rej_len = (atc_hdlc_u32)test_pack_frame(0x01, S_CTRL(0x02, 3, 0),
                                                           NULL, 0,
                                                           temp_input_buffer, sizeof(temp_input_buffer));
    atc_hdlc_data_in(&ctx, temp_input_buffer, rej_len);

    int retransmitted = mock_frame_count - frames_before;
    if (ctx.va != 3) {
        test_fail("Window7 REJ", "VA not advanced to 3 after REJ");
        return;
    }
    if (retransmitted != 4) {
        printf("  Expected 4 retransmissions, got %d\n", retransmitted);
        test_fail("Window7 REJ", "Wrong retransmit count after REJ");
        return;
    }
    if (ctx.vs != 7)
        { test_fail("Window7 REJ", "VS changed during retransmission"); return; }

    /* Phase 4: ACK all with RR N(R)=7, window reopens */
    atc_hdlc_u32 rr_len = (atc_hdlc_u32)test_pack_frame(0x01, S_CTRL(0x00, 7, 0),
                                                          NULL, 0,
                                                          temp_input_buffer, sizeof(temp_input_buffer));
    atc_hdlc_data_in(&ctx, temp_input_buffer, rr_len);

    if (ctx.va != ctx.vs) { test_fail("Window7 REJ", "VA != VS after full ACK"); return; }
    if (atc_hdlc_transmit_i(&ctx, (atc_hdlc_u8 *)"REOPEN", 6) != ATC_HDLC_OK) {
        test_fail("Window7 REJ", "Window did not reopen after full ACK");
        return;
    }

    test_pass("Window7 Mid-REJ");
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
    make_ctx(&ctx, (atc_hdlc_u8)window_size, ATC_HDLC_DEFAULT_T1_TIMEOUT);

    // Prepare chunk payload (repeating pattern)
    atc_hdlc_u8 chunk[BENCH_CHUNK_SIZE];
    for (int i = 0; i < BENCH_CHUNK_SIZE; i++) chunk[i] = (atc_hdlc_u8)(i & 0xFF);

    int sent = 0;

    while (sent < BENCH_TOTAL_CHUNKS) {
        // Send as many frames as the window allows
        int batch = 0;
        while (sent < BENCH_TOTAL_CHUNKS) {
            atc_hdlc_error_t ok = atc_hdlc_transmit_i(&ctx, chunk, BENCH_CHUNK_SIZE);
            if (ok != ATC_HDLC_OK) break; // Window full
            sent++;
            batch++;
        }

        if (batch == 0) {
            // Should not happen if we ACK properly - safety break
            break;
        }

        // Simulate one round-trip: peer ACKs all outstanding frames
        result.round_trips++;

        atc_hdlc_u32 rr_len = (atc_hdlc_u32)test_pack_frame(0x01, S_CTRL(0x00, ctx.vs, 0),
                                                              NULL, 0,
                                                              temp_input_buffer, sizeof(temp_input_buffer));
        atc_hdlc_data_in(&ctx, temp_input_buffer, rr_len);
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

void test_nr_modulo_validation(void) {
    printf("\nTEST: N(R) Modulo Validation Bug\n");
    reset_test_state();

    atc_hdlc_context_t ctx;
    make_ctx(&ctx, 7, ATC_HDLC_DEFAULT_T1_TIMEOUT);

    // 1. Send 6 frames (0 to 5)
    char payload[8] = "DATA";
    for (int i = 0; i < 6; i++) {
        atc_hdlc_transmit_i(&ctx, (atc_hdlc_u8 *)payload, 4);
    }
    // 2. Peer ACKs them: N(R)=6
    atc_hdlc_u32 len = (atc_hdlc_u32)test_pack_frame(0x01, S_CTRL(0x00, 6, 0),
                                                       NULL, 0,
                                                       temp_input_buffer, sizeof(temp_input_buffer));
    atc_hdlc_data_in(&ctx, temp_input_buffer, len);
    
    if (ctx.va != 6 || ctx.vs != 6) { test_fail("NR Bug Test", "Setup failed"); return; }
    
    // 3. Send 2 more frames (Seq 6, and Seq 7)
    atc_hdlc_transmit_i(&ctx, (atc_hdlc_u8 *)"DATA6", 5);
    atc_hdlc_transmit_i(&ctx, (atc_hdlc_u8 *)"DATA7", 5);
    
    // Now V(A)=6, V(S)=0 (wrap around). Outstanding: 6, 7.
    if (ctx.va != 6 || ctx.vs != 0) { test_fail("NR Bug Test", "Wrap around setup failed"); return; }

    // Tick the timer so we can observe if it resets
    atc_hdlc_t1_expired(&ctx); /* simulated T1 expiry */
    atc_hdlc_bool timer_was_active = ctx.t1_active;

    // 4. Peer sends RR N(R)=6. This is perfectly valid (peer acknowledging up to 5, waiting for 6)
    // If hdlc_nr_valid has the bug, it will reject N(R)=6 because it thinks 6 is outside [6, 0].
    len = (atc_hdlc_u32)test_pack_frame(0x01, S_CTRL(0x00, 6, 0),
                                         NULL, 0,
                                         temp_input_buffer, sizeof(temp_input_buffer));
    atc_hdlc_data_in(&ctx, temp_input_buffer, len);

    // If valid, the timer should be reset to timeout. If ignored, the timer continues ticking down.
    if (!ctx.t1_active) {
        test_fail("NR Modulo Bug", "Ignored valid N(R)=6 due to modulo arithmetic bug");
        return;
    }

    test_pass("NR Modulo Bug Fixed");
}

void test_nr_edge_cases(void) {
    printf("\nTEST: N(R) Edge Cases\n");
    reset_test_state();

    atc_hdlc_context_t ctx;
    make_ctx(&ctx, 7, 1000);

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
        ctx.t1_active = false; /* reset to observe change */

        atc_hdlc_u32 len = (atc_hdlc_u32)test_pack_frame(0x01, S_CTRL(0x00, cases[i].nr, 0),
                                                           NULL, 0,
                                                           temp_input_buffer, sizeof(temp_input_buffer));
        atc_hdlc_data_in(&ctx, temp_input_buffer, len);

        bool was_treated_as_valid = false;
        
        /* Determine if frame was treated as valid:
         * - If V(A) changed: definitely accepted (V(A) := N(R))
         * - If N(R) == V(A): frame was accepted but V(A) unchanged (N(R) == V(A)
         *   is a valid no-op ACK per ISO 13239). T1 is NOT restarted in this case.
         * - If invalid: context state is unchanged (FRMR_ERROR transition would
         *   change state, which we detect). */
        if (ctx.va != cases[i].va) {
            /* V(A) advanced — frame accepted */
            was_treated_as_valid = true;
        } else if (cases[i].nr == cases[i].va) {
            /* N(R) == V(A): valid no-op ACK — state unchanged is correct */
            was_treated_as_valid = (ctx.current_state != ATC_HDLC_STATE_FRMR_ERROR);
        }
        /* else: N(R) invalid → FRMR_ERROR or no state change — was_treated_as_valid stays false */

        /* Reset context state for next iteration */
        if (ctx.current_state == ATC_HDLC_STATE_FRMR_ERROR)
            ctx.current_state = ATC_HDLC_STATE_CONNECTED;

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
make_ctx(&ctx, ATC_HDLC_DEFAULT_WINDOW_SIZE, ATC_HDLC_DEFAULT_T1_TIMEOUT);

    // 1. Send an I-frame so that V(S) increments to 1
    atc_hdlc_transmit_i(&ctx, (atc_hdlc_u8 *)"TEST", 4);
    
    // Simulate peer sending an I-frame so V(R) increments to 1
    atc_hdlc_u32 len = (atc_hdlc_u32)test_pack_frame(0x01, I_CTRL(0, 0, 0),
                                                       (atc_hdlc_u8 *)"PEER", 4,
                                                       temp_input_buffer, sizeof(temp_input_buffer));
    atc_hdlc_data_in(&ctx, temp_input_buffer, len);

    // Simulate peer sending RR N(R)=1 so V(A) increments to 1
    len = (atc_hdlc_u32)test_pack_frame(0x01, S_CTRL(0x00, 1, 0),
                                         NULL, 0,
                                         temp_input_buffer, sizeof(temp_input_buffer));
    atc_hdlc_data_in(&ctx, temp_input_buffer, len);

    if (ctx.vs == 0 || ctx.vr == 0 || ctx.va == 0) {
        test_fail("State Init Setup", "Failed to advance state variables before reset");
        return;
    }

    // 2. Peer sends SABM to reset connection
    printf("   Peer sends SABM to trigger reset.\n");
    len = (atc_hdlc_u32)test_pack_frame(0x01, U_CTRL(U_SABM, 1),
                                         NULL, 0,
                                         temp_input_buffer, sizeof(temp_input_buffer));
    atc_hdlc_data_in(&ctx, temp_input_buffer, len);

    // After processing SABM, we should be connected and state variables MUST be 0
    if (ctx.vs != 0 || ctx.vr != 0 || ctx.va != 0) {
        char msg[128];
        sprintf(msg, "Variables not reset: V(S)=%d, V(R)=%d, V(A)=%d", ctx.vs, ctx.vr, ctx.va);
        test_fail("State Init", msg);
        return;
    }

    test_pass("SABM/UA State Initialization Working");
}

/**
 * @brief Test: Public query API — get_state, get_window_available,
 *        has_pending_ack, get_stats.
 */
void test_public_query_api(void) {
    printf("\nTEST: Public Query API\n");

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    ctx.peer_address = 0x02;
    ctx.current_state = ATC_HDLC_STATE_CONNECTED;

    /* atc_hdlc_get_state */
    if (atc_hdlc_get_state(&ctx) != ATC_HDLC_STATE_CONNECTED)
        test_fail("Query API", "get_state returned wrong value");

    /* T2 starts when I-frame is received */
    atc_hdlc_u8 payload[] = {0x01};
    if (ctx.t2_active)
        test_fail("Query API", "t2_active should be false before receiving I-frame");

    /* Simulate receiving an I-frame → T2 starts */
    atc_hdlc_u8 i_raw[64];
    atc_hdlc_u32 i_len = (atc_hdlc_u32)test_pack_frame(0x01, I_CTRL(0, 0, 0),
                                                         payload, 1, i_raw, sizeof(i_raw));
    reset_test_state();
    atc_hdlc_data_in(&ctx, i_raw, i_len);
    if (!ctx.t2_active)
        test_fail("Query API", "t2_active should be true after I-frame");

    /* NULL safety */
    if (atc_hdlc_get_state(NULL) != ATC_HDLC_STATE_DISCONNECTED)
        test_fail("Query API", "get_state(NULL) should return DISCONNECTED");
    test_pass("Public Query API");
}

/**
 * @brief Test: atc_hdlc_set_local_busy() — RNR sent when local busy,
 *        RR sent when cleared.
 */
void test_set_local_busy(void) {
    printf("\nTEST: Local Busy (set_local_busy)\n");

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    ctx.peer_address = 0x02;
    ctx.current_state = ATC_HDLC_STATE_CONNECTED;

    /* Must fail in non-CONNECTED state */
    ctx.current_state = ATC_HDLC_STATE_DISCONNECTED;
    if (atc_hdlc_set_local_busy(&ctx, true) != ATC_HDLC_ERR_INVALID_STATE)
        test_fail("Local Busy", "Should reject in DISCONNECTED");
    ctx.current_state = ATC_HDLC_STATE_CONNECTED;

    /* Assert busy — no RNR in output (set_local_busy itself doesn't transmit RNR,
     * it just sets the flag; RNR goes out when next I-frame arrives) */
    reset_test_state();
    atc_hdlc_error_t err = atc_hdlc_set_local_busy(&ctx, true);
    if (err != ATC_HDLC_OK)
        test_fail("Local Busy", "set_local_busy(true) failed");
    if (!ctx.local_busy)
        test_fail("Local Busy", "local_busy flag not set");

    /* Clear busy — RR must be sent */
    reset_test_state();
    err = atc_hdlc_set_local_busy(&ctx, false);
    if (err != ATC_HDLC_OK)
        test_fail("Local Busy", "set_local_busy(false) failed");
    if (ctx.local_busy)
        test_fail("Local Busy", "local_busy flag not cleared");
    if (mock_output_len < 6)
        test_fail("Local Busy", "RR not sent on busy-clear");

    /* Verify output is RR (S-frame) */
    atc_hdlc_u8 flat[32];
    test_frame_t resp = test_unpack_frame(mock_output_buffer, mock_output_len, flat, sizeof(flat));
    if (resp.valid) {
        if (CTRL_S(resp.control) != S_RR)
            test_fail("Local Busy", "Cleared-busy frame is not RR");
    }

    /* Idempotent calls should not re-set or double-clear */
    reset_test_state();
    atc_hdlc_set_local_busy(&ctx, false); /* already false */
    if (mock_output_len != 0)
        test_fail("Local Busy", "Double-clear should not send extra RR");

    test_pass("Local Busy (set_local_busy)");
}

/**
 * @brief Test: I-frame received while locally busy → RNR sent in response.
 *        Peer must not send more data until we clear busy.
 */
void test_local_busy_rnr_response(void) {
    printf("\nTEST: Local Busy — RNR response to incoming I-frame\n");

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    ctx.peer_address  = 0x02;
    ctx.current_state = ATC_HDLC_STATE_CONNECTED;

    atc_hdlc_set_local_busy(&ctx, true);

    /* Peer sends I-frame N(S)=0, N(R)=0 */
    atc_hdlc_u8 payload[] = {0xAB};
    atc_hdlc_u8 i_raw[64];
    atc_hdlc_u32 i_len = (atc_hdlc_u32)test_pack_frame(0x01, I_CTRL(0, 0, 0),
                                                         payload, 1, i_raw, sizeof(i_raw));

    reset_test_state();
    atc_hdlc_data_in(&ctx, i_raw, i_len);

    /* Must have sent a response */
    if (mock_output_len < 6)
        test_fail("Local Busy RNR", "No response sent while locally busy");

    /* Response must be RNR, not RR */
    atc_hdlc_u8 flat[32];
    test_frame_t resp = test_unpack_frame(mock_output_buffer, mock_output_len, flat, sizeof(flat));
    if (!resp.valid)
        test_fail("Local Busy RNR", "Failed to unpack response");
    if (CTRL_S(resp.control) != S_RNR)
        test_fail("Local Busy RNR", "Response is not RNR while locally busy");

    test_pass("Local Busy — RNR response to incoming I-frame");
}

/**
 * @brief Test: RNR reception sets remote_busy; clears on RR.
 *        Verifies ATC_HDLC_ERR_REMOTE_BUSY returned from transmit_i.
 */
void test_rnr_reception(void) {
    printf("\nTEST: RNR Reception (remote_busy)\n");

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    ctx.peer_address = 0x02;
    ctx.current_state = ATC_HDLC_STATE_CONNECTED;

    /* Build RNR(P=0, N(R)=0) from peer (address = my_address = 0x01) */
    atc_hdlc_u8 rnr_raw[32];
    atc_hdlc_u32 rnr_len = (atc_hdlc_u32)test_pack_frame(0x01, S_CTRL(S_RNR, 0, 0),
                                                           NULL, 0, rnr_raw, sizeof(rnr_raw));

    reset_test_state();
    atc_hdlc_data_in(&ctx, rnr_raw, rnr_len);

    if (!ctx.remote_busy)
        test_fail("RNR Reception", "remote_busy not set after RNR");

    /* transmit_i must return REMOTE_BUSY */
    atc_hdlc_u8 payload[] = {0xAA};
    atc_hdlc_error_t err = atc_hdlc_transmit_i(&ctx, payload, 1);
    if (err != ATC_HDLC_ERR_REMOTE_BUSY)
        test_fail("RNR Reception", "transmit_i should return REMOTE_BUSY");

    /* Build RR(P=0, N(R)=0) from peer to clear busy */
    atc_hdlc_u8 rr_raw[32];
    atc_hdlc_u32 rr_len = (atc_hdlc_u32)test_pack_frame(0x01, S_CTRL(S_RR, 0, 0),
                                                          NULL, 0, rr_raw, sizeof(rr_raw));
    atc_hdlc_data_in(&ctx, rr_raw, rr_len);

    /* remote_busy should clear on RR */
    if (ctx.remote_busy)
        test_fail("RNR Reception", "remote_busy not cleared by RR");

    /* transmit_i should succeed now */
    err = atc_hdlc_transmit_i(&ctx, payload, 1);
    if (err != ATC_HDLC_OK)
        test_fail("RNR Reception", "transmit_i should succeed after RR");

    test_pass("RNR Reception (remote_busy)");
}

/**
 * @brief Test: T2 platform callbacks fire on I-frame receipt and stop on
 *        piggybacked ACK send.
 */
void test_t2_timer_callbacks(void) {
    printf("\nTEST: T2 Timer Callbacks\n");

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    ctx.peer_address = 0x02;
    ctx.current_state = ATC_HDLC_STATE_CONNECTED;

    /* Receive I-frame → T2 must start */
    atc_hdlc_u8 payload[] = {0x01, 0x02};
    atc_hdlc_u8 i_raw[64];
    atc_hdlc_u32 i_len = (atc_hdlc_u32)test_pack_frame(0x01, I_CTRL(0, 0, 0),
                                                         payload, 2, i_raw, sizeof(i_raw));

    reset_test_state();
    atc_hdlc_data_in(&ctx, i_raw, i_len);
    if (mock_t2_start_count < 1)
        test_fail("T2 Callbacks", "T2 not started after I-frame");
    if (!ctx.t2_active)
        test_fail("T2 Callbacks", "t2_active not set");

    /* atc_hdlc_t2_expired → RR sent, T2 clears */
    reset_test_state();
    atc_hdlc_t2_expired(&ctx);
    if (ctx.t2_active)
        test_fail("T2 Callbacks", "t2_active not cleared after expiry");
    if (mock_output_len < 6)
        test_fail("T2 Callbacks", "RR not sent on T2 expiry");

    /* Verify output is RR */
    atc_hdlc_u8 flat[32];
    test_frame_t resp = test_unpack_frame(mock_output_buffer, mock_output_len, flat, sizeof(flat));
    if (resp.valid) {
        if (CTRL_S(resp.control) != S_RR)
            test_fail("T2 Callbacks", "T2 expired frame is not RR");
    }

    /* Sending own I-frame stops T2 (ACK piggybacked) */
    reset_test_state();
    ctx.current_state = ATC_HDLC_STATE_CONNECTED;
    ctx.vs = 0; ctx.vr = 0; ctx.va = 0;
    /* Receive I-frame N(S)=0, N(R)=0 → vr becomes 1, T2 starts */
    atc_hdlc_u8 i_raw2[64];
    atc_hdlc_u32 i_len2 = (atc_hdlc_u32)test_pack_frame(0x01, I_CTRL(0, 0, 0),
                                                          payload, 2, i_raw2, sizeof(i_raw2));
    atc_hdlc_data_in(&ctx, i_raw2, i_len2);
    if (!ctx.t2_active)
        test_fail("T2 Callbacks", "T2 not started before piggybacked ACK test");
    int t2_stop_before = mock_t2_stop_count;
    /* Sending outgoing I-frame with N(R)=vr piggybacks ACK → T2 should stop */
    atc_hdlc_u8 out_payload[] = {0x55};
    atc_hdlc_transmit_i(&ctx, out_payload, 1);
    if (mock_t2_stop_count <= t2_stop_before)
        test_fail("T2 Callbacks", "T2 not stopped on piggybacked ACK");

    test_pass("T2 Timer Callbacks");
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
  test_nr_modulo_validation();
  test_nr_edge_cases();
  test_state_initialization();
  test_public_query_api();
  test_set_local_busy();
  test_local_busy_rnr_response();
  test_rnr_reception();
  test_t2_timer_callbacks();
  test_n2_retry_connected();

  printf("\n%sALL RELIABLE TRANSMISSION TESTS PASSED SUCCESSFULLY!%s\n", COL_GREEN, COL_RESET);
  return 0;
}
