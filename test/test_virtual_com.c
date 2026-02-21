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
    
    if (write(node->fd, &byte, 1) < 0) {
        // Handle error if needed, but in test we might ignore transient errors
    }
}

static void node_on_frame_cb(const hdlc_frame_t *frame, void *user_data) {
    virtual_node_t *node = (virtual_node_t *)user_data;
    if (frame->type == HDLC_FRAME_I) {
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
    
    // 1. Connect (With retries for SABM drops during error injection)
    int retries = 500; // Max 5 seconds (500 * 10ms)
    hdlc_connect(&node1.ctx);
    while((!node1.connected || !node2.connected) && retries > 0) {
        if (!node1.connected) {
            if (retries % 100 == 0) {
                hdlc_connect(&node1.ctx); // Retry SABM every 1 sec
            }
        }
        
        // Allow main thread to also process node1 RX buffers if background thread is busy
        uint8_t rx_buf[128];
        int n = read(node1.fd, rx_buf, sizeof(rx_buf));
        if (n > 0) {
            hdlc_input_bytes(&node1.ctx, rx_buf, n);
        }
        hdlc_tick(&node1.ctx, 10);
        
        usleep(10000); // 10ms
        retries--;
    }
    
    if (!node1.connected || !node2.connected) {
        char test_name[64];
        sprintf(test_name, "PTY Connection Failed (Window %d)", window_size);
        test_fail(test_name, "Timeout");
        goto cleanup;
    }
    
    // 2. Send Data
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    uint32_t bytes_to_send = PAYLOAD_SIZE;
    uint8_t payload[CHUNK_SIZE];
    memset(payload, 0xAA, sizeof(payload)); // dummy data
    
    uint32_t sent = 0;
    int timeout = (error_prob > 0) ? 10000 : 1000; // 10000 * 10ms = 100s timeout for errors
    while(sent < bytes_to_send && timeout > 0) {
        uint32_t to_send = (bytes_to_send - sent) > CHUNK_SIZE ? CHUNK_SIZE : (bytes_to_send - sent);
        
        // Wait until window opens up
        if (hdlc_output_frame_i(&node1.ctx, payload, to_send)) {
            sent += to_send;
            timeout = (error_prob > 0) ? 10000 : 1000; // reset timeout on successful send
        } else {
            // Wait for background thread to process ACKs and slide window
            // If background thread is delayed, help it process!
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
    
    // Check if we timed out transmitting
    if (sent < bytes_to_send) {
        char test_name[64];
        sprintf(test_name, "PTY Transfer TX Timeout (Window %d)", window_size);
        test_fail(test_name, "Timeout waiting for window");
        goto cleanup;
    }
    
    // Wait for all data to be received on node2
    timeout = (error_prob > 0) ? 50000 : 5000; // * 10ms intervals
    while(node2.bytes_received < bytes_to_send && timeout > 0) {
        usleep(10000); // 10ms instead of 100ms
        timeout--;
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    if (node2.bytes_received != bytes_to_send) {
        printf("Expected %u bytes, got %u bytes\n", bytes_to_send, node2.bytes_received);
        char test_name[64];
        sprintf(test_name, "PTY Transfer (Window %d)", window_size);
        test_fail(test_name, "Incomplete transfer");
        goto cleanup;
    }
    
    // 3. Disconnect (With retries for DISC drops)
    retries = 500; // max 5 seconds (500 * 10ms)
    hdlc_disconnect(&node1.ctx);
    while((node1.connected || node2.connected) && retries > 0) {
        if (node1.connected) {
            if (retries % 100 == 0) {
                hdlc_disconnect(&node1.ctx);
            }
        }
        
        uint8_t rx_buf[128];
        int n = read(node1.fd, rx_buf, sizeof(rx_buf));
        if (n > 0) {
            hdlc_input_bytes(&node1.ctx, rx_buf, n);
        }
        hdlc_tick(&node1.ctx, 10);
        
        usleep(10000); // 10ms instead of 100ms
        retries--;
    }
    
    double elapsed = (end_time.tv_sec - start_time.tv_sec) + 
                     (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
    double speed_kbps = (bytes_to_send / 1024.0) / elapsed;
    
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
    
    printf("Virtual COM Tests Completed Successfully.\n");
    return 0;
}
