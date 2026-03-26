#include "../inc/hdlc.h"
#include "../src/hdlc_frame.h"
#include "test_common.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// -----------------------------------------------------------------------------
// Mocks & Helpers
// -----------------------------------------------------------------------------
static atc_hdlc_context_t ctx;

static atc_hdlc_state_t last_state_change = (atc_hdlc_state_t)-1;
static int state_change_call_count = 0;

/* on_event callback — matches atc_hdlc_on_event_fn signature.
 * We derive the "new state" from the event type for test assertions. */
void on_state_change(atc_hdlc_event_t event, void* user_data) {
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
    static atc_hdlc_u8 s_retx_slots[1 * 1024];
    static atc_hdlc_u32 s_retx_lens[1];

    static const atc_hdlc_config_t cfg = {
        .mode = ATC_HDLC_MODE_ABM,
        .address = 0x01,
        .max_info_size = 1024,
        .max_retries = 3,
        .t1_ms = ATC_HDLC_DEFAULT_T1_TIMEOUT,
        .t2_ms = ATC_HDLC_DEFAULT_T2_TIMEOUT,
    };
    static const atc_hdlc_platform_t plat = {
        .on_send = mock_send_cb,
        .on_data = mock_on_data_cb,
        .on_event = on_state_change,
        .user_ctx = NULL,
    };
    static atc_hdlc_tx_window_t tw = {
        .slots = s_retx_slots,
        .slot_lens = s_retx_lens,
        .slot_capacity = 1024,
        .slot_count = 1,
    };
    static atc_hdlc_rx_buffer_t rx = {
        .buffer = mock_rx_buffer,
        .capacity = sizeof(mock_rx_buffer),
    };

    atc_hdlc_params_t p = {.config = &cfg, .platform = &plat, .tx_window = &tw, .rx_buf = &rx};
    atc_hdlc_init(&ctx, p);
    ctx.peer_address = 0x02; /* peer address for tests */

    reset_test_state();
    state_change_call_count = 0;
    last_state_change = (atc_hdlc_state_t)-1;
}

// Helper to inspect the last transmitted frame (assumes it's a valid frame)
// Use mock_output_buffer
test_frame_t decode_last_tx(atc_hdlc_u8* flat_buf, uint32_t flat_len) {
    return test_unpack_frame(mock_output_buffer, mock_output_len, flat_buf, (int)flat_len);
}

// -----------------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------------

void test_init_state(void) {
    printf("TEST: Init State\n");
    setup_context();

    if (ctx.current_state != ATC_HDLC_STATE_DISCONNECTED)
        test_fail("Init State", "Initial state is not DISCONNECTED");

    if (atc_hdlc_get_state(&ctx) == ATC_HDLC_STATE_CONNECTED)
        test_fail("Init State", "Reported connected initially");

    test_pass("Init State");
}

void test_connect_sends_sabm(void) {
    printf("TEST: Connect Sends SABM\n");
    setup_context();

    // 1. Trigger Connect
    atc_hdlc_error_t res = atc_hdlc_link_setup(&ctx, 0x02);
    if (res != ATC_HDLC_OK)
        test_fail("Connect Sends SABM", "Connect returned error");

    // State Check
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTING)
        test_fail("Connect Sends SABM", "State not CONNECTING");

    if (state_change_call_count != 1)
        test_fail("Connect Sends SABM", "State change callback count incorrect");

    if (last_state_change != ATC_HDLC_STATE_CONNECTING)
        test_fail("Connect Sends SABM", "Last state change not CONNECTING");

    // 2. Check Output Frame (SABM to Peer)
    uint8_t flat[32];
    test_frame_t frame_out = decode_last_tx(flat, sizeof(flat));

    if (frame_out.address != 0x02)
        test_fail("Connect Sends SABM", "Wrong Dest Address"); // To Peer
    if (frame_out.control != 0x3F)
        test_fail("Connect Sends SABM", "Not SABM(P=1)"); // SABM (P=1) -> 0x3F

    test_pass("Connect Sends SABM");
}

void test_connect_complete_on_ua(void) {
    printf("TEST: Connect Complete on UA\n");
    setup_context();
    atc_hdlc_link_setup(&ctx, 0x02); // Go to CONNECTING
    mock_output_len = 0;             // Clear TX buffer
    state_change_call_count = 0;     // Clear counters

    // Simulate Receiving UA from Peer
    uint8_t packed[32];
    int packed_len = test_pack_frame(0x02, 0x73, NULL, 0, packed, sizeof(packed));

    // Feed bytes
    atc_hdlc_data_in(&ctx, packed, packed_len);

    // Verify State Change
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTED)
        test_fail("Connect Complete UA", "State not CONNECTED");

    if (state_change_call_count != 1)
        test_fail("Connect Complete UA", "Callback count mismatch");

    if (atc_hdlc_get_state(&ctx) != ATC_HDLC_STATE_CONNECTED)
        test_fail("Connect Complete UA", "State not CONNECTED after UA");

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
    if (res != ATC_HDLC_OK)
        test_fail("Disconnect Flow", "Disconnect returned error");

    // 1. Check State
    if (ctx.current_state != ATC_HDLC_STATE_DISCONNECTING)
        test_fail("Disconnect Flow", "State not DISCONNECTING");

    // 2. Check Output Frame (DISC to Peer)
    uint8_t flat[32];
    test_frame_t frame_out = decode_last_tx(flat, sizeof(flat));

    // DISC(P=1) = 0x53
    if (frame_out.address != 0x02)
        test_fail("Disconnect Flow", "Wrong Address");
    if (frame_out.control != 0x53)
        test_fail("Disconnect Flow", "Not DISC(P=1)");

    // 3. Receive UA
    // Clear buffer
    mock_output_len = 0;

    uint8_t packed[32];
    int packed_len = test_pack_frame(0x02, 0x73, NULL, 0, packed, sizeof(packed));
    atc_hdlc_data_in(&ctx, packed, packed_len);

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
    uint8_t packed[32];
    int packed_len = test_pack_frame(0x01, 0x3F, NULL, 0, packed, sizeof(packed));

    atc_hdlc_data_in(&ctx, packed, packed_len);

    // 1. Should be CONNECTED
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTED)
        test_fail("Passive Open", "State not CONNECTED after SABM");

    // 2. Should have sent UA (Response from Me)
    uint8_t flat[32];
    test_frame_t frame_out = decode_last_tx(flat, sizeof(flat));

    if (frame_out.address != 0x01)
        test_fail("Passive Open", "UA wrong address"); // My address
    if (frame_out.control != 0x73)
        test_fail("Passive Open", "Not UA(F=1)"); // UA(F=1)

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

    uint8_t packed[32];
    int packed_len = test_pack_frame(0x02, U_CTRL(U_FRMR, 0), frmr_payload, sizeof(frmr_payload),
                                     packed, sizeof(packed));

    // Feed bytes
    atc_hdlc_data_in(&ctx, packed, packed_len);

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

    uint8_t packed[32];
    int packed_len = test_pack_frame(0x01, U_CTRL(U_SNRM, 1), NULL, 0, packed, sizeof(packed));

    // Clear TX capture
    mock_output_len = 0;

    // Feed bytes
    atc_hdlc_data_in(&ctx, packed, packed_len);

    // Inspect captured bytes dump using helper
    print_hexdump("Captured TX", mock_output_buffer, mock_output_len);

    // 1. State should remain DISCONNECTED
    if (ctx.current_state != ATC_HDLC_STATE_DISCONNECTED)
        test_fail("Mode Rejection", "State changed on invalid mode!");

    // 2. Output should be DM
    uint8_t flat[32];
    test_frame_t frame_out = decode_last_tx(flat, sizeof(flat));

    // DM: 000 F 00 11 (Hi=0, Lo=3). F should match P (1).
    // 000 1 11 11 -> 0x1F.
    if (frame_out.control != 0x1F) // DM with F=1
        test_fail("Mode Rejection", "Did not send DM");

    test_pass("Mode Rejection (SNRM)");
}

void test_extended_mode_rejection(void) {
    printf("TEST: Extended Mode Rejection (SABME, SNRME, SARME)\n");
    setup_context();

    static const struct {
        atc_hdlc_u8 ctrl;
        const char* name;
    } cases[] = {
        {U_CTRL(U_SABME, 1), "SABME"},
        {U_CTRL(U_SNRME, 1), "SNRME"},
        {U_CTRL(U_SARME, 1), "SARME"},
    };

    for (int i = 0; i < 3; i++) {
        atc_hdlc_u8 packed[32];
        int packed_len = test_pack_frame(0x01, cases[i].ctrl, NULL, 0, packed, sizeof(packed));

        mock_output_len = 0;
        atc_hdlc_data_in(&ctx, packed, packed_len);

        atc_hdlc_u8 flat[32];
        test_frame_t frame_out = decode_last_tx(flat, sizeof(flat));

        if (frame_out.control != 0x1F) { /* DM with F=1 */
            char msg[64];
            sprintf(msg, "%s did not send DM(F=1)", cases[i].name);
            test_fail("Ext Mode Rejection", msg);
        }
    }

    test_pass("Extended Mode Rejection");
}

void test_contention_resolution_winner(void) {
    printf("TEST: Contention Resolution (Winner)\n");
    setup_context();

    /* LAPB-style collision: when both sides send SABM simultaneously, each
     * responds with UA and stays in CONNECTING. Both then receive the other's
     * UA(F=1) and transition to CONNECTED with CONNECT_ACCEPTED. No address
     * comparison needed — the protocol resolves it symmetrically. */

    ctx.my_address = 0x02;
    ctx.peer_address = 0x01;

    // 1. We initiate connection (SABM sent)
    atc_hdlc_link_setup(&ctx, 0x01);
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTING)
        test_fail("Contention Winner", "State not CONNECTING");

    mock_output_len = 0;

    // 2. Peer also initiated connection, so we receive their SABM
    uint8_t packed[32];
    int packed_len = test_pack_frame(0x02, 0x3F, NULL, 0, packed, sizeof(packed));

    atc_hdlc_data_in(&ctx, packed, packed_len);

    // LAPB behaviour: send UA, remain in CONNECTING (wait for peer's UA)
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTING)
        test_fail("Contention Winner", "State should remain CONNECTING after collision");

    uint8_t flat[32];
    test_frame_t frame_out = decode_last_tx(flat, sizeof(flat));

    if (frame_out.address != 0x02)
        test_fail("Contention Winner", "UA wrong address");
    if (frame_out.control != 0x73)
        test_fail("Contention Winner", "Did not send UA(F=1)");

    // 3. Now peer's UA(F=1) arrives in response to our SABM → CONNECTED
    mock_output_len = 0;
    int ua_len = test_pack_frame(0x02, 0x73, NULL, 0, packed, sizeof(packed));
    atc_hdlc_data_in(&ctx, packed, ua_len);

    if (ctx.current_state != ATC_HDLC_STATE_CONNECTED)
        test_fail("Contention Winner", "State should be CONNECTED after receiving UA(F=1)");

    test_pass("Contention Resolution (Winner)");
}

void test_contention_resolution_loser(void) {
    printf("TEST: Contention Resolution (Loser)\n");
    setup_context();

    /* Same LAPB symmetric collision: address does not matter. Both sides
     * send UA and stay in CONNECTING. The "loser" side is identical to
     * the "winner" side — the protocol treats them the same. */
    ctx.peer_address = 0x02;

    // 1. We initiate connection
    atc_hdlc_link_setup(&ctx, 0x02);
    mock_output_len = 0;

    // 2. Peer also initiated — we receive their SABM
    uint8_t packed[32];
    int packed_len = test_pack_frame(0x01, 0x3F, NULL, 0, packed, sizeof(packed));

    atc_hdlc_data_in(&ctx, packed, packed_len);

    // LAPB behaviour: send UA, remain in CONNECTING
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTING)
        test_fail("Contention Loser", "State changed from CONNECTING");

    // UA must have been sent
    uint8_t flat[32];
    test_frame_t frame_out = decode_last_tx(flat, sizeof(flat));
    if (frame_out.control != 0x73)
        test_fail("Contention Loser", "Did not send UA(F=1) in response to collision SABM");

    /* T1 is still running — we are waiting for peer's UA */
    if (!(ctx.flags & HDLC_F_T1_ACTIVE))
        test_fail("Contention Loser", "T1 should be running while waiting for UA");

    // 3. T1 expires before UA arrives — retransmit SABM
    mock_output_len = 0;
    atc_hdlc_t1_expired(&ctx);

    if (mock_output_len == 0) {
        test_fail("Contention Loser", "Timer expired but SABM was not retransmitted");
        return;
    }

    uint8_t retx_flat[32];
    test_frame_t retx_frame = decode_last_tx(retx_flat, sizeof(retx_flat));

    if (retx_frame.control != 0x3F)
        test_fail("Contention Loser", "Timer expired but output was not SABM");

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
    ctx.vs = 3;
    ctx.vr = 2;
    ctx.va = 1;
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
    if (!(ctx.flags & HDLC_F_T1_ACTIVE))
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
    ctx.peer_address = 0x02;

    /* Build DISC(P=1) addressed to me (0x01) */
    atc_hdlc_u8 disc_raw[32];
    int disc_len = test_pack_frame(0x01, U_CTRL(U_DISC, 1), NULL, 0, disc_raw, sizeof(disc_raw));

    reset_test_state();
    atc_hdlc_data_in(&ctx, disc_raw, disc_len);

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
    atc_hdlc_u8 resp_flat[64];
    test_frame_t resp =
        test_unpack_frame(mock_output_buffer, mock_output_len, resp_flat, sizeof(resp_flat));
    if (resp.valid) {
        if ((resp.control & ~PF_BIT) != U_UA)
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
    atc_hdlc_u8 ua_raw[32];
    int ua_len = test_pack_frame(0x02, U_CTRL(U_UA, 1), NULL, 0, ua_raw, sizeof(ua_raw));
    reset_test_state();
    atc_hdlc_data_in(&ctx, ua_raw, ua_len);
    if (last_event != ATC_HDLC_EVENT_CONNECT_ACCEPTED)
        test_fail("Event Callbacks", "CONNECT_ACCEPTED not fired");

    /* disconnect → DISCONNECT_REQUEST */
    reset_test_state();
    atc_hdlc_disconnect(&ctx);
    if (last_event != ATC_HDLC_EVENT_DISCONNECT_REQUEST)
        test_fail("Event Callbacks", "DISCONNECT_REQUEST not fired");

    /* Feed UA → DISCONNECT_COMPLETE */
    reset_test_state();
    atc_hdlc_data_in(&ctx, ua_raw, (atc_hdlc_u32)ua_len);
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
    atc_hdlc_u8 ua_raw[32];
    int ua_len = test_pack_frame(0x02, U_CTRL(U_UA, 1), NULL, 0, ua_raw, sizeof(ua_raw));
    reset_test_state();
    atc_hdlc_data_in(&ctx, ua_raw, (atc_hdlc_u32)ua_len);
    if (mock_t1_stop_count < 1)
        test_fail("T1 Callbacks", "T1 not stopped on UA");
    if (ctx.flags & HDLC_F_T1_ACTIVE)
        test_fail("T1 Callbacks", "t1_active still set after UA");

    test_pass("T1 Timer Callbacks");
}

/**
 * @brief Test: FRMR is sent for invalid N(R) (out of V(A)..V(S) window).
 */
void test_frmr_send_invalid_nr(void) {
    printf("TEST: FRMR sent on invalid N(R)\n");

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    ctx.current_state = ATC_HDLC_STATE_CONNECTED;
    ctx.peer_address = 0x02;
    ctx.va = 0;
    ctx.vs = 2; /* outstanding frames 0..1 */

    /* Send RR with N(R)=5 — invalid (outside V(A)..V(S) = 0..2) */
    atc_hdlc_u8 rr_raw[32];
    int rr_len = test_pack_frame(0x01, S_CTRL(S_RR, 5, 0), NULL, 0, rr_raw, sizeof(rr_raw));

    reset_test_state();
    atc_hdlc_data_in(&ctx, rr_raw, (atc_hdlc_u32)rr_len);

    /* State must be FRMR_ERROR */
    if (ctx.current_state != ATC_HDLC_STATE_FRMR_ERROR)
        test_fail("FRMR Invalid NR", "State not FRMR_ERROR after invalid N(R)");

    /* FRMR frame must have been transmitted */
    if (mock_output_len < 6)
        test_fail("FRMR Invalid NR", "No FRMR in output");

    /* Verify FRMR Z bit set — decode the frame */
    atc_hdlc_u8 flat[32];
    test_frame_t frmr_out =
        test_unpack_frame(mock_output_buffer, mock_output_len, flat, sizeof(flat));
    if (frmr_out.valid) {
        if ((frmr_out.control & ~PF_BIT) != U_FRMR)
            test_fail("FRMR Invalid NR", "Output is not a FRMR frame");
        if (frmr_out.info_len >= 3) {
            atc_hdlc_u8 reason = frmr_out.info[2];
            if (!(reason & FRMR_Z))
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
    ctx.peer_address = 0x02;

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
    atc_hdlc_u8 sabm_raw[32];
    int sabm_len = test_pack_frame(0x01, U_CTRL(U_SABM, 1), NULL, 0, sabm_raw, sizeof(sabm_raw));
    reset_test_state();
    atc_hdlc_data_in(&ctx, sabm_raw, (atc_hdlc_u32)sabm_len);
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTED)
        test_fail("FRMR Lock-down", "SABM from peer should re-establish CONNECTED from FRMR_ERROR");

    test_pass("FRMR_ERROR lock-down");
}

/**
 * @brief Test: Duplicate REJ guard — second OOS I-frame does not send another REJ.
 */
/**
 * @brief Test: DM received in CONNECTING state.
 *        Peer refuses our SABM with DM(F=1) — must fire PEER_REJECT and
 *        return to DISCONNECTED.
 */
void test_dm_on_connecting(void) {
    printf("TEST: DM received in CONNECTING state\n");
    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    ctx.peer_address = 0x02;

    atc_hdlc_link_setup(&ctx, 0x02);
    if (ctx.current_state != ATC_HDLC_STATE_CONNECTING)
        test_fail("DM on Connecting", "Setup failed — not CONNECTING");

    /* Peer sends DM(F=1) to our address */
    atc_hdlc_u8 dm_raw[32];
    int dm_len = test_pack_frame(0x01, U_CTRL(U_DM, 1), NULL, 0, dm_raw, sizeof(dm_raw));

    reset_test_state();
    atc_hdlc_data_in(&ctx, dm_raw, (atc_hdlc_u32)dm_len);

    if (ctx.current_state != ATC_HDLC_STATE_DISCONNECTED)
        test_fail("DM on Connecting", "State not DISCONNECTED after DM");
    if (last_event != ATC_HDLC_EVENT_PEER_REJECT)
        test_fail("DM on Connecting", "PEER_REJECT event not fired");
    if (ctx.flags & HDLC_F_T1_ACTIVE)
        test_fail("DM on Connecting", "T1 should be stopped after DM");

    test_pass("DM received in CONNECTING state");
}

void test_duplicate_rej_guard(void) {
    printf("TEST: Duplicate REJ guard\n");

    atc_hdlc_context_t ctx;
    setup_test_context(&ctx);
    ctx.current_state = ATC_HDLC_STATE_CONNECTED;
    ctx.peer_address = 0x02;
    ctx.vr = 0;

    /* Send I-frame N(S)=1 (out of sequence, expect N(S)=0) → REJ sent */
    atc_hdlc_u8 payload[] = {0xBB};
    atc_hdlc_u8 i_raw[64];
    int i_len = test_pack_frame(0x01, I_CTRL(1, 0, 0), payload, 1, i_raw, sizeof(i_raw));

    reset_test_state();
    atc_hdlc_data_in(&ctx, i_raw, (atc_hdlc_u32)i_len);
    if (!(ctx.flags & HDLC_F_REJ_EXCEPTION))
        test_fail("Duplicate REJ", "rej_exception not set after OOS frame");
    int first_output_len = mock_output_len;
    if (first_output_len < 6)
        test_fail("Duplicate REJ", "No REJ sent on first OOS");

    /* Send second OOS I-frame N(S)=2 — REJ must NOT be sent again */
    atc_hdlc_u8 i_raw2[64];
    int i_len2 = test_pack_frame(0x01, I_CTRL(2, 0, 0), payload, 1, i_raw2, sizeof(i_raw2));
    reset_test_state();
    atc_hdlc_data_in(&ctx, i_raw2, (atc_hdlc_u32)i_len2);
    /* No REJ should be in output (rej_exception guards duplicate REJ) */
    if (mock_output_len >= 6) {
        /* Check that the output is NOT a REJ */
        atc_hdlc_u8 flat[32];
        test_frame_t resp =
            test_unpack_frame(mock_output_buffer, mock_output_len, flat, sizeof(flat));
        if (resp.valid) {
            if (CTRL_S(resp.control) == S_REJ)
                test_fail("Duplicate REJ", "Duplicate REJ sent — rej_exception guard failed");
        }
    }

    test_pass("Duplicate REJ guard");
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
    test_dm_on_connecting();
    test_duplicate_rej_guard();

    printf("\n%sALL TESTS PASSED SUCCESSFULLY!%s\n", COL_GREEN, COL_RESET);
    return 0;
}
