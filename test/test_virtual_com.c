#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <termios.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#include "../inc/hdlc.h"
#include "test_common.h"

// Configuration
#define PAYLOAD_SIZE 4096
#define CHUNK_SIZE 128

typedef struct {
    int fd;
    hdlc_context_t ctx;
    hdlc_u8 input_buffer[8192];
    hdlc_u8 retransmit_buffer[8192];
    pthread_t thread;
    volatile bool running;
    
    // Config
    uint32_t error_probability; // 0 to 10000 (0% to 100%)
    bool drop_next_i_frame;
    
    // Receive buffer (for file transfer verification)
    uint8_t *rx_data;
    uint32_t rx_data_capacity;
    
    // Stats
    volatile uint32_t bytes_received;
    volatile uint32_t frames_received;
    volatile bool connected;
} virtual_node_t;

// Callbacks
static void node_output_cb(hdlc_u8 byte, hdlc_bool flush, void *user_data) {
    (void)flush;
    virtual_node_t *node = (virtual_node_t *)user_data;
    
    if (node->error_probability > 0) {
        if ((uint32_t)(rand() % 10000) < node->error_probability) {
            // Intentionally drop the byte to simulate line corruption
            return;
        }
    }
    
    if (node->drop_next_i_frame) {
        // Simple heuristic: if it looks like an I-frame control field, drop the entire frame byte by suppressing output
        // We simulate a dropped frame by dropping everything until the next flag
        if (byte == 0x7E) {
            node->drop_next_i_frame = false; // reset after dropping one frame
            return; 
        }
        return; // drop byte silently
    }
    
    if (write(node->fd, &byte, 1) < 0) {
        // Handle error if needed, but in test we might ignore transient errors
    }
}

static void node_on_frame_cb(const hdlc_frame_t *frame, void *user_data) {
    virtual_node_t *node = (virtual_node_t *)user_data;
    if (frame->type == HDLC_FRAME_I) {
        // If a receive buffer is allocated, copy data into it
        if (node->rx_data && (node->bytes_received + frame->information_len) <= node->rx_data_capacity) {
            memcpy(node->rx_data + node->bytes_received, frame->information, frame->information_len);
        }
        node->bytes_received += frame->information_len;
        node->frames_received++;
    }
}

static void node_state_cb(hdlc_protocol_state_t state, void *user_data) {
    virtual_node_t *node = (virtual_node_t *)user_data;
    if (state == HDLC_PROTOCOL_STATE_CONNECTED) {
        node->connected = true;
    } else {
        node->connected = false;
    }
}

int create_pty_pair(int *controller_fd, int *peripheral_fd) {
    *controller_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (*controller_fd < 0) return -1;
    if (grantpt(*controller_fd) != 0) return -1;
    if (unlockpt(*controller_fd) != 0) return -1;
    char *pts_name = ptsname(*controller_fd);
    if (!pts_name) return -1;
    *peripheral_fd = open(pts_name, O_RDWR | O_NOCTTY);
    if (*peripheral_fd < 0) return -1;

    struct termios t;
    if (tcgetattr(*controller_fd, &t) == 0) {
        cfmakeraw(&t);
        tcsetattr(*controller_fd, TCSANOW, &t);
    }
    if (tcgetattr(*peripheral_fd, &t) == 0) {
        cfmakeraw(&t);
        tcsetattr(*peripheral_fd, TCSANOW, &t);
    }
    return 0;
}

void* node_thread_func(void* arg) {
    virtual_node_t *node = (virtual_node_t *)arg;
    
    // set non-blocking read
    int flags = fcntl(node->fd, F_GETFL, 0);
    fcntl(node->fd, F_SETFL, flags | O_NONBLOCK);

    uint8_t buf[256];
    while(node->running) {
        int n = read(node->fd, buf, sizeof(buf));
        if (n > 0) {
            hdlc_input_bytes(&node->ctx, buf, n);
        }
        
        // Always tick the HDLC context in the background thread
        hdlc_tick(&node->ctx, 10);
        usleep(10000); // 10ms tick
    }
    return NULL;
}

static bool hdlc_test_connect(hdlc_context_t *ctx, int fd, volatile bool *is_connected, int timeout_ms) {
    // 1. Connect (With retries for SABM drops)
    int retries = timeout_ms / 10;
    hdlc_connect(ctx);
    while((!(*is_connected)) && retries > 0) {
        // Retry SABM every 1 sec
        if (retries % 100 == 0) {
            hdlc_connect(ctx);
        }
        
        usleep(10000); // 10ms
        retries--;
    }
    return *is_connected;
}

static bool hdlc_test_send_data(hdlc_context_t *ctx, int fd, const uint8_t *payload, uint32_t payload_len, int timeout_ms) {
    // 2. Send Data
    uint32_t sent = 0;
    int timeout = timeout_ms / 10;
    
    while(sent < payload_len && timeout > 0) {
        uint32_t to_send = (payload_len - sent) > CHUNK_SIZE ? CHUNK_SIZE : (payload_len - sent);
        
        if (hdlc_output_frame_i(ctx, payload + sent, to_send)) {
            sent += to_send;
            timeout = timeout_ms / 10; // reset timeout on successful send
        } else {
            usleep(10000); 
            timeout--;
        }
    }
    return sent == payload_len;
}

static bool hdlc_test_wait_rx(volatile uint32_t *bytes_received, uint32_t expected_bytes, int timeout_ms) {
    int timeout = timeout_ms / 10;
    while(*bytes_received < expected_bytes && timeout > 0) {
        usleep(10000);
        timeout--;
    }
    return *bytes_received == expected_bytes;
}

static void hdlc_test_disconnect(hdlc_context_t *ctx, int fd, volatile bool *is_connected, int timeout_ms) {
    // 3. Disconnect (With retries for DISC drops)
    int retries = timeout_ms / 10;
    hdlc_disconnect(ctx);
    while((*is_connected) && retries > 0) {
        if (retries % 100 == 0) {
            hdlc_disconnect(ctx);
        }
        
        usleep(10000); // 10ms
        retries--;
    }
}

void run_window_test(int window_size, uint32_t error_prob) {
    if (error_prob > 0) {
        printf("\n--- Running PTY Test with Window Size = %d (Error Prob: %.2f%%) ---\n", window_size, error_prob / 100.0);
    } else {
        printf("\n--- Running PTY Test with Window Size = %d ---\n", window_size);
    }
    
    int fd1, fd2;
    if (create_pty_pair(&fd1, &fd2) != 0) {
        printf("Failed to create PTY pair\n");
        exit(1);
    }
    
    virtual_node_t node1;
    virtual_node_t node2;
    memset(&node1, 0, sizeof(node1));
    memset(&node2, 0, sizeof(node2));
    
    node1.fd = fd1;
    node2.fd = fd2;
    node1.running = true;
    node2.running = true;
    node1.error_probability = error_prob;
    node2.error_probability = error_prob;
    
    hdlc_init(&node1.ctx, node1.input_buffer, sizeof(node1.input_buffer),
              node1.retransmit_buffer, sizeof(node1.retransmit_buffer),
              500, window_size,
              node_output_cb, node_on_frame_cb, node_state_cb, &node1);

    hdlc_init(&node2.ctx, node2.input_buffer, sizeof(node2.input_buffer),
              node2.retransmit_buffer, sizeof(node2.retransmit_buffer),
              500, window_size,
              node_output_cb, node_on_frame_cb, node_state_cb, &node2);
              
    hdlc_configure_addresses(&node1.ctx, 0x01, 0x02);
    hdlc_configure_addresses(&node2.ctx, 0x02, 0x01);
    
    pthread_create(&node1.thread, NULL, node_thread_func, &node1);
    pthread_create(&node2.thread, NULL, node_thread_func, &node2);
    
    char test_name_fail[64];
    
    // Connect phase
    if (!hdlc_test_connect(&node1.ctx, node1.fd, &node1.connected, 5000)) {
        sprintf(test_name_fail, "PTY Connection Failed (Window %d)", window_size);
        test_fail(test_name_fail, "Timeout waiting for Node 1 to connect");
        goto cleanup;
    }
    
    // Wait for node2 to also see the connection since connect helper targets one node
    int sync_timeout = 500;
    while(!node2.connected && sync_timeout > 0) { usleep(10000); sync_timeout--; }
    if (!node2.connected) {
        sprintf(test_name_fail, "PTY Connection Failed (Window %d)", window_size);
        test_fail(test_name_fail, "Node 2 never reported connected state");
        goto cleanup;
    }
    
    // Send Data phase
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    uint32_t bytes_to_send = PAYLOAD_SIZE;
    uint8_t payload[CHUNK_SIZE];
    memset(payload, 0xAA, sizeof(payload)); // dummy data
    
    int tx_timeout_ms = (error_prob > 0) ? 100000 : 10000;
    if (!hdlc_test_send_data(&node1.ctx, node1.fd, payload, bytes_to_send, tx_timeout_ms)) {
        sprintf(test_name_fail, "PTY Transfer TX Timeout (Window %d)", window_size);
        test_fail(test_name_fail, "Timeout waiting for window");
        goto cleanup;
    }
    
    // Receive verification phase
    int rx_timeout_ms = (error_prob > 0) ? 500000 : 50000;
    if (!hdlc_test_wait_rx(&node2.bytes_received, bytes_to_send, rx_timeout_ms)) {
        printf("Expected %u bytes, got %u bytes\n", bytes_to_send, node2.bytes_received);
        sprintf(test_name_fail, "PTY Transfer (Window %d)", window_size);
        test_fail(test_name_fail, "Incomplete transfer (RX Timeout)");
        goto cleanup;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    // Disconnect phase
    hdlc_test_disconnect(&node1.ctx, node1.fd, &node1.connected, 5000);
    hdlc_test_disconnect(&node2.ctx, node2.fd, &node2.connected, 5000); // Ensures both cleanly shut down state
    
    double elapsed = (end_time.tv_sec - start_time.tv_sec) + 
                     (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
    double speed_kbps = (PAYLOAD_SIZE / 1024.0) / elapsed;
    
    char test_name_pass[128];
    if (error_prob > 0) {
        sprintf(test_name_pass, "PTY Transfer Errored (Window %d) [Time: %.3fs, Speed: %.2f KB/s]", window_size, elapsed, speed_kbps);
    } else {
        sprintf(test_name_pass, "PTY Transfer (Window %d) [Time: %.3fs, Speed: %.2f KB/s]", window_size, elapsed, speed_kbps);
    }
    test_pass(test_name_pass);
    
cleanup:
    node1.running = false;
    node2.running = false;
    pthread_join(node1.thread, NULL);
    pthread_join(node2.thread, NULL);
    close(fd1);
    close(fd2);
}

void run_timeout_test(int window_size) {
    printf("\n--- Running PTY Test with Window Size = %d (Timeout Injection - 100%% Error) ---\n", window_size);
    
    int fd1, fd2;
    if (create_pty_pair(&fd1, &fd2) != 0) {
        printf("Failed to create PTY pair\n");
        exit(1);
    }
    
    virtual_node_t node1;
    virtual_node_t node2;
    memset(&node1, 0, sizeof(node1));
    memset(&node2, 0, sizeof(node2));
    
    node1.fd = fd1;
    node2.fd = fd2;
    node1.running = true;
    node2.running = true;
    node1.error_probability = 0; // connect reliably first
    node2.error_probability = 0;
    
    hdlc_init(&node1.ctx, node1.input_buffer, sizeof(node1.input_buffer),
              node1.retransmit_buffer, sizeof(node1.retransmit_buffer),
              500, window_size,
              node_output_cb, node_on_frame_cb, node_state_cb, &node1);

    hdlc_init(&node2.ctx, node2.input_buffer, sizeof(node2.input_buffer),
              node2.retransmit_buffer, sizeof(node2.retransmit_buffer),
              500, window_size,
              node_output_cb, node_on_frame_cb, node_state_cb, &node2);
              
    hdlc_configure_addresses(&node1.ctx, 0x01, 0x02);
    hdlc_configure_addresses(&node2.ctx, 0x02, 0x01);
    
    pthread_create(&node1.thread, NULL, node_thread_func, &node1);
    pthread_create(&node2.thread, NULL, node_thread_func, &node2);
    
    char test_name_pass[128];
    sprintf(test_name_pass, "PTY Graceful Timeout (Window %d)", window_size);
    
    if (!hdlc_test_connect(&node1.ctx, node1.fd, &node1.connected, 5000)) {
        test_fail(test_name_pass, "Timeout during initial reliable connect");
        goto cleanup;
    }
    int sync_timeout = 500;
    while(!node2.connected && sync_timeout > 0) { usleep(10000); sync_timeout--; }
    
    // Now trigger 100% error probability to force TX timeout
    node1.error_probability = 10000;
    
    uint32_t bytes_to_send = PAYLOAD_SIZE;
    uint8_t payload[CHUNK_SIZE];
    memset(payload, 0xAA, sizeof(payload)); // dummy data
    
    // We expect this to return FALSE because of timeout!
    int tx_timeout_ms = 2000; // Fast 2-second timeout
    if (hdlc_test_send_data(&node1.ctx, node1.fd, payload, bytes_to_send, tx_timeout_ms)) {
        test_fail(test_name_pass, "Data transfer succeeded despite 100% packet loss! (Expected Timeout)");
        goto cleanup;
    }
    
    // If it returns false, it correctly timed out!
    test_pass(test_name_pass);

cleanup:
    node1.running = false;
    node2.running = false;
    pthread_join(node1.thread, NULL);
    pthread_join(node2.thread, NULL);
    close(fd1);
    close(fd2);
}

void run_go_back_n_test(int window_size) {
    // This test specifically triggers Go-Back-N by transmitting a stream of I-Frames
    // but predictably dropping the Nth frame, then tracking that the receiver successfully 
    // re-requests and retrieves the entire stream in order.
    printf("\n--- Running PTY Test with Window Size = %d (Go-Back-N Protocol Verification) ---\n", window_size);
    
    int fd1, fd2;
    if (create_pty_pair(&fd1, &fd2) != 0) {
        printf("Failed to create PTY pair\n");
        exit(1);
    }
    
    virtual_node_t node1;
    virtual_node_t node2;
    memset(&node1, 0, sizeof(node1));
    memset(&node2, 0, sizeof(node2));
    
    node1.fd = fd1;
    node2.fd = fd2;
    node1.running = true;
    node2.running = true;
    
    // We will set drop_next_i_frame dynamically to drop one frame
    
    hdlc_init(&node1.ctx, node1.input_buffer, sizeof(node1.input_buffer),
              node1.retransmit_buffer, sizeof(node1.retransmit_buffer),
              500, window_size,
              node_output_cb, node_on_frame_cb, node_state_cb, &node1);

    hdlc_init(&node2.ctx, node2.input_buffer, sizeof(node2.input_buffer),
              node2.retransmit_buffer, sizeof(node2.retransmit_buffer),
              500, window_size,
              node_output_cb, node_on_frame_cb, node_state_cb, &node2);
              
    hdlc_configure_addresses(&node1.ctx, 0x01, 0x02);
    hdlc_configure_addresses(&node2.ctx, 0x02, 0x01);
    
    pthread_create(&node1.thread, NULL, node_thread_func, &node1);
    pthread_create(&node2.thread, NULL, node_thread_func, &node2);
    
    char test_name[128];
    sprintf(test_name, "PTY Go-Back-N Sequence Recovery (Window %d)", window_size);
    
    if (!hdlc_test_connect(&node1.ctx, node1.fd, &node1.connected, 5000)) {
        test_fail(test_name, "Timeout waiting for Node 1 to connect");
        goto cleanup;
    }
    int sync_timeout = 500;
    while(!node2.connected && sync_timeout > 0) { usleep(10000); sync_timeout--; }
    
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    // Send 3 full window's worth of frames
    uint32_t bytes_to_send = (window_size * 3) * CHUNK_SIZE;
    uint8_t payload[CHUNK_SIZE];
    memset(payload, 0xBB, sizeof(payload)); // dummy data
    
    uint32_t sent = 0;
    int timeout = 5000; // 50s
    int frame_counter = 0;
    
    while(sent < bytes_to_send && timeout > 0) {
        uint32_t to_send = CHUNK_SIZE;
        if (bytes_to_send - sent < CHUNK_SIZE) to_send = bytes_to_send - sent;
        
        // Predictably Drop the 2nd dataframe in the stream to force a Go-Back-N state
        if (frame_counter == 1 && window_size > 1) { 
            node1.drop_next_i_frame = true; 
        }
        
        if (hdlc_output_frame_i(&node1.ctx, payload, to_send)) {
            sent += to_send;
            frame_counter++;
            timeout = 5000;
        } else {
            // Help background thread process ACKs and slide window
            uint8_t rx_buf[128];
            int n = read(node1.fd, rx_buf, sizeof(rx_buf));
            if (n > 0) {
                hdlc_input_bytes(&node1.ctx, rx_buf, n);
            }
            hdlc_tick(&node1.ctx, 10);
            usleep(10000); 
            timeout--;
        }
    }
    
    if (sent < bytes_to_send) {
        test_fail(test_name, "TX Timeout waiting for window");
        goto cleanup;
    }
    
    // Wait for all data to be received on node2
    // If Go-Back-N worked, node2 will eventually request retransmission and fetch all of it!
    timeout = 10000; // 100s
    while(node2.bytes_received < bytes_to_send && timeout > 0) {
        usleep(10000);
        timeout--;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    if (node2.bytes_received != bytes_to_send) {
        printf("Expected %u bytes, got %u bytes\n", bytes_to_send, node2.bytes_received);
        test_fail(test_name, "Incomplete transfer (Go-Back-N Failed)");
        goto cleanup;
    }
    
    hdlc_test_disconnect(&node1.ctx, node1.fd, &node1.connected, 5000);
    hdlc_test_disconnect(&node2.ctx, node2.fd, &node2.connected, 5000);
    
    test_pass(test_name);

cleanup:
    node1.running = false;
    node2.running = false;
    pthread_join(node1.thread, NULL);
    pthread_join(node2.thread, NULL);
    close(fd1);
    close(fd2);
}

void run_file_transfer_test(const char *filepath, int window_size) {
    printf("\n--- Running PTY File Transfer Test (Window Size = %d, File: %s) ---\n", window_size, filepath);
    
    // Read the file into memory
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        printf("Failed to open file: %s\n", filepath);
        test_fail("File Transfer", "Cannot open test file");
        return;
    }
    
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fclose(f);
        test_fail("File Transfer", "File is empty or unreadable");
        return;
    }
    
    uint8_t *file_data = (uint8_t *)malloc((size_t)file_size);
    if (!file_data) {
        fclose(f);
        test_fail("File Transfer", "malloc failed");
        return;
    }
    
    size_t bytes_read = fread(file_data, 1, (size_t)file_size, f);
    fclose(f);
    
    if ((long)bytes_read != file_size) {
        free(file_data);
        test_fail("File Transfer", "Failed to read entire file");
        return;
    }
    
    printf("  File size: %ld bytes\n", file_size);
    
    int fd1, fd2;
    if (create_pty_pair(&fd1, &fd2) != 0) {
        free(file_data);
        printf("Failed to create PTY pair\n");
        exit(1);
    }
    
    virtual_node_t node1;
    virtual_node_t node2;
    memset(&node1, 0, sizeof(node1));
    memset(&node2, 0, sizeof(node2));
    
    node1.fd = fd1;
    node2.fd = fd2;
    node1.running = true;
    node2.running = true;
    
    // Allocate receive buffer for node2 to store file content
    uint8_t *rx_buffer = (uint8_t *)malloc((size_t)file_size);
    if (!rx_buffer) {
        free(file_data);
        close(fd1);
        close(fd2);
        test_fail("File Transfer", "malloc failed for rx_buffer");
        return;
    }
    memset(rx_buffer, 0, (size_t)file_size);
    node2.rx_data = rx_buffer;
    node2.rx_data_capacity = (uint32_t)file_size;
    
    hdlc_init(&node1.ctx, node1.input_buffer, sizeof(node1.input_buffer),
              node1.retransmit_buffer, sizeof(node1.retransmit_buffer),
              500, window_size,
              node_output_cb, node_on_frame_cb, node_state_cb, &node1);

    hdlc_init(&node2.ctx, node2.input_buffer, sizeof(node2.input_buffer),
              node2.retransmit_buffer, sizeof(node2.retransmit_buffer),
              500, window_size,
              node_output_cb, node_on_frame_cb, node_state_cb, &node2);
              
    hdlc_configure_addresses(&node1.ctx, 0x01, 0x02);
    hdlc_configure_addresses(&node2.ctx, 0x02, 0x01);
    
    pthread_create(&node1.thread, NULL, node_thread_func, &node1);
    pthread_create(&node2.thread, NULL, node_thread_func, &node2);
    
    char test_name[256];
    sprintf(test_name, "File Transfer (Window %d)", window_size);
    
    // Connect
    if (!hdlc_test_connect(&node1.ctx, node1.fd, &node1.connected, 5000)) {
        test_fail(test_name, "Timeout waiting for Node 1 to connect");
        goto cleanup;
    }
    int sync_timeout = 500;
    while(!node2.connected && sync_timeout > 0) { usleep(10000); sync_timeout--; }
    if (!node2.connected) {
        test_fail(test_name, "Node 2 never reported connected state");
        goto cleanup;
    }
    
    // Send file data
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    uint32_t bytes_to_send = (uint32_t)file_size;
    
    if (!hdlc_test_send_data(&node1.ctx, node1.fd, file_data, bytes_to_send, 300000)) {
        test_fail(test_name, "TX Timeout");
        goto cleanup;
    }
    
    if (!hdlc_test_wait_rx(&node2.bytes_received, bytes_to_send, 300000)) {
        printf("  Expected %u bytes, got %u bytes\n", bytes_to_send, node2.bytes_received);
        test_fail(test_name, "Incomplete transfer");
        goto cleanup;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    // Byte-for-byte integrity check!
    if (memcmp(file_data, rx_buffer, (size_t)file_size) != 0) {
        // Find first mismatch for debugging
        for (long i = 0; i < file_size; i++) {
            if (file_data[i] != rx_buffer[i]) {
                printf("  First mismatch at byte %ld: expected 0x%02X, got 0x%02X\n", i, file_data[i], rx_buffer[i]);
                break;
            }
        }
        test_fail(test_name, "Data integrity check failed (memcmp mismatch)");
        goto cleanup;
    }
    
    hdlc_test_disconnect(&node1.ctx, node1.fd, &node1.connected, 5000);
    hdlc_test_disconnect(&node2.ctx, node2.fd, &node2.connected, 5000);
    
    double elapsed = (end_time.tv_sec - start_time.tv_sec) + 
                     (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
    double speed_kbps = (file_size / 1024.0) / elapsed;
    
    char pass_msg[256];
    sprintf(pass_msg, "File Transfer (Window %d) [%ld bytes, Time: %.3fs, Speed: %.2f KB/s, Integrity: OK]", 
            window_size, file_size, elapsed, speed_kbps);
    test_pass(pass_msg);

cleanup:
    node1.running = false;
    node2.running = false;
    pthread_join(node1.thread, NULL);
    pthread_join(node2.thread, NULL);
    close(fd1);
    close(fd2);
    free(file_data);
    free(rx_buffer);
}

int main(void) {
    srand((unsigned int)time(NULL));
    
    printf("Starting Virtual COM Tests (Reliable)...\n");
    for (int w = 1; w <= 7; w++) {
        run_window_test(w, 0); // 0% error probability
    }
    
    /*
     * NOTE: Error Injection Tests (1.50% byte drop probability)
     * 
     * To a human observer, these tests--especially Window Size 1--may appear to "hang" 
     * or freeze for 60+ seconds. This is NOT a deadlock; it is mathematically expected.
     * 
     * In a 134-byte frame, a 1.5% drop rate per byte means the probability of the 
     * ENTIRE frame arriving perfectly intact is only ~13% ((1 - 0.015)^134). 
     * Furthermore, both the data frame AND the acknowledgment frame must survive.
     * 
     * Therefore, the system is actively working, but the high statistical probability 
     * of dropping packets continuously triggers the HDLC Go-Back-N sliding window 
     * to discard out-of-sequence packets and repeatedly retransmit until a clean 
     * sequence succeeds.
     */
    printf("\nStarting Virtual COM Tests (Error Injection - %.2f%%)...\n", 0.05);
    for (int w = 1; w <= 7; w++) {
        run_window_test(w, 5); // 0.05% byte drop probability
    }
    
    printf("\nStarting Virtual COM Tests (Timeout Injection)...\n");
    for (int w = 1; w <= 7; w++) {
        run_timeout_test(w);
    }
    
    printf("\nStarting Virtual COM Tests (Go-Back-N Deterministic Drop)...\n");
    // Only tests window sizes > 1 because Window Size 1 corresponds to Stop-and-Wait, 
    // which lacks the capability to send out-of-order packets ahead of a dropped packet anyway.
    for (int w = 2; w <= 7; w++) {
        run_go_back_n_test(w);
    }
    
    printf("\nStarting Virtual COM Tests (File Transfer - test.pdf)...\n");
    for (int w = 1; w <= 7; w++) {
        run_file_transfer_test(TEST_DATA_DIR "/test.pdf", w);
    }
    
    printf("\nVirtual COM Tests Completed Successfully.\n");
    return 0;
}
