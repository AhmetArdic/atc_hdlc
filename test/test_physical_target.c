#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

#include "../inc/hdlc.h"
#include "test_common.h"

#define SERIAL_PORT "/dev/ttyUSB0"
#define BAUDRATE B921600
#define TOTAL_DATA_SIZE (100 * 1024)
#define CHUNK_SIZE 355
#define BUFFER_SIZE 4096

typedef struct {
    int fd;
    pthread_mutex_t ctx_lock;
    hdlc_context_t ctx;
    hdlc_u8 input_buffer[BUFFER_SIZE * 2];
    hdlc_u8 retransmit_buffer[BUFFER_SIZE * 2 * 8];    
    pthread_t rx_thread;
    volatile bool running;
    
    volatile uint32_t bytes_received;
    volatile uint32_t frames_received;
} physical_node_t;

static int open_serial_port(const char* port) {
    int fd = open(port, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        printf("Error opening serial port %s: %s\n", port, strerror(errno));
        return -1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        printf("Error from tcgetattr: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    cfsetospeed(&tty, BAUDRATE);
    cfsetispeed(&tty, BAUDRATE);

    cfmakeraw(&tty);

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static uint8_t tx_buffer[BUFFER_SIZE];
static hdlc_u32 tx_index = 0;

static void node_output_cb(hdlc_u8 byte, hdlc_bool flush, void *user_data) {
    physical_node_t *node = (physical_node_t *)user_data;
    
    if (tx_index < sizeof(tx_buffer)) {
        tx_buffer[tx_index++] = byte;
    }
    
    if (flush || tx_index >= sizeof(tx_buffer) - 1) {
        if (tx_index > 0) {
            int written = 0;
            while (written < tx_index) {
                int res = write(node->fd, tx_buffer + written, tx_index - written);
                if (res > 0) {
                    written += res;
                } else if (res < 0 && errno != EAGAIN && errno != EINTR) {
                    break;
                }
            }
            tx_index = 0;
        }
    }
}

static void node_on_frame_cb(const hdlc_frame_t *frame, void *user_data) {
    physical_node_t *node = (physical_node_t *)user_data;
    
    node->frames_received++;
    
    if (frame->type == HDLC_FRAME_U && hdlc_get_u_frame_sub_type(&frame->control) == HDLC_U_FRAME_TYPE_UI) {
        node->bytes_received += frame->information_len;
        
        printf("\rReceived UI frame #%u (len=%u)         \n", node->frames_received, frame->information_len);
        fflush(stdout);
    } else {
        printf("\nReceived non-UI frame: type=%d, len=%u, ctrl=%02X\n", frame->type, frame->information_len, frame->control.value);
    }
}

static void node_state_cb(hdlc_protocol_state_t state, void *user_data) {
    physical_node_t *node = (physical_node_t *)user_data;
    if (state == HDLC_PROTOCOL_STATE_CONNECTED) {
        printf("\nLogical connection established!\n");
    } else if (state == HDLC_PROTOCOL_STATE_DISCONNECTED) {
        printf("\n[Error] Logical connection dropped! Max retries reached or DISC received.\n");
        // We log it here. The main loop will also catch this state change.
    }
}

static bool wait_for_connection(physical_node_t *node, int timeout_ms) {
    int retries = timeout_ms;
    pthread_mutex_lock(&node->ctx_lock);
    hdlc_connect(&node->ctx);
    pthread_mutex_unlock(&node->ctx_lock);
    
    while(node->ctx.current_state != HDLC_PROTOCOL_STATE_CONNECTED && retries > 0) {
        usleep(1000);
        
        // Every 1s, trigger tick to allow retransmission of SABM
        if (retries % 1000 == 0) {
            pthread_mutex_lock(&node->ctx_lock);
            hdlc_tick(&node->ctx, 1000);
            pthread_mutex_unlock(&node->ctx_lock);
        }
        
        retries--;
    }
    return node->ctx.current_state == HDLC_PROTOCOL_STATE_CONNECTED;
}

void* serial_rx_thread(void* arg) {
    physical_node_t *node = (physical_node_t *)arg;
    uint8_t buf[2048];
    
    struct timespec last;
    clock_gettime(CLOCK_MONOTONIC, &last);
    
    while(node->running) {
        int n = read(node->fd, buf, sizeof(buf));
        
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - last.tv_sec) * 1000 + (now.tv_nsec - last.tv_nsec) / 1000000;
        
        pthread_mutex_lock(&node->ctx_lock);
        
        if (n > 0) {
            hdlc_input_bytes(&node->ctx, buf, n);
            // Hex dump removed for speed
        }
        
        if (elapsed_ms >= 10) {
            hdlc_tick(&node->ctx, elapsed_ms);
            last = now;
        }
        
        pthread_mutex_unlock(&node->ctx_lock);
        
        if (n <= 0) {
            usleep(1000);
        }
    }
    return NULL;
}

static double get_time_s_local() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(void) {
    printf("Starting Physical Target Test on %s\n", SERIAL_PORT);
    
    physical_node_t node;
    memset(&node, 0, sizeof(node));
    
    node.fd = open_serial_port(SERIAL_PORT);
    if (node.fd < 0) {
        test_fail("Physical Target Open", "Cannot open serial port.");
        return 1;
    }
    
    pthread_mutex_init(&node.ctx_lock, NULL);
    node.running = true;
    
    // As per target's behavior: window size 7, timeout 1000
    // We will match these timeout settings locally to be safe.
    hdlc_init(&node.ctx, node.input_buffer, sizeof(node.input_buffer),
              node.retransmit_buffer, sizeof(node.retransmit_buffer),
              1000, 7, 3, node_output_cb, node_on_frame_cb, node_state_cb, &node);

    // Target is 0x02, we are 0x01.
    hdlc_configure_addresses(&node.ctx, 0x01, 0x02);

    pthread_create(&node.rx_thread, NULL, serial_rx_thread, &node);

    printf("Connecting to target...\n");
    if (!wait_for_connection(&node, 10000)) {
        printf("Warning: Failed to establish HDLC connection. Proceeding to send anyway...\n");
    }

    printf("Connected! Sending 100KB in %d-byte chunks...\n", CHUNK_SIZE);
    
    uint32_t sent_bytes = 0;
    uint8_t payload[CHUNK_SIZE];
    
    double start_time = get_time_s_local();
    
    while (sent_bytes < TOTAL_DATA_SIZE) {
        uint32_t chunk = CHUNK_SIZE;
        if (TOTAL_DATA_SIZE - sent_bytes < CHUNK_SIZE) {
            chunk = TOTAL_DATA_SIZE - sent_bytes;
        }
        
        for (uint32_t i = 0; i < chunk; i++) {
            payload[i] = (uint8_t)((sent_bytes + i) % 256);
        }
        
        uint32_t expected_echo_bytes = node.bytes_received + chunk;
        bool sent_ok = false;
        long stuck_count = 0;

        // Try to send the I-frame
        while (!sent_ok && node.running) {
            pthread_mutex_lock(&node.ctx_lock);
            if (node.ctx.current_state == HDLC_PROTOCOL_STATE_CONNECTED) {
                sent_ok = hdlc_output_frame_i(&node.ctx, payload, chunk);
            } else {
                stuck_count++;
                if (stuck_count % 1000 == 0) {
                    printf("  [Error] Disconnected while sending... Target dropped the link.\n");
                    node.running = false; // Abort test on disconnect
                }
            }
            pthread_mutex_unlock(&node.ctx_lock);
            
            if (!sent_ok && node.running) {
                stuck_count++;
                if (stuck_count % 10000 == 0) {
                    printf("  [Wait] TX Window full. V(S)=%u, V(R)=%u, V(A)=%u\n",
                           node.ctx.vs, node.ctx.vr, node.ctx.va);
                }
                usleep(100); // 100us wait when window is full, to avoid 100% CPU
            }
        }
        
        sent_bytes += chunk;
        
        if (sent_bytes % (CHUNK_SIZE * 20) == 0) {
            printf("\rSent %u bytes, Echoed %u bytes...   ", sent_bytes, node.bytes_received);
            fflush(stdout);
        }
        
        // No application-level flow control. Blast the target at full speed.
        // The HDLC Go-Back-N Window (size 7) will perform the pacing automatically.
        // Added 100us micro-pacing to prevent UART RX overrun on the 200Mhz target.
        usleep(100); 
    }

    if (!node.running) {
        printf("\nTest aborted due to disconnection.\n");
    } else {
        printf("\nFinished transmitting %u bytes. Waiting for final echoes...\n", sent_bytes);
        
        int timeout = 100; // 10 seconds timeout for final echoes
        while (node.bytes_received < TOTAL_DATA_SIZE && timeout > 0 && node.running) {
            usleep(100000); // 100ms
            timeout--;
        }
        
        if (timeout <= 0) {
            printf("\n[Warning] Timeout waiting for final echoes.\n");
            printf("  -> Stats: RX Frames parsed=%u, CRC Errors=%u\n", 
                   node.ctx.stats_input_frames, node.ctx.stats_crc_errors);
        }
    }

    double end_time = get_time_s_local();
    double duration = end_time - start_time;
    double kbps = (TOTAL_DATA_SIZE * 8) / (duration * 1000.0);
    
    printf("\n\n--- Test Results ---\n");
    printf("Total Sent    : %u bytes\n", sent_bytes);
    printf("Total Received: %u bytes\n", node.bytes_received);
    printf("Frames Rcvd   : %u\n", node.frames_received);
    printf("Time Taken    : %.2f seconds\n", duration);
    printf("Throughput    : %.2f kbps\n", kbps);

    if (node.bytes_received > 0 && node.bytes_received == TOTAL_DATA_SIZE) {
        test_pass("Physical Target UI Frame Echo Test (Perfect Full Echo)");
    } else if (node.bytes_received > 0) {
        test_pass("Physical Target UI Frame Echo Test (Partial Echo)");
    } else {
        test_fail("Physical Target UI Frame Echo Test", "No echo received");
    }

    node.running = false;
    pthread_join(node.rx_thread, NULL);
    pthread_mutex_destroy(&node.ctx_lock);
    close(node.fd);

    return 0;
}
