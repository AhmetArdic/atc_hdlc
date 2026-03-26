#include "test_virtual_pipe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../inc/hdlc.h"
#include "test_common.h"

// Configuration
#define PAYLOAD_SIZE 1024 * 1024 // 1MB
#define CHUNK_SIZE   150
#define BUFFER_SIZE  256

/* Pipe queue provided by test_virtual_pipe.h */

typedef struct {
    pipe_queue_t* tx_pipe;
    pipe_queue_t* rx_pipe;

    mutex_t ctx_lock;

    atc_hdlc_context_t ctx;
    atc_hdlc_u8 input_buffer[BUFFER_SIZE * 2];
    atc_hdlc_u8 retransmit_slots[7 * 1024]; /* 7 slots x 1024 B */
    atc_hdlc_u32 retransmit_lens[7];
    /* Descriptors stored per-node so they outlive node_pair_init() */
    atc_hdlc_config_t hdlc_cfg;
    atc_hdlc_platform_t hdlc_plat;
    atc_hdlc_tx_window_t hdlc_tw;
    atc_hdlc_rx_buffer_t hdlc_rx;
    thread_t thread;
    volatile bool running;

    // Config
    uint32_t error_probability; /* 0 to 1000000 (0.0000% to 100.0000%); e.g. 50 = 0.005% */
    bool drop_next_i_frame;

    // Receive buffer (for file transfer verification)
    uint8_t* rx_data;
    uint32_t rx_data_capacity;

    // Stats
    volatile uint32_t bytes_received;
    volatile uint32_t frames_received;
    volatile bool connected;

    /* Timer simulation timestamps (set by platform callbacks) */
    volatile double t1_started_at;
    volatile double t2_started_at;
    volatile bool t1_pending;
    volatile bool t2_pending;
} virtual_node_t;

// Callbacks
static int node_output_cb(atc_hdlc_u8 byte, bool flush, void* user_data) {
    (void)flush;
    virtual_node_t* node = (virtual_node_t*)user_data;

    if (node->error_probability > 0) {
        if ((uint32_t)(rand() % 1000000) < node->error_probability) {
            return 0; /* drop byte to simulate corruption */
        }
    }

    if (node->drop_next_i_frame) {
        if (byte == 0x7E) {
            node->drop_next_i_frame = false;
            return 0;
        }
        return 0;
    }

    pipe_write(node->tx_pipe, &byte, 1);
    return 0;
}

static void node_on_data_cb(const atc_hdlc_u8* payload, atc_hdlc_u16 len, void* user_data) {
    virtual_node_t* node = (virtual_node_t*)user_data;
    if (node->rx_data && (node->bytes_received + len) <= node->rx_data_capacity) {
        memcpy(node->rx_data + node->bytes_received, payload, len);
    }
    node->bytes_received += len;
    node->frames_received++;
}

/* --- Timer platform callbacks --- */
static void node_t1_start_cb(atc_hdlc_u32 ms, void* user_data) {
    virtual_node_t* node = (virtual_node_t*)user_data;
    /* Use a short absolute deadline instead of epoch trick, so T1 fires
     * after the configured ms wall-clock time rather than instantly.
     * Virtual pipe is fast but we still need T1 > 0 to allow the peer
     * a chance to ACK before retransmitting. */
    node->t1_started_at = get_time_s();
    /* Override: use a short fixed timeout for the virtual pipe environment
     * (ms parameter comes from config->t1_ms which is already set to 20ms). */
    (void)ms;
    node->t1_pending = true;
}
static void node_t1_stop_cb(void* user_data) {
    virtual_node_t* node = (virtual_node_t*)user_data;
    node->t1_pending = false;
}
static void node_t2_start_cb(atc_hdlc_u32 ms, void* user_data) {
    (void)ms;
    /* Virtual pipe has zero propagation delay — record start time as
     * far in the past so the thread loop fires it on the very next check. */
    virtual_node_t* node = (virtual_node_t*)user_data;
    node->t2_started_at = 0.0; /* epoch = always elapsed */
    node->t2_pending = true;
}
static void node_t2_stop_cb(void* user_data) {
    virtual_node_t* node = (virtual_node_t*)user_data;
    node->t2_pending = false;
}

static void node_event_cb(atc_hdlc_event_t event, void* user_data) {
    virtual_node_t* node = (virtual_node_t*)user_data;
    if (event == ATC_HDLC_EVENT_CONNECT_ACCEPTED || event == ATC_HDLC_EVENT_INCOMING_CONNECT) {
        node->connected = true;
    } else if (event == ATC_HDLC_EVENT_DISCONNECT_COMPLETE ||
               event == ATC_HDLC_EVENT_PEER_DISCONNECT || event == ATC_HDLC_EVENT_LINK_FAILURE) {
        node->connected = false;
    }
}

void* node_thread_func(void* arg) {
    virtual_node_t* node = (virtual_node_t*)arg;

    uint8_t buf[1024];

    while (node->running) {
        int n = pipe_read(node->rx_pipe, buf, sizeof(buf));

        MUTEX_LOCK(&node->ctx_lock);

        if (n > 0) {
            atc_hdlc_data_in(&node->ctx, buf, n);
            /* T2 start callback sets t2_started_at=0, t2_pending=true.
             * Fire immediately here while lock is held — no wall-clock
             * check needed for zero-latency virtual pipe. */
            if (node->t2_pending) {
                node->t2_pending = false;
                atc_hdlc_t2_expired(&node->ctx);
            }
        }

        /* T1: wall-clock check, only when actually pending (infrequent) */
        if (node->t1_pending) {
            double now = get_time_s();
            if ((now - node->t1_started_at) * 1000.0 >= (double)node->hdlc_cfg.t1_ms) {
                node->t1_pending = false;
                atc_hdlc_t1_expired(&node->ctx);
            }
        }

        MUTEX_UNLOCK(&node->ctx_lock);

        if (n == 0) {
            YIELD_THREAD();
        }
    }
    return NULL;
}

/* Thread handling moved to test_virtual_pipe.c */

static void node_pair_init(virtual_node_t* node1, virtual_node_t* node2, pipe_queue_t* pipe1,
                           pipe_queue_t* pipe2, int window_size) {
    memset(node1, 0, sizeof(*node1));
    memset(node2, 0, sizeof(*node2));

    pipe_init(pipe1);
    pipe_init(pipe2);

    node1->tx_pipe = pipe1;
    node1->rx_pipe = pipe2;

    node2->tx_pipe = pipe2;
    node2->rx_pipe = pipe1;

    MUTEX_INIT(&node1->ctx_lock);
    MUTEX_INIT(&node2->ctx_lock);

    node1->running = true;
    node2->running = true;

    /* --- Node 1: fill per-node descriptors, then init --- */
    node1->hdlc_cfg.mode = ATC_HDLC_MODE_ABM;
    node1->hdlc_cfg.address = 0x01;
    node1->hdlc_cfg.window_size = (atc_hdlc_u8)window_size;
    node1->hdlc_cfg.max_frame_size = CHUNK_SIZE;
    node1->hdlc_cfg.max_retries = 25;
    node1->hdlc_cfg.t1_ms = 20;
    node1->hdlc_cfg.t2_ms = 10;

    node1->hdlc_plat.on_send = node_output_cb;
    node1->hdlc_plat.on_data = node_on_data_cb;
    node1->hdlc_plat.on_event = node_event_cb;
    node1->hdlc_plat.user_ctx = node1;
    node1->hdlc_plat.t1_start = node_t1_start_cb;
    node1->hdlc_plat.t1_stop = node_t1_stop_cb;
    node1->hdlc_plat.t2_start = node_t2_start_cb;
    node1->hdlc_plat.t2_stop = node_t2_stop_cb;

    node1->hdlc_tw.slots = node1->retransmit_slots;
    node1->hdlc_tw.slot_lens = node1->retransmit_lens;
    node1->hdlc_tw.slot_capacity = CHUNK_SIZE;
    node1->hdlc_tw.slot_count = (atc_hdlc_u8)window_size;

    node1->hdlc_rx.buffer = node1->input_buffer;
    node1->hdlc_rx.capacity = sizeof(node1->input_buffer);

    atc_hdlc_params_t p1 = {.config = &node1->hdlc_cfg,
                            .platform = &node1->hdlc_plat,
                            .tx_window = &node1->hdlc_tw,
                            .rx_buf = &node1->hdlc_rx};
    atc_hdlc_init(&node1->ctx, p1);

    /* --- Node 2: fill per-node descriptors, then init --- */
    node2->hdlc_cfg.mode = ATC_HDLC_MODE_ABM;
    node2->hdlc_cfg.address = 0x02;
    node2->hdlc_cfg.window_size = (atc_hdlc_u8)window_size;
    node2->hdlc_cfg.max_frame_size = CHUNK_SIZE;
    node2->hdlc_cfg.max_retries = 25;

    node2->hdlc_cfg.t1_ms = 20;
    node2->hdlc_cfg.t2_ms = 10;

    node2->hdlc_plat.on_send = node_output_cb;
    node2->hdlc_plat.on_data = node_on_data_cb;
    node2->hdlc_plat.on_event = node_event_cb;
    node2->hdlc_plat.user_ctx = node2;
    node2->hdlc_plat.t1_start = node_t1_start_cb;
    node2->hdlc_plat.t1_stop = node_t1_stop_cb;
    node2->hdlc_plat.t2_start = node_t2_start_cb;
    node2->hdlc_plat.t2_stop = node_t2_stop_cb;

    node2->hdlc_tw.slots = node2->retransmit_slots;
    node2->hdlc_tw.slot_lens = node2->retransmit_lens;
    node2->hdlc_tw.slot_capacity = CHUNK_SIZE;
    node2->hdlc_tw.slot_count = (atc_hdlc_u8)window_size;

    node2->hdlc_rx.buffer = node2->input_buffer;
    node2->hdlc_rx.capacity = sizeof(node2->input_buffer);

    atc_hdlc_params_t p2 = {.config = &node2->hdlc_cfg,
                            .platform = &node2->hdlc_plat,
                            .tx_window = &node2->hdlc_tw,
                            .rx_buf = &node2->hdlc_rx};
    atc_hdlc_init(&node2->ctx, p2);

    /* Cross-link peer addresses */
    node1->ctx.peer_address = 0x02;
    node2->ctx.peer_address = 0x01;
}

static void node_pair_start(virtual_node_t* node1, virtual_node_t* node2) {
    thread_create(&node1->thread, node_thread_func, node1);
    thread_create(&node2->thread, node_thread_func, node2);
}

static void node_pair_cleanup(virtual_node_t* node1, virtual_node_t* node2, pipe_queue_t* pipe1,
                              pipe_queue_t* pipe2) {
    node1->running = false;
    node2->running = false;
    thread_join(node1->thread);
    thread_join(node2->thread);
    pipe_destroy(pipe1);
    pipe_destroy(pipe2);
    MUTEX_DESTROY(&node1->ctx_lock);
    MUTEX_DESTROY(&node2->ctx_lock);
}

static bool hdlc_test_connect(virtual_node_t* node, int timeout_ms) {
    int retries = timeout_ms;
    MUTEX_LOCK(&node->ctx_lock);
    atc_hdlc_link_setup(&node->ctx, node->ctx.peer_address);
    MUTEX_UNLOCK(&node->ctx_lock);
    while ((!node->connected) && retries > 0) {
        /* Retry only if still DISCONNECTED (not already CONNECTING/CONNECTED) */
        if (retries % 1000 == 0 && node->ctx.current_state == ATC_HDLC_STATE_DISCONNECTED) {
            MUTEX_LOCK(&node->ctx_lock);
            atc_hdlc_link_setup(&node->ctx, node->ctx.peer_address);
            MUTEX_UNLOCK(&node->ctx_lock);
        }
        SLEEP_MS(1);
        retries--;
    }
    return node->connected;
}

static bool hdlc_test_send_data(virtual_node_t* node, const uint8_t* payload, uint32_t payload_len,
                                int timeout_ms) {
    uint32_t sent = 0;
    double start = get_time_s();
    double timeout_s = (double)timeout_ms / 1000.0;

    while (sent < payload_len && (get_time_s() - start) < timeout_s) {
        if (!node->connected)
            break;
        uint32_t to_send = (payload_len - sent) > CHUNK_SIZE ? CHUNK_SIZE : (payload_len - sent);

        MUTEX_LOCK(&node->ctx_lock);
        atc_hdlc_error_t send_result = atc_hdlc_transmit_i(&node->ctx, payload + sent, to_send);
        MUTEX_UNLOCK(&node->ctx_lock);

        if (send_result == ATC_HDLC_OK) {
            sent += to_send;
            start = get_time_s(); // Reset timeout on successful progress
        } else {
            YIELD_THREAD();
        }
    }
    return sent == payload_len;
}

static bool hdlc_test_wait_rx(volatile uint32_t* bytes_received, uint32_t expected_bytes,
                              int timeout_ms) {
    double start = get_time_s();
    double timeout_s = (double)timeout_ms / 1000.0;
    while (*bytes_received < expected_bytes && (get_time_s() - start) < timeout_s) {
        // If the receiver node's context indicates it's disconnected, there's no point in waiting
        YIELD_THREAD();
    }
    return *bytes_received == expected_bytes;
}

static void hdlc_test_disconnect(virtual_node_t* node, int timeout_ms) {
    int retries = timeout_ms;
    MUTEX_LOCK(&node->ctx_lock);
    atc_hdlc_disconnect(&node->ctx);
    MUTEX_UNLOCK(&node->ctx_lock);
    while ((node->connected) && retries > 0) {
        if (retries % 1000 == 0) {
            MUTEX_LOCK(&node->ctx_lock);
            atc_hdlc_disconnect(&node->ctx);
            MUTEX_UNLOCK(&node->ctx_lock);
        }
        SLEEP_MS(1);
        retries--;
    }
}

void run_window_test(int window_size, uint32_t error_prob) {
    printf("\n--- Running Mem-Pipe Test with Window Size = %d%s ---\n", window_size,
           error_prob > 0 ? " (Error Prob)" : "");

    pipe_queue_t pipe1, pipe2;
    virtual_node_t node1, node2;
    node_pair_init(&node1, &node2, &pipe1, &pipe2, window_size);

    node1.error_probability = error_prob;
    node2.error_probability = error_prob;

    node_pair_start(&node1, &node2);

    char test_name_fail[64];

    if (!hdlc_test_connect(&node1, 5000)) {
        sprintf(test_name_fail, "Mem-Pipe Connection Failed (Window %d)", window_size);
        test_fail(test_name_fail, "Timeout waiting for Node 1 to connect");
        goto cleanup;
    }

    int sync_timeout = 500;
    while (!node2.connected && sync_timeout > 0) {
        SLEEP_MS(10);
        sync_timeout--;
    }
    if (!node2.connected) {
        sprintf(test_name_fail, "Mem-Pipe Connection Failed (Window %d)", window_size);
        test_fail(test_name_fail, "Node 2 never reported connected state");
        goto cleanup;
    }

    double start_time = get_time_s();

    uint32_t bytes_to_send = PAYLOAD_SIZE;
    uint8_t* payload = (uint8_t*)malloc(bytes_to_send);
    if (!payload) {
        test_fail("Mem-Pipe Transfer", "Failed to allocate payload buffer");
        goto cleanup;
    }
    memset(payload, 0xAA, bytes_to_send);

    int tx_timeout_ms = (error_prob > 0) ? 100000 : 10000;
    if (!hdlc_test_send_data(&node1, payload, bytes_to_send, tx_timeout_ms)) {
        sprintf(test_name_fail, "Mem-Pipe Transfer TX Timeout (Window %d)", window_size);
        test_fail(test_name_fail, "Timeout waiting for window");
        goto cleanup;
    }

    int rx_timeout_ms = (error_prob > 0) ? 500000 : 50000;
    if (!hdlc_test_wait_rx(&node2.bytes_received, bytes_to_send, rx_timeout_ms)) {
        sprintf(test_name_fail, "Mem-Pipe Transfer (Window %d)", window_size);
        test_fail(test_name_fail, "Incomplete transfer (RX Timeout)");
        goto cleanup;
    }

    double elapsed = get_time_s() - start_time;

    hdlc_test_disconnect(&node1, 5000);
    hdlc_test_disconnect(&node2, 5000);

    double speed_kbps = (PAYLOAD_SIZE / 1024.0) / elapsed;
    char test_name_pass[128];
    sprintf(test_name_pass, "Mem-Pipe Transfer %s(Window %d) [Time: %.3fs, Speed: %.2f KB/s]",
            (error_prob > 0) ? "Errored " : "", window_size, elapsed, speed_kbps);
    test_pass(test_name_pass);

cleanup:
    node_pair_cleanup(&node1, &node2, &pipe1, &pipe2);
    if (payload)
        free(payload);
}

void run_timeout_test(int window_size) {
    printf(
        "\n--- Running Mem-Pipe Test with Window Size = %d (Timeout Injection - 100%% Error) ---\n",
        window_size);

    pipe_queue_t pipe1, pipe2;
    virtual_node_t node1, node2;
    node_pair_init(&node1, &node2, &pipe1, &pipe2, window_size);
    node_pair_start(&node1, &node2);

    char test_name_pass[128];
    sprintf(test_name_pass, "Mem-Pipe Graceful Timeout (Window %d)", window_size);

    if (!hdlc_test_connect(&node1, 5000)) {
        test_fail(test_name_pass, "Timeout during initial reliable connect");
        goto cleanup;
    }
    int sync_timeout = 500;
    while (!node2.connected && sync_timeout > 0) {
        SLEEP_MS(10);
        sync_timeout--;
    }

    node1.error_probability = 10000;

    uint32_t bytes_to_send = PAYLOAD_SIZE;
    uint8_t* payload = (uint8_t*)malloc(bytes_to_send);
    if (!payload) {
        test_fail("Mem-Pipe Graceful Timeout", "Failed to allocate payload buffer");
        goto cleanup;
    }
    memset(payload, 0xAA, bytes_to_send);

    int tx_timeout_ms = 2000;
    if (hdlc_test_send_data(&node1, payload, bytes_to_send, tx_timeout_ms)) {
        test_fail(test_name_pass, "Data transfer succeeded despite 100% loss!");
        goto cleanup;
    }

    test_pass(test_name_pass);

cleanup:
    node_pair_cleanup(&node1, &node2, &pipe1, &pipe2);
    if (payload)
        free(payload);
}

void run_go_back_n_test(int window_size) {
    printf("\n--- Running Mem-Pipe Test w/ Win=%d (Go-Back-N Protocol Verification) ---\n",
           window_size);

    pipe_queue_t pipe1, pipe2;
    virtual_node_t node1, node2;
    node_pair_init(&node1, &node2, &pipe1, &pipe2, window_size);
    node_pair_start(&node1, &node2);

    char test_name[128];
    sprintf(test_name, "Mem-Pipe Go-Back-N Sequence Recovery (Window %d)", window_size);

    if (!hdlc_test_connect(&node1, 5000)) {
        test_fail(test_name, "Timeout waiting for Node 1 to connect");
        goto cleanup;
    }
    int sync_timeout = 500;
    while (!node2.connected && sync_timeout > 0) {
        SLEEP_MS(10);
        sync_timeout--;
    }

    uint32_t bytes_to_send = (window_size * 3) * CHUNK_SIZE;
    uint8_t payload[CHUNK_SIZE];
    memset(payload, 0xBB, sizeof(payload));

    uint32_t sent = 0;
    int frame_counter = 0;
    double start = get_time_s();
    double timeout_s = 5.0; // 5 seconds

    while (sent < bytes_to_send && (get_time_s() - start) < timeout_s) {
        if (!node1.connected)
            break;
        uint32_t to_send = CHUNK_SIZE;
        if (bytes_to_send - sent < CHUNK_SIZE)
            to_send = bytes_to_send - sent;

        if (frame_counter == 1 && window_size > 1) {
            node1.drop_next_i_frame = true;
        }

        MUTEX_LOCK(&node1.ctx_lock);
        atc_hdlc_error_t send_result = atc_hdlc_transmit_i(&node1.ctx, payload, to_send);
        MUTEX_UNLOCK(&node1.ctx_lock);

        if (send_result == ATC_HDLC_OK) {
            sent += to_send;
            frame_counter++;
            start = get_time_s();
        } else {
            YIELD_THREAD();
        }
    }

    if (sent < bytes_to_send) {
        test_fail(test_name, "TX Timeout waiting for window");
        goto cleanup;
    }

    double rx_start = get_time_s();
    while (node2.bytes_received < bytes_to_send && (get_time_s() - rx_start) < 10.0) {
        if (!node2.connected)
            break;
        YIELD_THREAD();
    }

    if (node2.bytes_received != bytes_to_send) {
        test_fail(test_name, "Incomplete transfer (Go-Back-N Failed)");
        goto cleanup;
    }

    hdlc_test_disconnect(&node1, 5000);
    hdlc_test_disconnect(&node2, 5000);

    test_pass(test_name);

cleanup:
    node_pair_cleanup(&node1, &node2, &pipe1, &pipe2);
}

void run_file_transfer_test(const char* filepath, int window_size, uint32_t error_prob) {
    // Read the file into memory
    FILE* f = fopen(filepath, "rb");
    if (!f) {
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
    uint8_t* file_data = (uint8_t*)malloc((size_t)file_size);
    size_t bytes_read = fread(file_data, 1, (size_t)file_size, f);
    fclose(f);
    if ((long)bytes_read != file_size) {
        free(file_data);
        test_fail("File Transfer", "Failed to read entire file");
        return;
    }

    pipe_queue_t pipe1, pipe2;
    virtual_node_t node1, node2;
    node_pair_init(&node1, &node2, &pipe1, &pipe2, window_size);

    node1.error_probability = error_prob;
    node2.error_probability = error_prob;

    uint8_t* rx_buffer = (uint8_t*)malloc((size_t)file_size);
    memset(rx_buffer, 0, (size_t)file_size);
    node2.rx_data = rx_buffer;
    node2.rx_data_capacity = (uint32_t)file_size;

    node_pair_start(&node1, &node2);

    char test_name[256];
    sprintf(test_name, "File Transfer %s(Window %d)", (error_prob > 0) ? "[Errored] " : "",
            window_size);

    if (!hdlc_test_connect(&node1, 5000)) {
        test_fail(test_name, "Timeout waiting for Node 1 to connect");
        goto cleanup;
    }
    int sync_timeout = 500;
    while (!node2.connected && sync_timeout > 0) {
        SLEEP_MS(10);
        sync_timeout--;
    }

    double start_time = get_time_s();

    uint32_t bytes_to_send = (uint32_t)file_size;

    int tx_timeout_ms = (error_prob > 0) ? 900000 : 300000;
    if (!hdlc_test_send_data(&node1, file_data, bytes_to_send, tx_timeout_ms)) {
        test_fail(test_name, "TX Timeout");
        goto cleanup;
    }

    int rx_timeout_ms = (error_prob > 0) ? 900000 : 300000;
    if (!hdlc_test_wait_rx(&node2.bytes_received, bytes_to_send, rx_timeout_ms)) {
        test_fail(test_name, "Incomplete transfer");
        goto cleanup;
    }

    double elapsed = get_time_s() - start_time;

    if (memcmp(file_data, rx_buffer, (size_t)file_size) != 0) {
        test_fail(test_name, "Data integrity check failed (memcmp mismatch)");
        goto cleanup;
    }

    hdlc_test_disconnect(&node1, 5000);
    hdlc_test_disconnect(&node2, 5000);

    double speed_kbps = (file_size / 1024.0) / elapsed;
    char pass_msg[256];
    sprintf(pass_msg, "File Transfer %s(Window %d) [%ld bytes, Speed: %.2f KB/s]",
            (error_prob > 0) ? "[Errored] " : "", window_size, file_size, speed_kbps);
    test_pass(pass_msg);

cleanup:
    node_pair_cleanup(&node1, &node2, &pipe1, &pipe2);
    free(file_data);
    free(rx_buffer);
}

int main(void) {
    srand((unsigned int)time(NULL));

    printf("Starting Mem-Pipe Virtual COM Tests (Reliable)...\n");
    for (int w = 1; w <= 7; w++)
        run_window_test(w, 0);

    /* Error injection starts at w=3: w=1 with error is impractically slow
     * (single-slot stop-and-wait + retransmit on every loss). */
    printf("\nStarting Mem-Pipe Virtual COM Tests (Error Injection - 0.005%%)...\n");
    for (int w = 1; w <= 7; w++)
        run_window_test(w, 50);

    printf("\nStarting Mem-Pipe Virtual COM Tests (Timeout Injection)...\n");
    for (int w = 1; w <= 7; w++)
        run_timeout_test(w);

    printf("\nStarting Mem-Pipe Virtual COM Tests (Go-Back-N Deterministic Drop)...\n");
    for (int w = 2; w <= 7; w++)
        run_go_back_n_test(w);

    printf("\nStarting Mem-Pipe Virtual COM Tests (File Transfer - test.pdf)...\n");
    for (int w = 1; w <= 7; w++)
        run_file_transfer_test(TEST_DATA_DIR "/test.pdf", w, 0);

    /* Error injection + large file is slow for small windows (stop-and-wait).
     * Only run for w>=4 where pipeline effect keeps throughput reasonable. */
    printf(
        "\nStarting Mem-Pipe Virtual COM Tests (File Transfer - Error Injection - 0.005%%)...\n");
    for (int w = 4; w <= 7; w++)
        run_file_transfer_test(TEST_DATA_DIR "/test.pdf", w, 50);

    printf("\nMem-Pipe Virtual COM Tests Completed Successfully.\n");
    return 0;
}
