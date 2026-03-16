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

static atc_hdlc_state_t last_state_change = (atc_hdlc_state_t)-1;
static int state_change_call_count = 0;

/* on_event callback — matches atc_hdlc_on_event_fn signature.
 * We derive the "new state" from the event type for test assertions. */
void on_state_change(atc_hdlc_event_t event, void *user_data) {
    (void)user_data;
    state_change_call_count++;

    /* Map events to states for backward-compatible test assertions */
    switch (event) {
        case ATC_HDLC_EVENT_CONNECT_ACCEPTED:
        case ATC_HDLC_EVENT_INCOMING_CONNECT:
            last_state_change = ATC_HDLC_STATE_CONNECTED;
            printf("   %s[EVENT] Connected (event %d)%s\n", COL_GREEN, event, COL_RESET);
            break;
        case ATC_HDLC_EVENT_LINK_SETUP_REQUEST:
            last_state_change = ATC_HDLC_STATE_CONNECTING;
            printf("   %s[EVENT] Connecting (event %d)%s\n", COL_YELLOW, event, COL_RESET);
            break;
        case ATC_HDLC_EVENT_DISCONNECT_REQUEST:
            last_state_change = ATC_HDLC_STATE_DISCONNECTING;
            printf("   %s[EVENT] Disconnecting (event %d)%s\n", COL_YELLOW, event, COL_RESET);
            break;
        case ATC_HDLC_EVENT_DISCONNECT_COMPLETE:
        case ATC_HDLC_EVENT_PEER_DISCONNECT:
        case ATC_HDLC_EVENT_PEER_REJECT:
        case ATC_HDLC_EVENT_LINK_FAILURE:
            last_state_change = ATC_HDLC_STATE_DISCONNECTED;
            printf("   %s[EVENT] Disconnected (event %d)%s\n", COL_RED, event, COL_RESET);
            break;
        case ATC_HDLC_EVENT_PROTOCOL_ERROR:
            last_state_change = ATC_HDLC_STATE_FRMR_ERROR;
            printf("   %s[EVENT] FRMR Error (event %d)%s\n", COL_RED, event, COL_RESET);
            break;
        default:
            printf("   %s[EVENT] Event %d%s\n", COL_YELLOW, event, COL_RESET);
            break;
    }
}

/* Helper to reset test state (custom for this file to inject on_state_change). */
void setup_context(void) {
    static atc_hdlc_u8  s_retx_slots[1 * 1024];
    static atc_hdlc_u32 s_retx_lens[1];
    static atc_hdlc_u8  s_retx_seq[1];

    static const atc_hdlc_config_t cfg = {
        .mode = ATC_HDLC_MODE_ABM, .address = 0x01, .window_size = 1,
        .max_frame_size = 1024, .max_retries = 3,
        .t1_ms = ATC_HDLC_DEFAULT_RETRANSMIT_TIMEOUT,
        .t2_ms = ATC_HDLC_DEFAULT_ACK_DELAY_TIMEOUT,
        .t3_ms = 30000, .use_extended = false,
    };
    static const atc_hdlc_platform_t plat = {
        .on_send = mock_send_cb,
        .on_data  = mock_on_data_cb,
        .on_event = on_state_change,
        .user_ctx = NULL,
    };
    static atc_hdlc_tx_window_t tw = {
        .slots = s_retx_slots, .slot_lens = s_retx_lens,
        .seq_to_slot = s_retx_seq, .slot_capacity = 1024, .slot_count = 1,
    };
    static atc_hdlc_rx_buffer_t rx = {
        .buffer = mock_rx_buffer, .capacity = sizeof(mock_rx_buffer),
    };

    atc_hdlc_init(&ctx, &cfg, &plat, &tw, &rx);
    ctx.peer_address = 0x02; /* peer address for tests */

    reset_test_state();
    state_change_call_count = 0;
    last_state_change = (atc_hdlc_state_t)-1;
}

// Helper to inspect the last transmitted frame (assumes it's a valid frame)
// Use mock_output_buffer
void decode_last_tx(atc_hdlc_frame_t *decoded_frame, uint8_t *flat_buf, uint32_t flat_len) {
    bool res = atc_hdlc_frame_unpack(mock_output_buffer, mock_output_len, decoded_frame, flat_buf, flat_len);
    if (!res) {
        test_fail("Frame Decode", "Failed to unpack transmitted frame");
    }
}

// -----------------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------------

void test_init_state(void) {
    printf("TEST: Init State\n");
    setup_context();
    
    if (ctx.current_state != ATC_HDLC_STATE_DISCONNECTED) 
        test_fail("Init State", "Initial state is not DISCONNECTED");
    
    if (atc_hdlc_is_connected(&ctx))
        test_fail("Init State", "Reported connected initially");
        
    test_pass("Init State");
}

void test_connect_sends_sabm(void) {
    printf("TEST: Connect Sends SABM\n");
    setup_context();
    
    // 1. Trigger Connect
    atc_hdlc_error_t res = atc_hdlc_link_setup(&ctx, 0x02);
    if (res != ATC_HDLC_OK) test_fail("Connect Sends SABM", "Connect returned error");
    
    // State Check
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTING)
        test_fail("Connect Sends SABM", "State not CONNECTING");
        
    if (state_change_call_count != 1)
        test_fail("Connect Sends SABM", "State change callback count incorrect");

    if (last_state_change != ATC_HDLC_STATE_CONNECTING)
        test_fail("Connect Sends SABM", "Last state change not CONNECTING");
        
    // 2. Check Output Frame (SABM to Peer)
    atc_hdlc_frame_t frame_out;
    uint8_t flat[32];
    decode_last_tx(&frame_out, flat, sizeof(flat));

    if (frame_out.address != 0x02) test_fail("Connect Sends SABM", "Wrong Dest Address");   // To Peer
    if (frame_out.control != 0x3F) test_fail("Connect Sends SABM", "Not SABM(P=1)");  // SABM (P=1) -> 0x3F
    
    test_pass("Connect Sends SABM");
}

void test_connect_complete_on_ua(void) {
    printf("TEST: Connect Complete on UA\n");
    setup_context();
    atc_hdlc_link_setup(&ctx, 0x02); // Go to CONNECTING
    mock_output_len = 0; // Clear TX buffer
    state_change_call_count = 0; // Clear counters

    // Simulate Receiving UA from Peer
    atc_hdlc_frame_t ua_frame;
    ua_frame.address = 0x02; // Peer's address (Response)
    ua_frame.control = 0x73; // UA with F=1
    ua_frame.information = NULL;
    ua_frame.information_len = 0;
    ua_frame.type = ATC_HDLC_FRAME_U;

    uint8_t packed[32];
    uint32_t packed_len = 0;
    atc_hdlc_frame_pack(&ua_frame, packed, sizeof(packed), &packed_len);

    // Feed bytes
    atc_hdlc_data_in_bytes(&ctx, packed, packed_len);

    // Verify State Change
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTED)
         test_fail("Connect Complete UA", "State not CONNECTED");
         
    if (state_change_call_count != 1)
         test_fail("Connect Complete UA", "Callback count mismatch");
         
    if (!atc_hdlc_is_connected(&ctx))
         test_fail("Connect Complete UA", "Helper returned not connected");

    test_pass("Connect Complete on UA");
}

void test_disconnect_flow(void) {
    printf("TEST: Disconnect Flow\n");
    setup_context();
    // Force Connected
    ctx.current_state = ATC_HDLC_STATE_CONNECTED;
    state_change_call_count = 0;

    // Send Disconnect
    atc_hdlc_error_t res = atc_hdlc_disconnect(&ctx);
    if (res != ATC_HDLC_OK) test_fail("Disconnect Flow", "Disconnect returned error");

    // 1. Check State
    if (ctx.current_state != ATC_HDLC_STATE_DISCONNECTING)
         test_fail("Disconnect Flow", "State not DISCONNECTING");
    
    // 2. Check Output Frame (DISC to Peer)
    atc_hdlc_frame_t frame_out;
    uint8_t flat[32];
    decode_last_tx(&frame_out, flat, sizeof(flat));
    
    // DISC(P=1) = 0x53
    if (frame_out.address != 0x02) test_fail("Disconnect Flow", "Wrong Address");
    if (frame_out.control != 0x53) test_fail("Disconnect Flow", "Not DISC(P=1)");

    // 3. Receive UA
    // Clear buffer
    mock_output_len = 0;
    
    atc_hdlc_frame_t ua_frame;
    ua_frame.address = 0x02;
    ua_frame.control = 0x73; // UA(F=1)
    ua_frame.information = NULL;
    ua_frame.information_len = 0;

    uint8_t packed[32];
    uint32_t packed_len = 0;
    atc_hdlc_frame_pack(&ua_frame, packed, sizeof(packed), &packed_len);
    atc_hdlc_data_in_bytes(&ctx, packed, packed_len);

    // Check State
    if (ctx.current_state != ATC_HDLC_STATE_DISCONNECTED)
         test_fail("Disconnect Flow", "State NOT disconnected after UA");

    test_pass("Disconnect Flow");
}

void test_passive_open(void) {
    printf("TEST: Passive Open (Accept SABM)\n");
    setup_context();
    
    // Simulate Receiving SABM from Peer (Command)
    // Addressed to ME (0x01).
    // SABM(P=1) = 0x3F.
    atc_hdlc_frame_t sabm_frame;
    sabm_frame.address = 0x01;
    sabm_frame.control = 0x3F;
    sabm_frame.information = NULL;
    sabm_frame.information_len = 0;

    uint8_t packed[32];
    uint32_t packed_len = 0;
    atc_hdlc_frame_pack(&sabm_frame, packed, sizeof(packed), &packed_len);
    
    atc_hdlc_data_in_bytes(&ctx, packed, packed_len);

    // 1. Should be CONNECTED
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTED)
         test_fail("Passive Open", "State not CONNECTED after SABM");

    // 2. Should have sent UA (Response from Me)
    atc_hdlc_frame_t frame_out;
    uint8_t flat[32];
    decode_last_tx(&frame_out, flat, sizeof(flat));

    if (frame_out.address != 0x01) test_fail("Passive Open", "UA wrong address");   // My address
    if (frame_out.control != 0x73) test_fail("Passive Open", "Not UA(F=1)");  // UA(F=1)
    
    test_pass("Passive Open (Accept SABM)");
}

void test_frmr_reception(void) {
    printf("TEST: FRMR Reception\n");
    setup_context();
    atc_hdlc_link_setup(&ctx, 0x02); // Connect first
    // Force Connected state for testing
    ctx.current_state = ATC_HDLC_STATE_CONNECTED;
    state_change_call_count = 0; // Clear counters

    // Simulate Receiving FRMR from Peer
    // Payload: 3 bytes (Rejected Ctrl, V(S)/V(R), Flags)
    // Byte 0: Rejected Control = 0x11 (Random)
    // Byte 1: 0 V(S) C/R V(R) -> 0 001 1 010 -> 0001 1010 = 0x1A (V(S)=1, C/R=1, V(R)=2)
    // Byte 2: W X Y Z V 0 0 0 -> 1 0 0 1 0 0 0 0 -> 1001 0000 = 0x90 (W=1, Z=1)
    
    uint8_t frmr_payload[] = {0x11, 0x1A, 0x90};

    atc_hdlc_frame_t frmr_frame;
    frmr_frame.address = 0x02; // From Peer
    frmr_frame.control = hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_FRMR, HDLC_U_MODIFIER_HI_FRMR, 0); // F=0
    frmr_frame.information = frmr_payload;
    frmr_frame.information_len = sizeof(frmr_payload);
    frmr_frame.type = ATC_HDLC_FRAME_U;

    uint8_t packed[32];
    uint32_t packed_len = 0;
    atc_hdlc_frame_pack(&frmr_frame, packed, sizeof(packed), &packed_len);

    // Feed bytes
    atc_hdlc_data_in_bytes(&ctx, packed, packed_len);

    /* FRMR now transitions to FRMR_ERROR (lock-down state), not DISCONNECTED.
     * The peer rejected one of our frames; only link_reset or disconnect is valid. */
    if (ctx.current_state != ATC_HDLC_STATE_FRMR_ERROR)
         test_fail("FRMR Reception", "State not FRMR_ERROR");

    if (state_change_call_count != 1)
         test_fail("FRMR Reception", "State change callback count incorrect");
         
    test_pass("FRMR Reception");
}

void test_mode_rejection(void) {
    printf("TEST: Mode Rejection (SNRM)\n");
    setup_context();
    // Use SNRM (Set Normal Response Mode) - Not Supported
    // M=100 00 -> Hi=4, Lo=0. P=1.
    // Ctrl: 100 1 00 11 -> 1001 0011 -> 0x93
    
    atc_hdlc_frame_t params_frame;
    params_frame.address = 0x01; // To Me
    params_frame.control = hdlc_create_u_ctrl(0, 4, 1); // SNRM, P=1, Hi=4, Lo=0
    params_frame.information = NULL;
    params_frame.information_len = 0;
    params_frame.type = ATC_HDLC_FRAME_U;

    uint8_t packed[32];
    uint32_t packed_len = 0;
    atc_hdlc_frame_pack(&params_frame, packed, sizeof(packed), &packed_len);
    
    // Clear TX capture
    mock_output_len = 0;

    // Feed bytes
    atc_hdlc_data_in_bytes(&ctx, packed, packed_len);
    
    // Inspect captured bytes dump using helper
    print_hexdump("Captured TX", mock_output_buffer, mock_output_len);

    // 1. State should remain DISCONNECTED
    if (ctx.current_state != ATC_HDLC_STATE_DISCONNECTED)
         test_fail("Mode Rejection", "State changed on invalid mode!");

    // 2. Output should be DM
    atc_hdlc_frame_t frame_out;
    uint8_t flat[32];
    decode_last_tx(&frame_out, flat, sizeof(flat));

    // DM: 000 F 00 11 (Hi=0, Lo=3). F should match P (1).
    // 000 1 11 11 -> 0x1F.
    if (frame_out.control != 0x1F) // DM with F=1
         test_fail("Mode Rejection", "Did not send DM");
    
    test_pass("Mode Rejection (SNRM)");
}

void test_extended_mode_rejection(void) {
    printf("TEST: Extended Mode Rejection (SABME, SNRME, SARME)\n");
    setup_context();

    // 1. SABME (0x7F if P=1)
    {
        atc_hdlc_frame_t frame_in;
        frame_in.address = 0x01;
        frame_in.control = hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_SABME, HDLC_U_MODIFIER_HI_SABME, 1);
        frame_in.information = NULL;
        frame_in.information_len = 0;
        frame_in.type = ATC_HDLC_FRAME_U;

        uint8_t packed[32];
        uint32_t packed_len = 0;
        atc_hdlc_frame_pack(&frame_in, packed, sizeof(packed), &packed_len);

        mock_output_len = 0;
        atc_hdlc_data_in_bytes(&ctx, packed, packed_len);

        atc_hdlc_frame_t frame_out;
        uint8_t flat[32];
        decode_last_tx(&frame_out, flat, sizeof(flat));

        if (frame_out.control != 0x1F) // DM with F=1
            test_fail("Ext Rejection SABME", "Did not send DM");
    }

    // 2. SNRME (0xDF if P=1)
    {
        atc_hdlc_frame_t frame_in;
        frame_in.address = 0x01;
        frame_in.control = hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_SNRME, HDLC_U_MODIFIER_HI_SNRME, 1);
        frame_in.information = NULL;
        frame_in.information_len = 0;
        frame_in.type = ATC_HDLC_FRAME_U;

        uint8_t packed[32];
        uint32_t packed_len = 0;
        atc_hdlc_frame_pack(&frame_in, packed, sizeof(packed), &packed_len);

        mock_output_len = 0;
        atc_hdlc_data_in_bytes(&ctx, packed, packed_len);

        atc_hdlc_frame_t frame_out;
        uint8_t flat[32];
        decode_last_tx(&frame_out, flat, sizeof(flat));

        if (frame_out.control != 0x1F) // DM with F=1
            test_fail("Ext Rejection SNRME", "Did not send DM");
    }

    // 3. SARME (0x5F if P=1)
    {
        atc_hdlc_frame_t frame_in;
        frame_in.address = 0x01;
        frame_in.control = hdlc_create_u_ctrl(HDLC_U_MODIFIER_LO_SARME, HDLC_U_MODIFIER_HI_SARME, 1);
        frame_in.information = NULL;
        frame_in.information_len = 0;
        frame_in.type = ATC_HDLC_FRAME_U;

        uint8_t packed[32];
        uint32_t packed_len = 0;
        atc_hdlc_frame_pack(&frame_in, packed, sizeof(packed), &packed_len);

        mock_output_len = 0;
        atc_hdlc_data_in_bytes(&ctx, packed, packed_len);

        atc_hdlc_frame_t frame_out;
        uint8_t flat[32];
        decode_last_tx(&frame_out, flat, sizeof(flat));

        if (frame_out.control != 0x1F) // DM with F=1
            test_fail("Ext Rejection SARME", "Did not send DM");
    }

    test_pass("Extended Mode Rejection");
}

void test_contention_resolution_winner(void) {
    printf("TEST: Contention Resolution (Winner)\n");
    setup_context();
    
    // We are 0x01, peer is 0x02. Wait, the rule is higher address wins. 
    // Let's reconfigure so we are higher.
    /* Reconfigure: we are 0x02 (higher), peer is 0x01 */
    ctx.my_address   = 0x02;
    ctx.peer_address = 0x01;

    // 1. We initiate connection (SABM sent)
    atc_hdlc_link_setup(&ctx, 0x01);
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTING)
         test_fail("Contention Winner", "State not CONNECTING");
         
    mock_output_len = 0; // Clear the SABM we just sent from mock tx buffer
    
    // 2. Peer also initiated connection, so we receive their SABM
    atc_hdlc_frame_t sabm_frame;
    sabm_frame.address = 0x02; // Addressed to us
    sabm_frame.control = 0x3F; // SABM (P=1)
    sabm_frame.information = NULL;
    sabm_frame.information_len = 0;

    uint8_t packed[32];
    uint32_t packed_len = 0;
    atc_hdlc_frame_pack(&sabm_frame, packed, sizeof(packed), &packed_len);
    
    atc_hdlc_data_in_bytes(&ctx, packed, packed_len);
    
    // We are higher address (2 > 1), so we WIN.
    // Winner behaviour: Immediately reply with UA, and transition to CONNECTED.
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTED)
         test_fail("Contention Winner", "State should transition to CONNECTED after winning");
         
    atc_hdlc_frame_t frame_out;
    uint8_t flat[32];
    decode_last_tx(&frame_out, flat, sizeof(flat));

    if (frame_out.address != 0x02) test_fail("Contention Winner", "UA wrong address");
    if (frame_out.control != 0x73) test_fail("Contention Winner", "Did not send UA(F=1)"); // UA(F=1)
    
    /* Winner: T1 should NOT be active (UA was sent, connection established) */
    /* State is CONNECTED after receiving peer SABM and responding with UA */
    
    test_pass("Contention Resolution (Winner)");
}

void test_contention_resolution_loser(void) {
    printf("TEST: Contention Resolution (Loser)\n");
    setup_context();
    
    // We are 0x01, peer is 0x02. Lower address loses.
    ctx.peer_address = 0x02; // Me=0x01, Peer=0x02
    
    // 1. We initiate connection (SABM sent)
    atc_hdlc_link_setup(&ctx, 0x02);
    mock_output_len = 0;
    
    // 2. Peer also initiated connection, so we receive their SABM
    atc_hdlc_frame_t sabm_frame;
    sabm_frame.address = 0x01; // Addressed to us
    sabm_frame.control = 0x3F; // SABM (P=1)
    sabm_frame.information = NULL;
    sabm_frame.information_len = 0;

    uint8_t packed[32];
    uint32_t packed_len = 0;
    atc_hdlc_frame_pack(&sabm_frame, packed, sizeof(packed), &packed_len);
    
    atc_hdlc_data_in_bytes(&ctx, packed, packed_len);
    
    // We are lower address (1 < 2), so we LOSE.
    // Loser behaviour: Do NOT send UA. Set contention timer. State remains CONNECTING.
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTING)
         test_fail("Contention Loser", "State changed from CONNECTING");
         
    if (mock_output_len != 0)
         test_fail("Contention Loser", "Sent a frame instead of backing off");
         
    /* Loser: T1 is running (from link_setup), waiting to retry SABM */
    if (!ctx.t1_active)
         test_fail("Contention Loser", "T1 should be running while waiting to retry");

    // 3. Simulate T1 expiry — this is the backoff; library retransmits SABM
    mock_output_len = 0;
    atc_hdlc_t1_expired(&ctx);
    
    if (mock_output_len == 0) {
        test_fail("Contention Loser", "Timer expired but SABM was not retransmitted");
        return;
    }
    
    atc_hdlc_frame_t retx_frame;
    uint8_t retx_flat[32];
    decode_last_tx(&retx_frame, retx_flat, sizeof(retx_flat));
    
    if (retx_frame.control != 0x3F) {
        test_fail("Contention Loser", "Timer expired but output was not SABM");
    }
         
    test_pass("Contention Resolution (Loser + Timer)");
}

/**
 * @brief Test: atc_hdlc_link_reset() resets state and sends SABM.
 */
void test_link_reset(void) {
    printf("TEST: Link Reset\n");
    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);

    /* Force CONNECTED state */
    ctx.current_state = ATC_HDLC_STATE_CONNECTED;
    ctx.vs = 3; ctx.vr = 2; ctx.va = 1;
    ctx.peer_address = 0x02;

    reset_test_state();
    atc_hdlc_error_t err = atc_hdlc_link_reset(&ctx);
    if (err != ATC_HDLC_OK)
        test_fail("Link Reset", "Return value not OK");

    /* State must be CONNECTING after reset */
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTING)
        test_fail("Link Reset", "State not CONNECTING after reset");

    /* Sequence variables must be zeroed */
    if (ctx.vs != 0 || ctx.vr != 0 || ctx.va != 0)
        test_fail("Link Reset", "Sequence variables not zeroed");

    /* RESET event must fire */
    if (last_event != ATC_HDLC_EVENT_RESET)
        test_fail("Link Reset", "RESET event not fired");

    /* SABM must have been sent */
    if (mock_output_len < 6)
        test_fail("Link Reset", "No SABM frame in output");

    /* T1 must be started */
    if (!ctx.t1_active)
        test_fail("Link Reset", "T1 not active after link_reset");
    if (mock_t1_start_count < 1)
        test_fail("Link Reset", "T1 start callback not invoked");

    test_pass("Link Reset");
}

/**
 * @brief Test: receiving DISC from peer while CONNECTED.
 *        Verifies UA response and PEER_DISCONNECT event.
 */
void test_peer_disconnect(void) {
    printf("TEST: Peer Disconnect (receive DISC)\n");
    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    ctx.current_state = ATC_HDLC_STATE_CONNECTED;
    ctx.peer_address  = 0x02;

    /* Build DISC(P=1) addressed to me (0x01) */
    atc_hdlc_u8 disc_ctrl = hdlc_create_u_ctrl(
        HDLC_U_MODIFIER_LO_DISC, HDLC_U_MODIFIER_HI_DISC, 1);
    atc_hdlc_frame_t disc_frame = {
        .address = 0x01, .control = disc_ctrl,
        .information = NULL, .information_len = 0 };
    atc_hdlc_u8 disc_raw[32]; atc_hdlc_u32 disc_len = 0;
    atc_hdlc_frame_pack(&disc_frame, disc_raw, sizeof(disc_raw), &disc_len);

    reset_test_state();
    atc_hdlc_data_in_bytes(&ctx, disc_raw, disc_len);

    /* Must transition to DISCONNECTED */
    if (ctx.current_state != ATC_HDLC_STATE_DISCONNECTED)
        test_fail("Peer Disconnect", "State not DISCONNECTED");

    /* Must fire PEER_DISCONNECT event */
    if (last_event != ATC_HDLC_EVENT_PEER_DISCONNECT)
        test_fail("Peer Disconnect", "PEER_DISCONNECT event not fired");

    /* UA(F=1) must have been sent back */
    if (mock_output_len < 6)
        test_fail("Peer Disconnect", "No UA response in output");

    /* Verify UA type in response */
    atc_hdlc_u8 resp_buf[64]; atc_hdlc_u32 resp_len = 0;
    atc_hdlc_frame_t resp; atc_hdlc_u8 resp_flat[64];
    memcpy(resp_buf, mock_output_buffer, mock_output_len);
    if (atc_hdlc_frame_unpack(resp_buf, mock_output_len, &resp, resp_flat, sizeof(resp_flat))) {
        atc_hdlc_u_frame_sub_type_t sub = atc_hdlc_get_u_frame_sub_type(resp.control);
        if (sub != ATC_HDLC_U_FRAME_TYPE_UA)
            test_fail("Peer Disconnect", "Response is not UA");
    }

    test_pass("Peer Disconnect (receive DISC)");
}

/**
 * @brief Test: on_event callback fires with correct event codes.
 *        Verifies LINK_SETUP_REQUEST, CONNECT_ACCEPTED, DISCONNECT_REQUEST,
 *        DISCONNECT_COMPLETE sequences.
 */
void test_event_callbacks(void) {
    printf("TEST: Event Callback Sequence\n");
    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    ctx.peer_address = 0x02;

    /* link_setup → LINK_SETUP_REQUEST */
    reset_test_state();
    atc_hdlc_link_setup(&ctx, 0x02);
    if (last_event != ATC_HDLC_EVENT_LINK_SETUP_REQUEST)
        test_fail("Event Callbacks", "LINK_SETUP_REQUEST not fired");
    if (on_event_call_count != 1)
        test_fail("Event Callbacks", "on_event called wrong number of times");

    /* Feed UA → CONNECT_ACCEPTED */
    atc_hdlc_u8 ua_ctrl = hdlc_create_u_ctrl(
        HDLC_U_MODIFIER_LO_UA, HDLC_U_MODIFIER_HI_UA, 1);
    atc_hdlc_frame_t ua = { .address = 0x02, .control = ua_ctrl,
                             .information = NULL, .information_len = 0 };
    atc_hdlc_u8 ua_raw[32]; atc_hdlc_u32 ua_len = 0;
    atc_hdlc_frame_pack(&ua, ua_raw, sizeof(ua_raw), &ua_len);
    reset_test_state();
    atc_hdlc_data_in_bytes(&ctx, ua_raw, ua_len);
    if (last_event != ATC_HDLC_EVENT_CONNECT_ACCEPTED)
        test_fail("Event Callbacks", "CONNECT_ACCEPTED not fired");

    /* disconnect → DISCONNECT_REQUEST */
    reset_test_state();
    atc_hdlc_disconnect(&ctx);
    if (last_event != ATC_HDLC_EVENT_DISCONNECT_REQUEST)
        test_fail("Event Callbacks", "DISCONNECT_REQUEST not fired");

    /* Feed UA → DISCONNECT_COMPLETE */
    reset_test_state();
    atc_hdlc_data_in_bytes(&ctx, ua_raw, ua_len);
    if (last_event != ATC_HDLC_EVENT_DISCONNECT_COMPLETE)
        test_fail("Event Callbacks", "DISCONNECT_COMPLETE not fired");

    test_pass("Event Callback Sequence");
}

/**
 * @brief Test: T1 platform callbacks are invoked at protocol-correct moments.
 *        link_setup → t1_start; UA received → t1_stop; T1 expiry in
 *        CONNECTING → SABM retransmit + t1_start again.
 */
void test_t1_timer_callbacks(void) {
    printf("TEST: T1 Timer Callbacks\n");
    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    ctx.peer_address = 0x02;

    /* link_setup must call t1_start once */
    reset_test_state();
    atc_hdlc_link_setup(&ctx, 0x02);
    if (mock_t1_start_count != 1)
        test_fail("T1 Callbacks", "T1 not started on link_setup");
    if (mock_t1_last_ms != ctx.config->t1_ms)
        test_fail("T1 Callbacks", "T1 started with wrong duration");

    /* T1 expiry in CONNECTING → retry SABM + t1_start again */
    int t1_start_before = mock_t1_start_count;
    atc_hdlc_t1_expired(&ctx);
    if (mock_t1_start_count != t1_start_before + 1)
        test_fail("T1 Callbacks", "T1 not restarted after expiry in CONNECTING");
    if (mock_output_len < 6)
        test_fail("T1 Callbacks", "No SABM retransmitted after T1 expiry");

    /* Feed UA → t1_stop */
    atc_hdlc_u8 ua_ctrl = hdlc_create_u_ctrl(
        HDLC_U_MODIFIER_LO_UA, HDLC_U_MODIFIER_HI_UA, 1);
    atc_hdlc_frame_t ua = { .address = 0x02, .control = ua_ctrl,
                             .information = NULL, .information_len = 0 };
    atc_hdlc_u8 ua_raw[32]; atc_hdlc_u32 ua_len = 0;
    atc_hdlc_frame_pack(&ua, ua_raw, sizeof(ua_raw), &ua_len);
    reset_test_state();
    atc_hdlc_data_in_bytes(&ctx, ua_raw, ua_len);
    if (mock_t1_stop_count < 1)
        test_fail("T1 Callbacks", "T1 not stopped on UA");
    if (ctx.t1_active)
        test_fail("T1 Callbacks", "t1_active still set after UA");

    test_pass("T1 Timer Callbacks");
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
    test_extended_mode_rejection();
    test_contention_resolution_winner();
    test_contention_resolution_loser();
    test_link_reset();
    test_peer_disconnect();
    test_event_callbacks();
    test_t1_timer_callbacks();
    test_frmr_send_invalid_nr();
    test_frmr_error_lockdown();
    test_t3_timer_on_connect();
    test_duplicate_rej_guard();

    printf("\n%sALL TESTS PASSED SUCCESSFULLY!%s\n", COL_GREEN, COL_RESET);
    return 0;
}

/* ================================================================
 *  Phase 4 tests — state machine expansion
 * ================================================================ */

/**
 * @brief Test: FRMR is sent for invalid N(R) (out of V(A)..V(S) window).
 */
void test_frmr_send_invalid_nr(void) {
    printf("TEST: FRMR sent on invalid N(R)\n");

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    ctx.current_state = ATC_HDLC_STATE_CONNECTED;
    ctx.peer_address  = 0x02;
    ctx.va = 0; ctx.vs = 2; /* outstanding frames 0..1 */

    /* Send RR with N(R)=5 — invalid (outside V(A)..V(S) = 0..2) */
    atc_hdlc_u8 rr_ctrl = hdlc_create_s_ctrl(HDLC_S_RR, 5, 0);
    atc_hdlc_frame_t rr = { .address = 0x01, .control = rr_ctrl,
                              .information = NULL, .information_len = 0 };
    atc_hdlc_u8 rr_raw[32]; atc_hdlc_u32 rr_len = 0;
    atc_hdlc_frame_pack(&rr, rr_raw, sizeof(rr_raw), &rr_len);

    reset_test_state();
    atc_hdlc_data_in_bytes(&ctx, rr_raw, rr_len);

    /* State must be FRMR_ERROR */
    if (ctx.current_state != ATC_HDLC_STATE_FRMR_ERROR)
        test_fail("FRMR Invalid NR", "State not FRMR_ERROR after invalid N(R)");

    /* FRMR frame must have been transmitted */
    if (mock_output_len < 6)
        test_fail("FRMR Invalid NR", "No FRMR in output");

    /* Verify FRMR Z bit set — decode the frame */
    atc_hdlc_frame_t frmr_out; atc_hdlc_u8 flat[32];
    if (atc_hdlc_frame_unpack(mock_output_buffer, mock_output_len,
                               &frmr_out, flat, sizeof(flat))) {
        if (atc_hdlc_get_u_frame_sub_type(frmr_out.control) != ATC_HDLC_U_FRAME_TYPE_FRMR)
            test_fail("FRMR Invalid NR", "Output is not a FRMR frame");
        if (frmr_out.information_len >= 3) {
            atc_hdlc_u8 reason = frmr_out.information[2];
            if (!(reason & HDLC_FRMR_Z_BIT))
                test_fail("FRMR Invalid NR", "FRMR Z bit not set");
        }
    }

    /* PROTOCOL_ERROR event must have fired */
    if (last_event != ATC_HDLC_EVENT_PROTOCOL_ERROR)
        test_fail("FRMR Invalid NR", "PROTOCOL_ERROR event not fired");

    test_pass("FRMR sent on invalid N(R)");
}

/**
 * @brief Test: FRMR_ERROR lock-down — all operations except reset/disconnect
 *        return ERR_INVALID_STATE.
 */
void test_frmr_error_lockdown(void) {
    printf("TEST: FRMR_ERROR lock-down\n");

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    ctx.current_state = ATC_HDLC_STATE_FRMR_ERROR;
    ctx.peer_address  = 0x02;

    /* transmit_i must be rejected */
    atc_hdlc_u8 payload[] = {0xAA};
    if (atc_hdlc_transmit_i(&ctx, payload, 1) != ATC_HDLC_ERR_INVALID_STATE)
        test_fail("FRMR Lock-down", "transmit_i should return INVALID_STATE in FRMR_ERROR");

    /* link_reset must be allowed */
    if (atc_hdlc_link_reset(&ctx) != ATC_HDLC_OK)
        test_fail("FRMR Lock-down", "link_reset should succeed in FRMR_ERROR");
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTING)
        test_fail("FRMR Lock-down", "link_reset should transition to CONNECTING");

    /* disconnect from FRMR_ERROR must also be allowed */
    ctx.current_state = ATC_HDLC_STATE_FRMR_ERROR;
    if (atc_hdlc_disconnect(&ctx) != ATC_HDLC_OK)
        test_fail("FRMR Lock-down", "disconnect should succeed in FRMR_ERROR");

    /* SABM from peer while in FRMR_ERROR: peer re-establishes → CONNECTED */
    ctx.current_state = ATC_HDLC_STATE_FRMR_ERROR;
    atc_hdlc_u8 sabm_ctrl = hdlc_create_u_ctrl(
        HDLC_U_MODIFIER_LO_SABM, HDLC_U_MODIFIER_HI_SABM, 1);
    atc_hdlc_frame_t sabm = { .address = 0x01, .control = sabm_ctrl,
                                .information = NULL, .information_len = 0 };
    atc_hdlc_u8 sabm_raw[32]; atc_hdlc_u32 sabm_len = 0;
    atc_hdlc_frame_pack(&sabm, sabm_raw, sizeof(sabm_raw), &sabm_len);
    reset_test_state();
    atc_hdlc_data_in_bytes(&ctx, sabm_raw, sabm_len);
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTED)
        test_fail("FRMR Lock-down", "SABM from peer should re-establish CONNECTED from FRMR_ERROR");

    test_pass("FRMR_ERROR lock-down");
}

/**
 * @brief Test: T3 keep-alive timer starts on CONNECTED entry and
 *        restarts on every received frame.
 */
void test_t3_timer_on_connect(void) {
    printf("TEST: T3 timer on CONNECTED entry\n");

    /* Use a config with t3_ms > 0 */
    static atc_hdlc_config_t cfg_t3;
    static atc_hdlc_platform_t plat_t3;
    static atc_hdlc_rx_buffer_t rx_t3;
    static atc_hdlc_u8 rx_buf_t3[1028];

    cfg_t3 = (atc_hdlc_config_t){
        .mode = ATC_HDLC_MODE_ABM, .address = 0x01, .window_size = 1,
        .max_frame_size = 1024, .max_retries = 3,
        .t1_ms = 1000, .t2_ms = 10, .t3_ms = 5000, .use_extended = false
    };
    plat_t3 = (atc_hdlc_platform_t){
        .on_send = mock_send_cb, .on_data = mock_on_data_cb,
        .on_event = mock_on_event_cb, .user_ctx = NULL,
        .t1_start = mock_t1_start_cb, .t1_stop = mock_t1_stop_cb,
        .t2_start = mock_t2_start_cb, .t2_stop = mock_t2_stop_cb,
        .t3_start = mock_t3_start_cb, .t3_stop = mock_t3_stop_cb,
    };
    rx_t3.buffer = rx_buf_t3; rx_t3.capacity = sizeof(rx_buf_t3);

    atc_hdlc_context_t ctx;
    reset_test_state();
    atc_hdlc_init(&ctx, &cfg_t3, &plat_t3, NULL, &rx_t3);
    ctx.peer_address = 0x02;

    /* link_setup → CONNECTING (T3 should NOT start yet) */
    atc_hdlc_link_setup(&ctx, 0x02);
    if (ctx.t3_active)
        test_fail("T3 Timer", "T3 should not be active in CONNECTING");

    /* Feed UA → CONNECTED: T3 should start */
    atc_hdlc_u8 ua_ctrl = hdlc_create_u_ctrl(
        HDLC_U_MODIFIER_LO_UA, HDLC_U_MODIFIER_HI_UA, 1);
    atc_hdlc_frame_t ua = { .address = 0x02, .control = ua_ctrl,
                              .information = NULL, .information_len = 0 };
    atc_hdlc_u8 ua_raw[32]; atc_hdlc_u32 ua_len = 0;
    atc_hdlc_frame_pack(&ua, ua_raw, sizeof(ua_raw), &ua_len);
    reset_test_state();
    atc_hdlc_data_in_bytes(&ctx, ua_raw, ua_len);
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTED)
        test_fail("T3 Timer", "Not CONNECTED after UA");
    if (!ctx.t3_active)
        test_fail("T3 Timer", "T3 not started on entering CONNECTED");
    if (mock_t3_start_count < 1)
        test_fail("T3 Timer", "T3 start callback not invoked");

    /* T3 expiry: RR(P=1) should be sent */
    reset_test_state();
    atc_hdlc_t3_expired(&ctx);
    if (mock_output_len < 6)
        test_fail("T3 Timer", "No keep-alive RR after T3 expiry");

    test_pass("T3 timer on CONNECTED entry");
}

/**
 * @brief Test: Duplicate REJ guard — second OOS I-frame does not send another REJ.
 */
void test_duplicate_rej_guard(void) {
    printf("TEST: Duplicate REJ guard\n");

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    ctx.current_state = ATC_HDLC_STATE_CONNECTED;
    ctx.peer_address  = 0x02;
    ctx.vr = 0;

    /* Send I-frame N(S)=1 (out of sequence, expect N(S)=0) → REJ sent */
    atc_hdlc_u8 i_ctrl = hdlc_create_i_ctrl(1, 0, 0);
    atc_hdlc_u8 payload[] = {0xBB};
    atc_hdlc_frame_t iframe = { .address = 0x01, .control = i_ctrl,
                                  .information = payload, .information_len = 1 };
    atc_hdlc_u8 i_raw[64]; atc_hdlc_u32 i_len = 0;
    atc_hdlc_frame_pack(&iframe, i_raw, sizeof(i_raw), &i_len);

    reset_test_state();
    atc_hdlc_data_in_bytes(&ctx, i_raw, i_len);
    if (!ctx.rej_exception)
        test_fail("Duplicate REJ", "rej_exception not set after OOS frame");
    int first_output_len = mock_output_len;
    if (first_output_len < 6)
        test_fail("Duplicate REJ", "No REJ sent on first OOS");

    /* Send second OOS I-frame N(S)=2 — REJ must NOT be sent again */
    atc_hdlc_u8 i_ctrl2 = hdlc_create_i_ctrl(2, 0, 0);
    atc_hdlc_frame_t iframe2 = { .address = 0x01, .control = i_ctrl2,
                                   .information = payload, .information_len = 1 };
    atc_hdlc_u8 i_raw2[64]; atc_hdlc_u32 i_len2 = 0;
    atc_hdlc_frame_pack(&iframe2, i_raw2, sizeof(i_raw2), &i_len2);
    reset_test_state();
    atc_hdlc_data_in_bytes(&ctx, i_raw2, i_len2);
    /* No REJ should be in output (rej_exception guards duplicate REJ) */
    if (mock_output_len >= 6) {
        /* Check that the output is NOT a REJ */
        atc_hdlc_frame_t resp; atc_hdlc_u8 flat[32];
        if (atc_hdlc_frame_unpack(mock_output_buffer, mock_output_len,
                                   &resp, flat, sizeof(flat))) {
            if (atc_hdlc_get_s_frame_sub_type(resp.control) == ATC_HDLC_S_FRAME_TYPE_REJ)
                test_fail("Duplicate REJ", "Duplicate REJ sent — rej_exception guard failed");
        }
    }

    test_pass("Duplicate REJ guard");
}

