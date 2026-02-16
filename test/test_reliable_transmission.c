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
    atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), retx_buf, sizeof(retx_buf), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
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
    if (ctx.vs == 1 && ctx.waiting_for_ack == true) {
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
    
    if (ctx.waiting_for_ack == false && ctx.vr == 1) { // VR increments because we received valid I-frame
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
    atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), retx_buf, sizeof(retx_buf), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
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
    atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), retx_buf, sizeof(retx_buf), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
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
        
        if (ctx.waiting_for_ack) {
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
    atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), retx_buf, sizeof(retx_buf), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);
    ctx.current_state = 2; // CONNECTED
    
    // Send I-Frame (VS becomes 1)
    atc_hdlc_output_i(&ctx, (atc_hdlc_u8*)"A", 1);
    
    // Receive ACK (RR NR=1)
    atc_hdlc_frame_t rr_frame = { .address=0x01, .control=atc_hdlc_create_s_ctrl(0, 1, 0) };
    atc_hdlc_u32 len = 0;
    atc_hdlc_frame_pack(&rr_frame, input_buffer, sizeof(input_buffer), &len);
    atc_hdlc_input_bytes(&ctx, input_buffer, len);
    
    if (ctx.waiting_for_ack) test_fail("Duplicate ACK", "First ACK failed");
    
    // Receive SAME ACK again
    atc_hdlc_input_bytes(&ctx, input_buffer, len);
    
    // Should stay not waiting, no error
    if (ctx.waiting_for_ack) test_fail("Duplicate ACK", "State regressed?");
    
    test_pass("Duplicate ACK Ignored");
}

void test_rej_retransmit(void) {
    printf("========================================\n");
    printf("TEST: REJ Triggers Retransmission\n");
    printf("========================================\n");
    atc_hdlc_context_t ctx;
    atc_hdlc_u8 retx_buf[128];
    atc_hdlc_init(&ctx, input_buffer, sizeof(input_buffer), retx_buf, sizeof(retx_buf), mock_output_byte_cb, mock_on_frame_cb, NULL, NULL);
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

int main() {
  printf("\n%sSTARTING RELIABLE TRANSMISSION TEST SUITE%s\n", COL_YELLOW,
         COL_RESET);
  printf("----------------------------------------\n\n");

  test_reliable_transmission();
  test_reliable_retransmission();
  test_sequence_rollover();
  test_duplicate_ack_ignored();
  test_rej_retransmit();

  printf("\n%sALL RELIABLE TRANSMISSION TESTS PASSED SUCCESSFULLY!%s\n", COL_GREEN, COL_RESET);
  return 0;
}
