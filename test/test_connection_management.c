#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include "../inc/hdlc.h"
#include "../src/hdlc_private.h"
#include "test_common.h"

// -----------------------------------------------------------------------------
// Mocks & Helpers
// -----------------------------------------------------------------------------
static atc_hdlc_context_t ctx;
static uint8_t rx_buffer[1024];
static uint8_t captured_tx_buffer[256];
static uint32_t captured_tx_len = 0;
static atc_hdlc_protocol_state_t last_state_change = (atc_hdlc_protocol_state_t)-1;
static int state_change_call_count = 0;

void on_tx_byte(uint8_t byte, bool flush, void *user_data) {
    (void)user_data;
    (void)flush;
    if (captured_tx_len < sizeof(captured_tx_buffer)) {
        captured_tx_buffer[captured_tx_len++] = byte;
    }
}

void on_rx_frame(const atc_hdlc_frame_t *frame, void *user_data) {
    (void)user_data;
    // Log frame using standard format
    printf("   %s[ON FRAME EVENT] Frame Received!%s\n", COL_GREEN, COL_RESET);
    printf("   Type: %d, Addr: %02X, Ctrl: %02X, Information Len: %d\n",
           frame->type, frame->address, frame->control.value,
           frame->information_len);
}


void on_state_change(atc_hdlc_protocol_state_t state, void *user_data) {
    (void)user_data;
    last_state_change = state;
    state_change_call_count++;
    printf("   %s[STATE CHANGE] New State: %d%s\n", COL_YELLOW, state, COL_RESET);
    switch(state) {
        case ATC_HDLC_PROTOCOL_STATE_CONNECTED:
            printf("   %sConnected!%s\n", COL_GREEN, COL_RESET);
            break;
        case ATC_HDLC_PROTOCOL_STATE_DISCONNECTED:
            printf("   %sDisconnected!%s\n", COL_RED, COL_RESET);
            break;
        case ATC_HDLC_PROTOCOL_STATE_CONNECTING:
            printf("   %sConnecting!%s\n", COL_YELLOW, COL_RESET);
            break;
        case ATC_HDLC_PROTOCOL_STATE_DISCONNECTING:
            printf("   %sDisconnecting!%s\n", COL_YELLOW, COL_RESET);
            break;
        default:
            printf("   %sUnknown State!%s\n", COL_YELLOW, COL_RESET);
            break;
    }
}

// Helper to reset test state
void setup_context(void) {
    atc_hdlc_init(&ctx, rx_buffer, sizeof(rx_buffer), NULL, 0, on_tx_byte, on_rx_frame, on_state_change, NULL);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02); // Me=0x01, Peer=0x02
    captured_tx_len = 0;
    state_change_call_count = 0;
    last_state_change = (atc_hdlc_protocol_state_t)-1;
}

// Helper to inspect the last transmitted frame (assumes it's a valid frame)
// Returns pointer to the frame starting at captured_tx_buffer
void decode_last_tx(atc_hdlc_frame_t *decoded_frame, uint8_t *flat_buf, uint32_t flat_len) {
    bool res = atc_hdlc_frame_unpack(captured_tx_buffer, captured_tx_len, decoded_frame, flat_buf, flat_len);
    if (!res) {
        test_fail("Frame Decode", "Failed to unpack transmitted frame");
    }
}

// -----------------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------------

void test_init_state(void) {
    printf("========================================\n");
    printf("TEST: Init State\n");
    printf("========================================\n");
    setup_context();
    
    if (ctx.current_state != HDLC_PROTOCOL_STATE_DISCONNECTED) 
        test_fail("Init State", "Initial state is not DISCONNECTED");
    
    if (atc_hdlc_is_connected(&ctx))
        test_fail("Init State", "Reported connected initially");
        
    test_pass("Init State");
}

void test_connect_sends_sabm(void) {
    printf("========================================\n");
    printf("TEST: Connect Sends SABM\n");
    printf("========================================\n");
    setup_context();
    
    bool res = atc_hdlc_connect(&ctx);
    if (!res) test_fail("Connect Sends SABM", "Connect failed to start");
    
    // 1. Check State Transition
    if (ctx.current_state != HDLC_PROTOCOL_STATE_CONNECTING)
        test_fail("Connect Sends SABM", "State not CONNECTING");
        
    if (state_change_call_count != 1)
        test_fail("Connect Sends SABM", "State change callback count incorrect");

    if (last_state_change != HDLC_PROTOCOL_STATE_CONNECTING)
        test_fail("Connect Sends SABM", "Last state change not CONNECTING");
        
    // 2. Check Output Frame (SABM to Peer)
    atc_hdlc_frame_t frame_out;
    uint8_t flat[32];
    decode_last_tx(&frame_out, flat, sizeof(flat));

    if (frame_out.address != 0x02) test_fail("Connect Sends SABM", "Wrong Dest Address");   // To Peer
    if (frame_out.control.value != 0x3F) test_fail("Connect Sends SABM", "Not SABM(P=1)");  // SABM (P=1) -> 0x3F
    
    test_pass("Connect Sends SABM");
}

void test_connect_complete_on_ua(void) {
    printf("========================================\n");
    printf("TEST: Connect Complete on UA\n");
    printf("========================================\n");
    setup_context();
    atc_hdlc_connect(&ctx); // Go to CONNECTING
    captured_tx_len = 0; // Clear TX buffer
    state_change_call_count = 0; // Clear counters

    // Simulate Receiving UA from Peer
    atc_hdlc_frame_t ua_frame;
    ua_frame.address = 0x02; // Peer's address (Response)
    ua_frame.control.value = 0x73; // UA with F=1
    ua_frame.information = NULL;
    ua_frame.information_len = 0;
    ua_frame.type = HDLC_FRAME_U;

    uint8_t packed[32];
    uint32_t packed_len = 0;
    atc_hdlc_frame_pack(&ua_frame, packed, sizeof(packed), &packed_len);

    // Feed bytes
    atc_hdlc_input_bytes(&ctx, packed, packed_len);

    // Verify State Change
    if (ctx.current_state != HDLC_PROTOCOL_STATE_CONNECTED)
         test_fail("Connect Complete FA", "State not CONNECTED");
         
    if (state_change_call_count != 1)
         test_fail("Connect Complete UA", "Callback count mismatch");
         
    if (!atc_hdlc_is_connected(&ctx))
         test_fail("Connect Complete UA", "Helper returned not connected");

    test_pass("Connect Complete on UA");
}

void test_disconnect_flow(void) {
    printf("========================================\n");
    printf("TEST: Disconnect Flow\n");
    printf("========================================\n");
    setup_context();
    // Force Connected
    ctx.current_state = HDLC_PROTOCOL_STATE_CONNECTED;
    state_change_call_count = 0;

    // Send Disconnect
    bool res = atc_hdlc_disconnect(&ctx);
    if (!res) test_fail("Disconnect Flow", "Disconnect returned false");

    // 1. Check State
    if (ctx.current_state != HDLC_PROTOCOL_STATE_DISCONNECTING)
         test_fail("Disconnect Flow", "State not DISCONNECTING");
    
    // 2. Check Output Frame (DISC to Peer)
    atc_hdlc_frame_t frame_out;
    uint8_t flat[32];
    decode_last_tx(&frame_out, flat, sizeof(flat));
    
    // DISC(P=1) = 0x53
    if (frame_out.address != 0x02) test_fail("Disconnect Flow", "Wrong Address");
    if (frame_out.control.value != 0x53) test_fail("Disconnect Flow", "Not DISC(P=1)");

    // 3. Receive UA
    // Clear buffer
    captured_tx_len = 0;
    
    atc_hdlc_frame_t ua_frame;
    ua_frame.address = 0x02;
    ua_frame.control.value = 0x73; // UA(F=1)
    ua_frame.information = NULL;
    ua_frame.information_len = 0;

    uint8_t packed[32];
    uint32_t packed_len = 0;
    atc_hdlc_frame_pack(&ua_frame, packed, sizeof(packed), &packed_len);
    atc_hdlc_input_bytes(&ctx, packed, packed_len);

    // Check State
    if (ctx.current_state != HDLC_PROTOCOL_STATE_DISCONNECTED)
         test_fail("Disconnect Flow", "State NOT disconnected after UA");

    test_pass("Disconnect Flow");
}

void test_passive_open(void) {
    printf("========================================\n");
    printf("TEST: Passive Open (Accept SABM)\n");
    printf("========================================\n");
    setup_context();
    
    // Simulate Receiving SABM from Peer (Command)
    // Addressed to ME (0x01).
    // SABM(P=1) = 0x3F.
    atc_hdlc_frame_t sabm_frame;
    sabm_frame.address = 0x01;
    sabm_frame.control.value = 0x3F;
    sabm_frame.information = NULL;
    sabm_frame.information_len = 0;

    uint8_t packed[32];
    uint32_t packed_len = 0;
    atc_hdlc_frame_pack(&sabm_frame, packed, sizeof(packed), &packed_len);
    
    atc_hdlc_input_bytes(&ctx, packed, packed_len);

    // 1. Should be CONNECTED
    if (ctx.current_state != HDLC_PROTOCOL_STATE_CONNECTED)
         test_fail("Passive Open", "State not CONNECTED after SABM");

    // 2. Should have sent UA (Response from Me)
    atc_hdlc_frame_t frame_out;
    uint8_t flat[32];
    decode_last_tx(&frame_out, flat, sizeof(flat));

    if (frame_out.address != 0x01) test_fail("Passive Open", "UA wrong address");   // My address
    if (frame_out.control.value != 0x73) test_fail("Passive Open", "Not UA(F=1)");  // UA(F=1)
    
    test_pass("Passive Open (Accept SABM)");
}

void test_frmr_reception(void) {
    printf("========================================\n");
    printf("TEST: FRMR Reception\n");
    printf("========================================\n");
    setup_context();
    atc_hdlc_connect(&ctx); // Connect first
    // Force Connected state for testing
    ctx.current_state = HDLC_PROTOCOL_STATE_CONNECTED;
    state_change_call_count = 0; // Clear counters

    // Simulate Receiving FRMR from Peer
    // Payload: 3 bytes (Rejected Ctrl, V(S)/V(R), Flags)
    // Byte 0: Rejected Control = 0x11 (Random)
    // Byte 1: 0 V(S) C/R V(R) -> 0 001 1 010 -> 0001 1010 = 0x1A (V(S)=1, C/R=1, V(R)=2)
    // Byte 2: W X Y Z V 0 0 0 -> 1 0 0 1 0 0 0 0 -> 1001 0000 = 0x90 (W=1, Z=1)
    
    uint8_t frmr_payload[] = {0x11, 0x1A, 0x90};

    atc_hdlc_frame_t frmr_frame;
    frmr_frame.address = 0x02; // From Peer
    frmr_frame.control = atc_hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_FRMR, HDLC_U_MODIFIER_HI_FRMR, 0); // F=0
    frmr_frame.information = frmr_payload;
    frmr_frame.information_len = sizeof(frmr_payload);
    frmr_frame.type = HDLC_FRAME_U;

    uint8_t packed[32];
    uint32_t packed_len = 0;
    atc_hdlc_frame_pack(&frmr_frame, packed, sizeof(packed), &packed_len);

    // Feed bytes
    atc_hdlc_input_bytes(&ctx, packed, packed_len);

    // Verify State Change -> DISCONNECTED
    if (ctx.current_state != HDLC_PROTOCOL_STATE_DISCONNECTED)
         test_fail("FRMR Reception", "State not DISCONNECTED");

    if (state_change_call_count != 1)
         test_fail("FRMR Reception", "State change callback count incorrect");
         
    test_pass("FRMR Reception");
}

void test_mode_rejection(void) {
    printf("========================================\n");
    printf("TEST: Mode Rejection (SNRM)\n");
    printf("========================================\n");
    setup_context();
    // Use SNRM (Set Normal Response Mode) - Not Supported
    // M=100 00 -> Hi=4, Lo=0. P=1.
    // Ctrl: 100 1 00 11 -> 1001 0011 -> 0x93
    
    atc_hdlc_frame_t params_frame;
    params_frame.address = 0x01; // To Me
    params_frame.control = atc_hdlc_create_u_ctrl(0, 4, 1); // SNRM, P=1, Hi=4, Lo=0
    params_frame.information = NULL;
    params_frame.information_len = 0;
    params_frame.type = HDLC_FRAME_U;

    uint8_t packed[32];
    uint32_t packed_len = 0;
    atc_hdlc_frame_pack(&params_frame, packed, sizeof(packed), &packed_len);
    
    // Clear TX capture
    captured_tx_len = 0;

    // Feed bytes
    atc_hdlc_input_bytes(&ctx, packed, packed_len);
    
    // Inspect captured bytes dump using helper
    print_hexdump("Captured TX", captured_tx_buffer, captured_tx_len);

    // 1. State should remain DISCONNECTED
    if (ctx.current_state != HDLC_PROTOCOL_STATE_DISCONNECTED)
         test_fail("Mode Rejection", "State changed on invalid mode!");

    // 2. Output should be DM
    atc_hdlc_frame_t frame_out;
    uint8_t flat[32];
    decode_last_tx(&frame_out, flat, sizeof(flat));

    // DM: 000 F 00 11 (Hi=0, Lo=3). F should match P (1).
    // 000 1 11 11 -> 0x1F.
    if (frame_out.control.value != 0x1F) // DM with F=1
         test_fail("Mode Rejection", "Did not send DM");
    
    test_pass("Mode Rejection (SNRM)");
}

int main(void) {
    printf("\n%sSTARTING CONNECTION MANAGEMENT TESTS%s\n", COL_YELLOW, COL_RESET);
    printf("----------------------------------------\n\n");
    
    test_init_state();
    test_connect_sends_sabm();
    test_connect_complete_on_ua();
    test_disconnect_flow();
    test_passive_open();
    test_frmr_reception();
    test_mode_rejection();
    
    printf("\n%sALL TESTS PASSED SUCCESSFULLY!%s\n", COL_GREEN, COL_RESET);
    return 0;
}
