/**
 * @file test_physical_target.c
 * @brief PC-side test suite for HDLC physical link verification.
 *
 * Two tests run per window size:
 *
 *   Test B — MCU-initiated I-frames: immediately after connection the MCU
 *             sends a known fixed-pattern burst (TEST_I_FRAME_COUNT frames);
 *             PC verifies frame count and payload byte-for-byte.
 *
 *   Test A — Bidirectional I-frame echo: PC sends a PDF file as I-frames;
 *             MCU echoes each frame back as an I-frame (not UI); PC verifies
 *             byte-for-byte integrity. TX and RTT throughput reported separately.
 *
 * Cross-platform: builds on both Linux and Windows.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../helpers/common.h"
#include "atc_hdlc/hdlc.h"

/* ================================================================
 *  Platform Abstraction
 * ================================================================ */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef HANDLE serial_handle_t;
#define SERIAL_INVALID INVALID_HANDLE_VALUE

typedef HANDLE thread_handle_t;
typedef CRITICAL_SECTION mutex_t;

#define mutex_init(m)    InitializeCriticalSection(m)
#define mutex_lock(m)    EnterCriticalSection(m)
#define mutex_unlock(m)  LeaveCriticalSection(m)
#define mutex_destroy(m) DeleteCriticalSection(m)

static void sleep_ms(unsigned ms) {
    Sleep(ms);
}
#define YIELD_THREAD() Sleep(0)

static double get_time_s(void) {
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart / (double)freq.QuadPart;
}

#else
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

typedef int serial_handle_t;
#define SERIAL_INVALID (-1)

typedef pthread_t thread_handle_t;
typedef pthread_mutex_t mutex_t;

#define mutex_init(m)    pthread_mutex_init(m, NULL)
#define mutex_lock(m)    pthread_mutex_lock(m)
#define mutex_unlock(m)  pthread_mutex_unlock(m)
#define mutex_destroy(m) pthread_mutex_destroy(m)

static void sleep_ms(unsigned ms) {
    usleep(ms * 1000u);
}
#define YIELD_THREAD()   sched_yield()

static double get_time_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
#endif

/* ================================================================
 *  Configuration
 * ================================================================ */
#ifdef _WIN32
#define DEFAULT_SERIAL_PORT "\\\\.\\COM4"
#else
#define DEFAULT_SERIAL_PORT "/dev/ttyUSB0"
#endif

#define DEFAULT_BAUD_RATE 921600
#define CHUNK_SIZE        512
#define BUFFER_SIZE       16384
#define PDF_PATH          TEST_DATA_DIR "/test.pdf"

/* Must match TEST_I_FRAME_COUNT / TEST_I_PAYLOAD_SIZE in MCU main.c */
#define MCU_TEST_FRAME_COUNT 5u
#define MCU_TEST_FRAME_SIZE  16u
#define MCU_TEST_TOTAL_BYTES (MCU_TEST_FRAME_COUNT * MCU_TEST_FRAME_SIZE)

/* ================================================================
 *  Physical Node Context
 * ================================================================ */
typedef struct {
    serial_handle_t port;
    mutex_t ctx_lock;
    atc_hdlc_ctx_t ctx;
    atc_hdlc_config_t cfg;
    atc_hdlc_plat_ops_t plat;
    atc_hdlc_txwin_t tw;
    atc_hdlc_rxbuf_t rx;
    atc_hdlc_u8 input_buffer[BUFFER_SIZE * 2];
    atc_hdlc_u8 retransmit_slots[7 * 1024];
    atc_hdlc_u32 retransmit_lens[7];
    thread_handle_t rx_thread;
    volatile bool running;

    /* Test B: frames sent by MCU after connection */
    uint8_t mcu_test_buf[MCU_TEST_TOTAL_BYTES];
    volatile uint32_t mcu_test_bytes;
    volatile uint32_t mcu_test_frames;
    volatile bool mcu_test_done; /* set by main thread to stop routing here */

    /* Test A: echo receive buffer */
    uint8_t* recv_buf;
    uint32_t recv_buf_len;
    volatile uint32_t bytes_received;
    volatile uint32_t frames_received;
} physical_node_t;

/* ================================================================
 *  Serial Port
 * ================================================================ */
#ifdef _WIN32

static serial_handle_t serial_open(const char* port, int baud) {
    HANDLE h = CreateFileA(port, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Error opening %s (err=%lu)\n", port, GetLastError());
        return SERIAL_INVALID;
    }
    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(h, &dcb);
    dcb.BaudRate = (DWORD)baud;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    dcb.fBinary = TRUE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    SetCommState(h, &dcb);
    COMMTIMEOUTS to = {MAXDWORD, 0, 1, 0, 1000};
    SetCommTimeouts(h, &to);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return h;
}
static int serial_read(serial_handle_t h, uint8_t* b, int n) {
    DWORD r = 0;
    ReadFile(h, b, n, &r, NULL);
    return (int)r;
}
static int serial_write(serial_handle_t h, const uint8_t* b, int n) {
    DWORD w = 0;
    WriteFile(h, b, n, &w, NULL);
    return (int)w;
}
static void serial_close(serial_handle_t h) {
    CloseHandle(h);
}

#else

static speed_t baud_to_speed(int baud) {
    switch (baud) {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    case 230400:
        return B230400;
    case 460800:
        return B460800;
    case 921600:
        return B921600;
    default:
        return (speed_t)-1;
    }
}

static serial_handle_t serial_open(const char* port, int baud) {
    int fd = open(port, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        printf("Error opening %s: %s\n", port, strerror(errno));
        return SERIAL_INVALID;
    }
    speed_t speed = baud_to_speed(baud);
    if (speed == (speed_t)-1) {
        printf("Unsupported baud rate: %d\n", baud);
        close(fd);
        return SERIAL_INVALID;
    }
    struct termios tty;
    tcgetattr(fd, &tty);
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);
    cfmakeraw(&tty);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CRTSCTS;
    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}
static int serial_read(serial_handle_t fd, uint8_t* b, int n) {
    return (int)read(fd, b, (size_t)n);
}
static int serial_write(serial_handle_t fd, const uint8_t* b, int n) {
    int t = 0;
    while (t < n) {
        int r = (int)write(fd, b + t, (size_t)(n - t));
        if (r > 0)
            t += r;
        else if (r < 0 && errno != EAGAIN && errno != EINTR)
            break;
    }
    return t;
}
static void serial_close(serial_handle_t fd) {
    close(fd);
}

#endif

/* ================================================================
 *  Thread Abstraction
 * ================================================================ */
#ifdef _WIN32
static DWORD WINAPI rx_thread_wrapper(LPVOID arg);
static thread_handle_t thread_create(physical_node_t* n) {
    return CreateThread(NULL, 0, rx_thread_wrapper, n, 0, NULL);
}
static void thread_join(thread_handle_t h) {
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
}
#else
static void* rx_thread_wrapper(void* arg);
static thread_handle_t thread_create(physical_node_t* n) {
    pthread_t t;
    pthread_create(&t, NULL, rx_thread_wrapper, n);
    return t;
}
static void thread_join(thread_handle_t t) {
    pthread_join(t, NULL);
}
#endif

/* ================================================================
 *  HDLC Callbacks
 * ================================================================ */
static uint8_t tx_buf[BUFFER_SIZE];
static atc_hdlc_u32 tx_idx = 0;

static int node_output_cb(atc_hdlc_u8 byte, bool flush, void* user_data) {
    physical_node_t* node = (physical_node_t*)user_data;
    if (tx_idx < sizeof(tx_buf))
        tx_buf[tx_idx++] = byte;
    if (flush || tx_idx >= sizeof(tx_buf) - 1) {
        if (tx_idx > 0) {
            serial_write(node->port, tx_buf, (int)tx_idx);
            tx_idx = 0;
        }
    }
    return 0;
}

/**
 * @brief Routes incoming frames: Test B frames go to mcu_test_buf until
 *        mcu_test_done is set; after that all frames go to recv_buf (Test A).
 */
static void node_on_data_cb(const atc_hdlc_u8* payload, atc_hdlc_u16 len, void* user_data) {
    physical_node_t* node = (physical_node_t*)user_data;
    node->frames_received++;

    if (!node->mcu_test_done) {
        uint32_t space = MCU_TEST_TOTAL_BYTES - node->mcu_test_bytes;
        uint32_t n = len < space ? len : space;
        memcpy(node->mcu_test_buf + node->mcu_test_bytes, payload, n);
        node->mcu_test_bytes += len;
        node->mcu_test_frames++;
        printf("\r[Test B] frame #%u  len=%-4u  total=%u/%u\n", node->mcu_test_frames, len, node->mcu_test_bytes,
               MCU_TEST_TOTAL_BYTES);
    } else {
        if (node->recv_buf) {
            uint32_t space = node->recv_buf_len - node->bytes_received;
            uint32_t n = len < space ? len : space;
            memcpy(node->recv_buf + node->bytes_received, payload, n);
        }
        node->bytes_received += len;
        printf("\r[Test A] frame #%u  len=%-4u  echo=%u\n", node->frames_received, len, node->bytes_received);
    }
    fflush(stdout);
}

static void node_event_cb(atc_hdlc_event_t event, void* user_data) {
    (void)user_data;
    if (event == ATC_HDLC_EVENT_CONN_ACCEPTED || event == ATC_HDLC_EVENT_CONN_REQ)
        printf("\nConnected.\n");
    else if (event == ATC_HDLC_EVENT_LINK_FAILURE || event == ATC_HDLC_EVENT_PEER_DISC)
        printf("\n[Error] Link dropped.\n");
}

/* ================================================================
 *  Timer State + RX Thread
 * ================================================================ */
static volatile double t1_at = 0, t2_at = 0;
static volatile int t1_pending = 0, t2_pending = 0;

static void t1_start(atc_hdlc_u32 ms, void* u) {
    (void)ms;
    (void)u;
    t1_at = get_time_s();
    t1_pending = 1;
}
static void t1_stop(void* u) {
    (void)u;
    t1_pending = 0;
}
static void t2_start(atc_hdlc_u32 ms, void* u) {
    (void)ms;
    (void)u;
    t2_at = get_time_s();
    t2_pending = 1;
}
static void t2_stop(void* u) {
    (void)u;
    t2_pending = 0;
}

static void rx_thread_body(physical_node_t* node) {
    uint8_t buf[8192];
    while (node->running) {
        int n = serial_read(node->port, buf, sizeof(buf));
        mutex_lock(&node->ctx_lock);
        if (n > 0)
            atc_hdlc_data_in(&node->ctx, buf, (atc_hdlc_u32)n);
        if (t2_pending) {
            double now = get_time_s();
            if ((now - t2_at) * 1000.0 >= (double)node->ctx.config->t2_ms) {
                t2_pending = 0;
                atc_hdlc_t2_expired(&node->ctx);
            }
        }
        if (t1_pending) {
            double now = get_time_s();
            if ((now - t1_at) * 1000.0 >= (double)node->ctx.config->t1_ms) {
                t1_pending = 0;
                atc_hdlc_t1_expired(&node->ctx);
            }
        }
        mutex_unlock(&node->ctx_lock);
        if (n <= 0)
            YIELD_THREAD();
    }
}

#ifdef _WIN32
static DWORD WINAPI rx_thread_wrapper(LPVOID arg) {
    rx_thread_body((physical_node_t*)arg);
    return 0;
}
#else
static void* rx_thread_wrapper(void* arg) {
    rx_thread_body((physical_node_t*)arg);
    return NULL;
}
#endif

/* ================================================================
 *  Helpers
 * ================================================================ */

static uint8_t* load_file(const char* path, uint32_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        printf("Cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        return NULL;
    }
    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = (uint32_t)sz;
    return buf;
}

static bool wait_for_connection(physical_node_t* node, int timeout_ms) {
    mutex_lock(&node->ctx_lock);
    atc_hdlc_link_setup(&node->ctx, node->ctx.peer_address);
    mutex_unlock(&node->ctx_lock);
    while (node->ctx.current_state != ATC_HDLC_STATE_CONNECTED && timeout_ms-- > 0)
        sleep_ms(1);
    return node->ctx.current_state == ATC_HDLC_STATE_CONNECTED;
}

/** @brief Test B: wait for MCU-initiated I-frames then verify payload. */
static bool run_test_b(physical_node_t* node) {
    static const uint8_t expected[MCU_TEST_FRAME_SIZE] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    };

    printf("Waiting for MCU self-test frames (%u bytes)...\n", MCU_TEST_TOTAL_BYTES);
    int timeout_ms = 5000;
    while (node->mcu_test_bytes < MCU_TEST_TOTAL_BYTES && timeout_ms > 0 && node->running) {
        sleep_ms(10);
        timeout_ms -= 10;
    }

    printf("\n--- Test B ---\n");
    printf("Expected: %u bytes (%u x %u)\n", MCU_TEST_TOTAL_BYTES, MCU_TEST_FRAME_COUNT, MCU_TEST_FRAME_SIZE);
    printf("Received: %u bytes (%u frames)\n", node->mcu_test_bytes, node->mcu_test_frames);

    if (node->mcu_test_bytes < MCU_TEST_TOTAL_BYTES) {
        printf("%s[FAIL] Test B: timeout%s\n", COL_RED, COL_RESET);
        return false;
    }

    for (uint32_t i = 0; i < MCU_TEST_FRAME_COUNT; i++) {
        if (memcmp(node->mcu_test_buf + i * MCU_TEST_FRAME_SIZE, expected, MCU_TEST_FRAME_SIZE) != 0) {
            printf("%s[FAIL] Test B: payload mismatch in frame %u%s\n", COL_RED, i, COL_RESET);
            return false;
        }
    }
    printf("%s[PASS] Test B: %u MCU-initiated I-frames verified%s\n", COL_GREEN, MCU_TEST_FRAME_COUNT, COL_RESET);
    return true;
}

static uint32_t send_data(physical_node_t* node, const uint8_t* data, uint32_t len) {
    uint32_t sent = 0;
    while (sent < len && node->running) {
        uint32_t chunk = (len - sent < CHUNK_SIZE) ? (len - sent) : CHUNK_SIZE;
        bool ok = false;
        long spin = 0;
        while (!ok && node->running) {
            mutex_lock(&node->ctx_lock);
            if (node->ctx.current_state == ATC_HDLC_STATE_CONNECTED)
                ok = (atc_hdlc_transmit_i(&node->ctx, data + sent, chunk) == ATC_HDLC_OK);
            else if (++spin % 1000 == 0) {
                printf("  [Error] Disconnected.\n");
                node->running = false;
            }
            mutex_unlock(&node->ctx_lock);
            if (!ok && node->running)
                YIELD_THREAD();
        }
        sent += chunk;
        if (sent % (CHUNK_SIZE * 20) == 0) {
            printf("\rSent %u  Echoed %u...   ", sent, node->bytes_received);
            fflush(stdout);
        }
        YIELD_THREAD();
    }
    return sent;
}

static void wait_for_echoes(physical_node_t* node, uint32_t expected, int timeout_ms) {
    while (node->bytes_received < expected && timeout_ms > 0 && node->running) {
        sleep_ms(10);
        timeout_ms -= 10;
    }
    if (timeout_ms <= 0 && node->bytes_received < expected)
        printf("\n[Warning] Echo timeout: received=%u expected=%u\n", node->bytes_received, expected);
}

/** @brief Test A: verify byte-for-byte integrity and report TX / RTT throughput. */
static bool run_test_a(physical_node_t* node, const uint8_t* original, uint32_t data_len, uint32_t sent, double tx_s,
                       double rtt_s, double* out_tx_kbps, double* out_rtt_kbps) {
    uint32_t frames = (data_len + CHUNK_SIZE - 1) / CHUNK_SIZE;
    uint32_t overhead = frames * 6; /* flag+addr+ctrl+2×FCS */
    double tx_kbps = ((data_len + overhead) * 8.0) / (tx_s * 1000.0);
    double rtt_kbps = ((data_len + overhead) * 2.0 * 8.0) / (rtt_s * 1000.0);
    if (out_tx_kbps)
        *out_tx_kbps = tx_kbps;
    if (out_rtt_kbps)
        *out_rtt_kbps = rtt_kbps;

    printf("\n--- Test A (Window %u) ---\n", node->ctx.tx_window->slot_count);
    printf("Sent     : %u bytes  (%u frames)\n", sent, frames);
    printf("Received : %u bytes  (%u frames)\n", node->bytes_received, node->frames_received);
    printf("TX       : %.3f s  %.2f kbps\n", tx_s, tx_kbps);
    printf("RTT      : %.3f s  %.2f kbps\n", rtt_s, rtt_kbps);

    if (node->bytes_received != data_len) {
        printf("%s[FAIL] Test A Window %u: size mismatch (sent=%u received=%u)%s\n", COL_RED,
               node->ctx.tx_window->slot_count, data_len, node->bytes_received, COL_RESET);
        return false;
    }
    if (memcmp(original, node->recv_buf, data_len) != 0) {
        uint32_t pos = 0;
        for (uint32_t i = 0; i < data_len; i++) {
            if (original[i] != node->recv_buf[i]) {
                pos = i;
                break;
            }
        }
        printf("%s[FAIL] Test A Window %u: mismatch at byte %u (0x%02X vs 0x%02X)%s\n", COL_RED,
               node->ctx.tx_window->slot_count, pos, original[pos], node->recv_buf[pos], COL_RESET);
        return false;
    }
    printf("%s[PASS] Test A Window %u: byte-for-byte match%s\n", COL_GREEN, node->ctx.tx_window->slot_count, COL_RESET);
    return true;
}

/* ================================================================
 *  Node Init / Cleanup
 * ================================================================ */
static bool node_init(physical_node_t* node, uint32_t recv_len, uint8_t window_size, const char* serial_port,
                      int baud_rate) {
    memset(node, 0, sizeof(*node));

    node->recv_buf = (uint8_t*)malloc(recv_len);
    if (!node->recv_buf)
        return false;
    node->recv_buf_len = recv_len;

    node->port = serial_open(serial_port, baud_rate);
    if (node->port == SERIAL_INVALID) {
        free(node->recv_buf);
        return false;
    }

    mutex_init(&node->ctx_lock);
    node->running = true;

    node->cfg.mode = ATC_HDLC_MODE_ABM;
    node->cfg.address = 0x01;
    node->cfg.max_info_size = 1024;
    node->cfg.max_retries = 10;
    node->cfg.t1_ms = ATC_HDLC_DEFAULT_T1_TIMEOUT;
    node->cfg.t2_ms = 1;

    node->plat.on_send = node_output_cb;
    node->plat.on_data = node_on_data_cb;
    node->plat.on_event = node_event_cb;
    node->plat.user_ctx = node;
    node->plat.t1_start = t1_start;
    node->plat.t1_stop = t1_stop;
    node->plat.t2_start = t2_start;
    node->plat.t2_stop = t2_stop;

    node->tw.slots = node->retransmit_slots;
    node->tw.slot_lens = node->retransmit_lens;
    node->tw.slot_capacity = 1024;
    node->tw.slot_count = window_size;

    node->rx.buffer = node->input_buffer;
    node->rx.capacity = sizeof(node->input_buffer);

    atc_hdlc_params_t p = {.config = &node->cfg, .platform = &node->plat, .tx_window = &node->tw, .rx_buf = &node->rx};
    atc_hdlc_init(&node->ctx, p);
    node->ctx.peer_address = 0x02;

    node->rx_thread = thread_create(node);
    return true;
}

static void node_cleanup(physical_node_t* node) {
    node->running = false;
    thread_join(node->rx_thread);
    mutex_destroy(&node->ctx_lock);
    serial_close(node->port);
    free(node->recv_buf);
}

/* ================================================================
 *  Main
 * ================================================================ */
int main(int argc, char* argv[]) {
    uint8_t w_min = 1, w_max = 7;
    const char* serial_port = DEFAULT_SERIAL_PORT;
    int baud_rate = DEFAULT_BAUD_RATE;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            serial_port = argv[++i];
        } else if (strcmp(argv[i], "--baud") == 0 && i + 1 < argc) {
            baud_rate = atoi(argv[++i]);
        } else {
            int w = 0;
            if (sscanf(argv[i], "w%d", &w) != 1 || w < 1 || w > 7) {
                fprintf(stderr, "Usage: %s [--port <dev>] [--baud <rate>] [w<1-7>]\n", argv[0]);
                return 1;
            }
            w_min = w_max = (uint8_t)w;
        }
    }

    printf("Physical target test  port=%s  baud=%d\n", serial_port, baud_rate);

    uint32_t pdf_size = 0;
    uint8_t* pdf_data = load_file(PDF_PATH, &pdf_size);
    if (!pdf_data || pdf_size == 0) {
        test_fail("load PDF", "Cannot open test PDF.");
        return 1;
    }
    printf("Loaded %s (%u bytes)\n\n", PDF_PATH, pdf_size);

    struct {
        uint8_t w;
        bool a, b;
        double tx_s, rtt_s, tx_kbps, rtt_kbps;
    } results[7];

    for (uint8_t w = w_min; w <= w_max; w++) {
        printf("==============================\n");
        printf(" Window size = %u\n", w);
        printf("==============================\n");

        physical_node_t node;
        if (!node_init(&node, pdf_size, w, serial_port, baud_rate)) {
            printf("[FAIL] node_init failed for window %u\n", w);
            results[w - 1] = (typeof(results[0])){w, false, false, 0, 0, 0, 0};
            sleep_ms(500);
            continue;
        }

        printf("Connecting...\n");
        if (!wait_for_connection(&node, 10000)) {
            printf("[FAIL] Connection timeout for window %u\n", w);
            results[w - 1] = (typeof(results[0])){w, false, false, 0, 0, 0, 0};
            node_cleanup(&node);
            sleep_ms(500);
            continue;
        }

        /* Test B: MCU-initiated I-frames */
        bool b = run_test_b(&node);

        /* Switch routing to Test A */
        node.mcu_test_done = true;
        node.bytes_received = 0;
        node.frames_received = 0; /* reset so Test A frame count is isolated */

        /* Test A: bidirectional I-frame echo */
        printf("\nSending %u bytes as I-frames (Test A)...\n", pdf_size);
        double t0 = get_time_s();
        uint32_t sent = send_data(&node, pdf_data, pdf_size);
        double tx_s = get_time_s() - t0;

        if (node.running) {
            printf("\nWaiting for echoes...\n");
            wait_for_echoes(&node, pdf_size, 10000);
        }
        double rtt_s = get_time_s() - t0;

        double tx_kbps = 0, rtt_kbps = 0;
        bool a = run_test_a(&node, pdf_data, pdf_size, sent, tx_s, rtt_s, &tx_kbps, &rtt_kbps);

        results[w - 1] = (typeof(results[0])){w, a, b, tx_s, rtt_s, tx_kbps, rtt_kbps};
        node_cleanup(&node);
        sleep_ms(500);
    }

    free(pdf_data);

    printf("\n============================================================================\n");
    printf(" SUMMARY (%d baud)\n", baud_rate);
    printf("============================================================================\n");
    printf(" | Win | Test A  | Test B  | TX (s) | RTT (s) | TX kbps  | RTT kbps |\n");
    printf(" |-----|---------|---------|--------|---------|----------|----------|\n");
    for (int i = 0; i < 7; i++) {
        printf(" |  %2u |  %s  |  %s  | %6.2f | %7.2f | %8.2f | %8.2f |\n", results[i].w,
               results[i].a ? " PASS" : " FAIL", results[i].b ? " PASS" : " FAIL", results[i].tx_s, results[i].rtt_s,
               results[i].tx_kbps, results[i].rtt_kbps);
    }
    printf("============================================================================\n\n");

    int fails = 0;
    for (int i = 0; i < 7; i++)
        if (!results[i].a || !results[i].b)
            fails++;

    if (fails) {
        printf("[ERROR] %d/7 tests FAILED.\n", fails);
        return 1;
    }
    printf("[SUCCESS] All 7 tests PASSED.\n");
    return 0;
}
