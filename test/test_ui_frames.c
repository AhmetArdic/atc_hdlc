#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../inc/hdlc.h"

// --- Mock Hardware ---
static uint8_t tx_buffer[256];
static uint32_t tx_len = 0;

static void mock_output_byte(hdlc_u8 byte, hdlc_bool flush, void *user_data) {
    (void)flush;
    (void)user_data;
    if (tx_len < sizeof(tx_buffer)) {
        tx_buffer[tx_len++] = byte;
    }
}

// --- Frame Capture ---
static hdlc_frame_t last_frame;
static uint8_t rx_info_buf[128];
static bool frame_received = false;

static void on_frame(const hdlc_frame_t *frame, void *user_data) {
    (void)user_data;
    last_frame = *frame;
    
    // Deep copy info because the library buffer is volatile
    if (frame->information_len > 0 && frame->information_len <= sizeof(rx_info_buf)) {
        memcpy(rx_info_buf, frame->information, frame->information_len);
        last_frame.information = rx_info_buf;
    } else {
        last_frame.information = NULL;
    }
    
    frame_received = true;
}

// --- Tests ---

void test_ui_frame_transmission(void) {
    printf("TEST: UI Frame Transmission... ");
    
    hdlc_context_t ctx;
    uint8_t buffer[256];
    hdlc_init(&ctx, buffer, sizeof(buffer), mock_output_byte, on_frame, NULL, NULL);
    hdlc_configure_addresses(&ctx, 0x01, 0x02); // My=0x01, Peer=0x02

    tx_len = 0;
    const char *payload = "HELLO";
    bool res = hdlc_send_ui(&ctx, (const uint8_t*)payload, 5);
    
    assert(res == true);
    assert(tx_len > 0);
    // Min Size: Flag(1) + Addr(1) + Ctrl(1) + Data(5) + FCS(2) + Flag(1) = 11
    assert(tx_len >= 11); 
    
    // Check Control Field for UI (0x03 or 0x13)
    // Addr=0x02 (Peer)
    // 7E 02 03 ...
    // Note: Depends on stuffing. "HELLO" is safe. 0x02 is safe. 0x03 is safe.
    assert(tx_buffer[0] == 0x7E); // Flag
    assert(tx_buffer[1] == 0x02); // Addr
    assert((tx_buffer[2] & 0xEF) == 0x03); // Ctrl: ~P bit. UI=0x03 (P=0)
    
    printf("PASS\n");
}

void test_ui_frame_reception(void) {
    printf("TEST: UI Frame Reception... ");
    
    hdlc_context_t ctx;
    uint8_t buffer[256];
    hdlc_init(&ctx, buffer, sizeof(buffer), mock_output_byte, on_frame, NULL, NULL);
    hdlc_configure_addresses(&ctx, 0x01, 0x02); // My=0x01, Peer=0x02

    // Construct a valid UI frame addressed to ME (0x01)
    // Addr=0x01, Ctrl=0x03 (UI, P=0), Data="WORLD"
    // FCS calculation required.
    // Let's rely on loopback from previous test logic or manually build it.
    
    // We can recycle the TX buffer from previous test if we tweak address
    tx_len = 0;
    // Send to MYSELF (0x01)
    hdlc_output_packet_start(&ctx, 0x01, 0x03); 
    hdlc_output_packet_information_bytes(&ctx, (uint8_t*)"WORLD", 5);
    hdlc_output_packet_end(&ctx);
    
    // Now Feed it back
    frame_received = false;
    hdlc_input_bytes(&ctx, tx_buffer, tx_len);
    
    assert(frame_received == true);
    assert(last_frame.type == HDLC_FRAME_U);
    assert(last_frame.address == 0x01);
    assert((last_frame.control.value & 0xEF) == 0x03);
    assert(last_frame.information_len == 5);
    assert(memcmp(last_frame.information, "WORLD", 5) == 0);

    printf("PASS\n");
}

int main(void) {
    test_ui_frame_transmission();
    test_ui_frame_reception();
    return 0;
}
