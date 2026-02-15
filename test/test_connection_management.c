#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include "../inc/hdlc.h"
#include "../src/hdlc_private.h"

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
    (void)frame;
    (void)user_data;
}


void on_state_change(atc_hdlc_protocol_state_t state, void *user_data) {
    (void)user_data;
    last_state_change = state;
    state_change_call_count++;
}

// Helper to reset test state
void setup_context(void) {
    atc_hdlc_init(&ctx, rx_buffer, sizeof(rx_buffer), on_tx_byte, on_rx_frame, on_state_change, NULL);
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02); // Me=0x01, Peer=0x02
    captured_tx_len = 0;
    state_change_call_count = 0;
    last_state_change = (atc_hdlc_protocol_state_t)-1;
}

// Helper to inspect the last transmitted frame (assumes it's a valid frame)
// Returns pointer to the frame starting at captured_tx_buffer
void decode_last_tx(atc_hdlc_frame_t *decoded_frame, uint8_t *flat_buf, uint32_t flat_len) {
    bool res = atc_hdlc_frame_unpack(captured_tx_buffer, captured_tx_len, decoded_frame, flat_buf, flat_len);
    assert(res && "Failed to unpack transmitted frame");
}

// -----------------------------------------------------------------------------
// Assertions
// -----------------------------------------------------------------------------
#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL: %s:%d: %d != %d\n", __FILE__, __LINE__, (int)(a), (int)(b)); \
        exit(1); \
    } \
} while(0)

#define ASSERT_TRUE(a) do { \
    if (!(a)) { \
        printf("FAIL: %s:%d: Expected true\n", __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

#define PASS() printf("PASS: %s\n", __func__)

// -----------------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------------

void test_init_state(void) {
    setup_context();
    ASSERT_EQ(ctx.current_state, HDLC_STATE_DISCONNECTED);
    ASSERT_TRUE(!atc_hdlc_is_connected(&ctx));
    PASS();
}

void test_connect_sends_sabm(void) {
    setup_context();
    
    bool res = atc_hdlc_connect(&ctx);
    ASSERT_TRUE(res);
    
    // 1. Check State Transition
    ASSERT_EQ(ctx.current_state, HDLC_STATE_CONNECTING);
    ASSERT_EQ(state_change_call_count, 1);
    ASSERT_EQ(last_state_change, HDLC_STATE_CONNECTING);

    // 2. Check Output Frame (SABM to Peer)
    atc_hdlc_frame_t frame_out;
    uint8_t flat[32];
    decode_last_tx(&frame_out, flat, sizeof(flat));

    ASSERT_EQ(frame_out.address, 0x02); // To Peer
    // SABM (P=1) -> 0x3F
    ASSERT_EQ(frame_out.control.value, 0x3F); 
    PASS();
}

void test_connect_complete_on_ua(void) {
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
    ASSERT_EQ(ctx.current_state, HDLC_STATE_CONNECTED);
    ASSERT_EQ(state_change_call_count, 1);
    ASSERT_EQ(last_state_change, HDLC_STATE_CONNECTED);
    ASSERT_TRUE(atc_hdlc_is_connected(&ctx));
    PASS();
}

void test_disconnect_flow(void) {
    setup_context();
    // Force Connected
    ctx.current_state = HDLC_STATE_CONNECTED;
    state_change_call_count = 0;

    // Send Disconnect
    bool res = atc_hdlc_disconnect(&ctx);
    ASSERT_TRUE(res);

    // 1. Check State
    ASSERT_EQ(ctx.current_state, HDLC_STATE_DISCONNECTING);
    
    // 2. Check Output Frame (DISC to Peer)
    atc_hdlc_frame_t frame_out;
    uint8_t flat[32];
    decode_last_tx(&frame_out, flat, sizeof(flat));
    ASSERT_EQ(frame_out.address, 0x02);
    // DISC(P=1) = 0x53
    ASSERT_EQ(frame_out.control.value, 0x53);

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
    ASSERT_EQ(ctx.current_state, HDLC_STATE_DISCONNECTED);
    PASS();
}

void test_passive_open(void) {
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
    ASSERT_EQ(ctx.current_state, HDLC_STATE_CONNECTED);
    ASSERT_EQ(state_change_call_count, 1);

    // 2. Should have sent UA (Response from Me)
    atc_hdlc_frame_t frame_out;
    uint8_t flat[32];
    decode_last_tx(&frame_out, flat, sizeof(flat));

    ASSERT_EQ(frame_out.address, 0x01); // My address
    ASSERT_EQ(frame_out.control.value, 0x73); // UA(F=1)
    PASS();
}

void test_frmr_reception(void) {
    setup_context();
    atc_hdlc_connect(&ctx); // Connect first
    // Force Connected state for testing
    ctx.current_state = HDLC_STATE_CONNECTED;
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
    ASSERT_EQ(ctx.current_state, HDLC_STATE_DISCONNECTED);
    ASSERT_EQ(state_change_call_count, 1);
    PASS();
}

void test_mode_rejection(void) {
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

    for(int i=0; i<captured_tx_len; i++) printf("%02X ", captured_tx_buffer[i]);
    printf("\n");

    // 1. State should remain DISCONNECTED (or not change if connected, but standard says DM implies disconnected)
    ASSERT_EQ(ctx.current_state, HDLC_STATE_DISCONNECTED);

    // 2. Output should be DM
    atc_hdlc_frame_t frame_out;
    uint8_t flat[32];
    decode_last_tx(&frame_out, flat, sizeof(flat));

    // DM: 000 F 00 11 (Hi=0, Lo=3). F should match P (1).
    // 000 1 11 11 -> 0x1F.
    ASSERT_EQ(frame_out.control.value, 0x1F); // DM with F=1
    PASS();
}

int main(void) {
    test_init_state();
    test_connect_sends_sabm();
    test_connect_complete_on_ua();
    test_disconnect_flow();
    test_passive_open();
    test_frmr_reception();
    test_mode_rejection();
    printf("\nAll Connection State Tests Passed!\n");
    return 0;
}
