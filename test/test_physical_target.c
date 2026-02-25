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
#define CHUNK_SIZE 355
#define BUFFER_SIZE 4096
#define PDF_PATH "../test/test.pdf"

typedef struct {
    int fd;
    pthread_mutex_t ctx_lock;
    hdlc_context_t ctx;
    hdlc_u8 input_buffer[BUFFER_SIZE * 2];
    hdlc_u8 retransmit_buffer[BUFFER_SIZE * 2 * 8];    
    pthread_t rx_thread;
    volatile bool running;
    
    /* Receive buffer for integrity check */
    uint8_t *recv_buffer;
    uint32_t recv_buffer_len;
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
            while (written < (int)tx_index) {
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
        /* Copy received data into buffer for integrity check */
        if (node->recv_buffer != NULL && frame->information_len > 0) {
            uint32_t space = node->recv_buffer_len - node->bytes_received;
            uint32_t copy_len = frame->information_len;
            if (copy_len > space) copy_len = space;
            memcpy(node->recv_buffer + node->bytes_received, frame->information, copy_len);
        }
        node->bytes_received += frame->information_len;
        
        printf("\rReceived UI frame #%u (len=%u)         \n", node->frames_received, frame->information_len);
        fflush(stdout);
    } else {
        printf("\nReceived non-UI frame: type=%d, len=%u, ctrl=%02X\n", frame->type, frame->information_len, frame->control.value);
    }
}

static void node_state_cb(hdlc_protocol_state_t state, void *user_data) {
    (void)user_data;
    if (state == HDLC_PROTOCOL_STATE_CONNECTED) {
        printf("\nLogical connection established!\n");
    } else if (state == HDLC_PROTOCOL_STATE_DISCONNECTED) {
        printf("\n[Error] Logical connection dropped! Max retries reached or DISC received.\n");
    }
}

static bool wait_for_connection(physical_node_t *node, int timeout_ms) {
    int retries = timeout_ms;
    pthread_mutex_lock(&node->ctx_lock);
    hdlc_connect(&node->ctx);
    pthread_mutex_unlock(&node->ctx_lock);
    
    while(node->ctx.current_state != HDLC_PROTOCOL_STATE_CONNECTED && retries > 0) {
        usleep(1000);
        
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

/* Load a file into a malloc'd buffer. Returns NULL on failure. */
static uint8_t* load_file(const char *path, uint32_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("Error: Cannot open file '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = (uint32_t)size;
    return buf;
}

int main(void) {
    printf("Starting Physical Target Test on %s\n", SERIAL_PORT);
    
    /* Load PDF file */
    uint32_t pdf_size = 0;
    uint8_t *pdf_data = load_file(PDF_PATH, &pdf_size);
    if (pdf_data == NULL || pdf_size == 0) {
        test_fail("Physical Target PDF Load", "Cannot load test PDF file.");
        return 1;
    }
    printf("Loaded %s (%u bytes)\n", PDF_PATH, pdf_size);
    
    physical_node_t node;
    memset(&node, 0, sizeof(node));
    
    /* Allocate receive buffer for integrity verification */
    node.recv_buffer = (uint8_t *)malloc(pdf_size);
    if (!node.recv_buffer) {
        test_fail("Physical Target Alloc", "Cannot allocate receive buffer.");
        free(pdf_data);
        return 1;
    }
    node.recv_buffer_len = pdf_size;
    memset(node.recv_buffer, 0, pdf_size);
    
    node.fd = open_serial_port(SERIAL_PORT);
    if (node.fd < 0) {
        test_fail("Physical Target Open", "Cannot open serial port.");
        free(pdf_data);
        free(node.recv_buffer);
        return 1;
    }
    
    pthread_mutex_init(&node.ctx_lock, NULL);
    node.running = true;
    
    hdlc_init(&node.ctx, node.input_buffer, sizeof(node.input_buffer),
              node.retransmit_buffer, sizeof(node.retransmit_buffer),
              1000, 7, 3, node_output_cb, node_on_frame_cb, node_state_cb, &node);

    hdlc_configure_addresses(&node.ctx, 0x01, 0x02);

    pthread_create(&node.rx_thread, NULL, serial_rx_thread, &node);

    printf("Connecting to target...\n");
    if (!wait_for_connection(&node, 10000)) {
        printf("Warning: Failed to establish HDLC connection. Proceeding to send anyway...\n");
    }

    uint32_t total_chunks = (pdf_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
    printf("Connected! Sending %s (%u bytes) in %d-byte chunks...\n", PDF_PATH, pdf_size, CHUNK_SIZE);
    
    uint32_t sent_bytes = 0;
    double start_time = get_time_s_local();
    
    while (sent_bytes < pdf_size) {
        uint32_t chunk = CHUNK_SIZE;
        if (pdf_size - sent_bytes < CHUNK_SIZE) {
            chunk = pdf_size - sent_bytes;
        }
        
        bool sent_ok = false;
        long stuck_count = 0;

        while (!sent_ok && node.running) {
            pthread_mutex_lock(&node.ctx_lock);
            if (node.ctx.current_state == HDLC_PROTOCOL_STATE_CONNECTED) {
                sent_ok = hdlc_output_frame_i(&node.ctx, pdf_data + sent_bytes, chunk);
            } else {
                stuck_count++;
                if (stuck_count % 1000 == 0) {
                    printf("  [Error] Disconnected while sending... Target dropped the link.\n");
                    node.running = false;
                }
            }
            pthread_mutex_unlock(&node.ctx_lock);
            
            if (!sent_ok && node.running) {
                stuck_count++;
                if (stuck_count % 10000 == 0) {
                    printf("  [Wait] TX Window full. V(S)=%u, V(R)=%u, V(A)=%u\n",
                           node.ctx.vs, node.ctx.vr, node.ctx.va);
                }
                usleep(100);
            }
        }
        
        sent_bytes += chunk;
        
        if (sent_bytes % (CHUNK_SIZE * 20) == 0) {
            printf("\rSent %u bytes, Echoed %u bytes...   ", sent_bytes, node.bytes_received);
            fflush(stdout);
        }
        
        usleep(100); 
    }

    if (!node.running) {
        printf("\nTest aborted due to disconnection.\n");
    } else {
        printf("\nFinished transmitting %u bytes. Waiting for final echoes...\n", sent_bytes);
        
        int timeout = 100; // 10 seconds timeout for final echoes
        while (node.bytes_received < pdf_size && timeout > 0 && node.running) {
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
    double kbps = (pdf_size * 8) / (duration * 1000.0);
    
    printf("\n\n--- Test Results ---\n");
    printf("Total Sent    : %u bytes\n", sent_bytes);
    printf("Total Received: %u bytes\n", node.bytes_received);
    printf("Frames Rcvd   : %u\n", node.frames_received);
    printf("Time Taken    : %.2f seconds\n", duration);
    printf("Throughput    : %.2f kbps\n", kbps);

    /* File Integrity Check */
    if (node.bytes_received == pdf_size) {
        bool match = (memcmp(pdf_data, node.recv_buffer, pdf_size) == 0);
        if (match) {
            test_pass("Physical Target PDF Echo Test (Perfect Match!)");
        } else {
            /* Find first mismatch for diagnostics */
            uint32_t mismatch_pos = 0;
            for (uint32_t i = 0; i < pdf_size; i++) {
                if (pdf_data[i] != node.recv_buffer[i]) {
                    mismatch_pos = i;
                    break;
                }
            }
            char msg[128];
            snprintf(msg, sizeof(msg), 
                     "Data mismatch at byte %u: sent=0x%02X, recv=0x%02X", 
                     mismatch_pos, pdf_data[mismatch_pos], node.recv_buffer[mismatch_pos]);
            test_fail("Physical Target PDF Echo Test", msg);
        }
    } else if (node.bytes_received > 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), 
                 "Size mismatch: sent=%u, received=%u", pdf_size, node.bytes_received);
        test_fail("Physical Target PDF Echo Test", msg);
    } else {
        test_fail("Physical Target PDF Echo Test", "No echo data received");
    }

    node.running = false;
    pthread_join(node.rx_thread, NULL);
    pthread_mutex_destroy(&node.ctx_lock);
    close(node.fd);
    free(pdf_data);
    free(node.recv_buffer);

    return 0;
}
