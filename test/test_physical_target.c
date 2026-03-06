/**
 * @file test_physical_target.c
 * @brief PC-side test: sends a PDF file to an STM32 target via serial (I-frames)
 *        and verifies that every byte is echoed back (as UI frames).
 *
 * Cross-platform: builds on both Linux and Windows.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "../inc/hdlc.h"
#include "test_common.h"

/* ================================================================
 *  Platform Abstraction
 * ================================================================ */
#ifdef _WIN32
  /* ---- Windows ---- */
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>

  typedef HANDLE            serial_handle_t;
  #define SERIAL_INVALID    INVALID_HANDLE_VALUE

  typedef HANDLE            thread_handle_t;
  typedef CRITICAL_SECTION  mutex_t;

  #define mutex_init(m)     InitializeCriticalSection(m)
  #define mutex_lock(m)     EnterCriticalSection(m)
  #define mutex_unlock(m)   LeaveCriticalSection(m)
  #define mutex_destroy(m)  DeleteCriticalSection(m)

  static void sleep_us(unsigned us) { Sleep(us / 1000 > 0 ? us / 1000 : 1); }
  static void sleep_ms(unsigned ms) { Sleep(ms); }
  
  #define YIELD_THREAD() Sleep(0)

  static double get_time_s(void)
  {
      LARGE_INTEGER freq, cnt;
      QueryPerformanceFrequency(&freq);
      QueryPerformanceCounter(&cnt);
      return (double)cnt.QuadPart / (double)freq.QuadPart;
  }

#else
  /* ---- POSIX (Linux / macOS) ---- */
  #include <fcntl.h>
  #include <unistd.h>
  #include <termios.h>
  #include <pthread.h>
  #include <time.h>
  #include <sched.h>

  typedef int               serial_handle_t;
  #define SERIAL_INVALID    (-1)

  typedef pthread_t         thread_handle_t;
  typedef pthread_mutex_t   mutex_t;

  #define mutex_init(m)     pthread_mutex_init(m, NULL)
  #define mutex_lock(m)     pthread_mutex_lock(m)
  #define mutex_unlock(m)   pthread_mutex_unlock(m)
  #define mutex_destroy(m)  pthread_mutex_destroy(m)

  static void sleep_us(unsigned us) { usleep(us); }
  static void sleep_ms(unsigned ms) { usleep(ms * 1000u); }
  
  #define YIELD_THREAD() sched_yield()

  static double get_time_s(void)
  {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return ts.tv_sec + (double)ts.tv_nsec / 1e9;
  }
#endif

/* ================================================================
 *  Configuration
 * ================================================================ */
#ifdef _WIN32
  #define SERIAL_PORT   "\\\\.\\COM4"
#else
  #define SERIAL_PORT   "/dev/ttyUSB0"
#endif

#define BAUD_RATE     921600
#define CHUNK_SIZE    512
#define BUFFER_SIZE   16384
#define PDF_PATH      TEST_DATA_DIR "/test.pdf"

/* ================================================================
 *  Physical Node Context
 * ================================================================ */
typedef struct {
    serial_handle_t port;
    mutex_t         ctx_lock;
    atc_hdlc_context_t  ctx;
    atc_hdlc_u8         input_buffer[BUFFER_SIZE * 2];
    atc_hdlc_u8         retransmit_buffer[BUFFER_SIZE * 2 * 8];
    thread_handle_t rx_thread;
    volatile bool   running;

    /* Receive buffer for integrity check */
    uint8_t        *recv_buffer;
    uint32_t        recv_buffer_len;
    volatile uint32_t bytes_received;
    volatile uint32_t frames_received;
} physical_node_t;

/* ================================================================
 *  Serial Port — Open / Read / Write / Close
 * ================================================================ */
#ifdef _WIN32

static serial_handle_t serial_open(const char *port_name)
{
    HANDLE h = CreateFileA(port_name, GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Error opening serial port %s (err=%lu)\n", port_name, GetLastError());
        return SERIAL_INVALID;
    }

    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        printf("Error GetCommState (err=%lu)\n", GetLastError());
        CloseHandle(h);
        return SERIAL_INVALID;
    }

    dcb.BaudRate = BAUD_RATE;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity   = NOPARITY;
    dcb.fBinary  = TRUE;
    dcb.fParity  = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl  = DTR_CONTROL_ENABLE;
    dcb.fRtsControl  = RTS_CONTROL_ENABLE;
    dcb.fOutX = FALSE;
    dcb.fInX  = FALSE;

    if (!SetCommState(h, &dcb)) {
        printf("Error SetCommState (err=%lu)\n", GetLastError());
        CloseHandle(h);
        return SERIAL_INVALID;
    }

    COMMTIMEOUTS timeouts;
    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.ReadIntervalTimeout         = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    timeouts.ReadTotalTimeoutConstant    = 1;   /* 1 ms read timeout */
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant   = 1000;
    SetCommTimeouts(h, &timeouts);

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return h;
}

static int serial_read(serial_handle_t h, uint8_t *buf, int max_len)
{
    DWORD bytes_read = 0;
    if (!ReadFile(h, buf, (DWORD)max_len, &bytes_read, NULL))
        return -1;
    return (int)bytes_read;
}

static int serial_write(serial_handle_t h, const uint8_t *buf, int len)
{
    DWORD written = 0;
    if (!WriteFile(h, buf, (DWORD)len, &written, NULL))
        return -1;
    return (int)written;
}

static void serial_close(serial_handle_t h)
{
    CloseHandle(h);
}

#else /* POSIX */

static serial_handle_t serial_open(const char *port_name)
{
    int fd = open(port_name, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        printf("Error opening serial port %s: %s\n", port_name, strerror(errno));
        return SERIAL_INVALID;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        printf("Error from tcgetattr: %s\n", strerror(errno));
        close(fd);
        return SERIAL_INVALID;
    }

    cfsetospeed(&tty, B921600);
    cfsetispeed(&tty, B921600);
    cfmakeraw(&tty);

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        close(fd);
        return SERIAL_INVALID;
    }

    return fd;
}

static int serial_read(serial_handle_t fd, uint8_t *buf, int max_len)
{
    return (int)read(fd, buf, (size_t)max_len);
}

static int serial_write(serial_handle_t fd, const uint8_t *buf, int len)
{
    int total = 0;
    while (total < len) {
        int res = (int)write(fd, buf + total, (size_t)(len - total));
        if (res > 0)       total += res;
        else if (res < 0 && errno != EAGAIN && errno != EINTR) break;
    }
    return total;
}

static void serial_close(serial_handle_t fd)
{
    close(fd);
}

#endif /* _WIN32 */

/* ================================================================
 *  Thread Abstraction
 * ================================================================ */
#ifdef _WIN32

static DWORD WINAPI rx_thread_wrapper(LPVOID arg);

static thread_handle_t thread_create(physical_node_t *node)
{
    return CreateThread(NULL, 0, rx_thread_wrapper, node, 0, NULL);
}

static void thread_join(thread_handle_t h)
{
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
}

#else /* POSIX */

static void *rx_thread_wrapper(void *arg);

static thread_handle_t thread_create(physical_node_t *node)
{
    pthread_t t;
    pthread_create(&t, NULL, rx_thread_wrapper, node);
    return t;
}

static void thread_join(thread_handle_t t)
{
    pthread_join(t, NULL);
}

#endif /* _WIN32 */

/* ================================================================
 *  HDLC Callbacks
 * ================================================================ */
static uint8_t  tx_buffer[BUFFER_SIZE];
static atc_hdlc_u32 tx_index = 0;

/** @brief Output callback — buffers bytes and writes to serial on flush. */
static void node_output_cb(atc_hdlc_u8 byte, atc_hdlc_bool flush, void *user_data)
{
    physical_node_t *node = (physical_node_t *)user_data;

    if (tx_index < sizeof(tx_buffer))
        tx_buffer[tx_index++] = byte;

    if (flush || tx_index >= sizeof(tx_buffer) - 1) {
        if (tx_index > 0) {
            serial_write(node->port, tx_buffer, (int)tx_index);
            tx_index = 0;
        }
    }
}

/** @brief Frame callback — accumulates echoed UI frames for integrity check. */
static void node_on_frame_cb(const atc_hdlc_frame_t *frame, void *user_data)
{
    physical_node_t *node = (physical_node_t *)user_data;
    node->frames_received++;

    if (frame->type == ATC_HDLC_FRAME_U &&
        atc_hdlc_get_u_frame_sub_type(&frame->control) == ATC_HDLC_U_FRAME_TYPE_UI) {
        /* Copy payload into verification buffer */
        if (node->recv_buffer && frame->information_len > 0) {
            uint32_t space = node->recv_buffer_len - node->bytes_received;
            uint32_t copy_len = frame->information_len;
            if (copy_len > space) copy_len = space;
            memcpy(node->recv_buffer + node->bytes_received,
                   frame->information, copy_len);
        }
        node->bytes_received += frame->information_len;
        printf("\rReceived UI frame #%u (len=%u)         \n",
               node->frames_received, frame->information_len);
        fflush(stdout);
    } else {
        printf("\nReceived non-UI frame: type=%d, len=%u, ctrl=%02X\n",
               frame->type, frame->information_len, frame->control.value);
    }
}

/** @brief Connection state change callback. */
static void node_state_cb(atc_hdlc_protocol_state_t state, atc_hdlc_event_t event, void *user_data)
{
    (void)user_data;
    (void)event;
    if (state == ATC_HDLC_PROTOCOL_STATE_CONNECTED)
        printf("\nLogical connection established!\n");
    else if (state == ATC_HDLC_PROTOCOL_STATE_DISCONNECTED)
        printf("\n[Error] Logical connection dropped!\n");
}

/* ================================================================
 *  RX Thread — reads serial + drives atc_hdlc_tick
 * ================================================================ */
static void rx_thread_body(physical_node_t *node)
{
    uint8_t buf[8192];
    double last = get_time_s();

    while (node->running) {
        int n = serial_read(node->port, buf, sizeof(buf));

        double now = get_time_s();
        long elapsed_ms = (long)((now - last) * 1000.0);

        mutex_lock(&node->ctx_lock);

        if (n > 0) {
            atc_hdlc_input_bytes(&node->ctx, buf, (atc_hdlc_u32)n);
        }

        if (elapsed_ms >= 2) {
            for (long t = 0; t < elapsed_ms; t++)
                atc_hdlc_tick(&node->ctx);
            last = now;
        }

        mutex_unlock(&node->ctx_lock);

        if (n <= 0)
            YIELD_THREAD();
    }
}

#ifdef _WIN32
static DWORD WINAPI rx_thread_wrapper(LPVOID arg)
{
    rx_thread_body((physical_node_t *)arg);
    return 0;
}
#else
static void *rx_thread_wrapper(void *arg)
{
    rx_thread_body((physical_node_t *)arg);
    return NULL;
}
#endif

/* ================================================================
 *  Helpers
 * ================================================================ */

/** @brief Load a file into a malloc'd buffer. Returns NULL on failure. */
static uint8_t *load_file(const char *path, uint32_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("Error: Cannot open file '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return NULL; }

    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (!buf) { fclose(f); return NULL; }

    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = (uint32_t)size;
    return buf;
}

/** @brief Connect to the target (SABM handshake) with timeout. */
static bool wait_for_connection(physical_node_t *node, int timeout_ms)
{
    mutex_lock(&node->ctx_lock);
    atc_hdlc_link_setup(&node->ctx);
    mutex_unlock(&node->ctx_lock);

    int retries = timeout_ms;
    while (node->ctx.current_state != ATC_HDLC_PROTOCOL_STATE_CONNECTED && retries > 0) {
        sleep_ms(1);
        if (retries % 1000 == 0) {
            mutex_lock(&node->ctx_lock);
            for (int t = 0; t < 1000; t++) atc_hdlc_tick(&node->ctx);
            mutex_unlock(&node->ctx_lock);
        }
        retries--;
    }
    return node->ctx.current_state == ATC_HDLC_PROTOCOL_STATE_CONNECTED;
}

/** @brief Send the entire payload as I-frames in CHUNK_SIZE pieces. */
static uint32_t send_data(physical_node_t *node,
                          const uint8_t *data, uint32_t data_len)
{
    uint32_t sent = 0;

    while (sent < data_len && node->running) {
        uint32_t chunk = CHUNK_SIZE;
        if (data_len - sent < CHUNK_SIZE) chunk = data_len - sent;

        bool sent_ok = false;
        long stuck_count = 0;

        /* Retry until the I-frame is accepted (window open) */
        while (!sent_ok && node->running) {
            mutex_lock(&node->ctx_lock);
            if (node->ctx.current_state == ATC_HDLC_PROTOCOL_STATE_CONNECTED) {
                sent_ok = atc_hdlc_output_frame_i(&node->ctx, data + sent, chunk);
            } else {
                stuck_count++;
                if (stuck_count % 1000 == 0) {
                    printf("  [Error] Disconnected while sending.\n");
                    node->running = false;
                }
            }
            mutex_unlock(&node->ctx_lock);

            if (!sent_ok && node->running) {
                stuck_count++;
                if (stuck_count % 5000000 == 0) {
                    printf("\r  [Wait] TX Window full. V(S)=%u, V(R)=%u, V(A)=%u   ",
                           node->ctx.vs, node->ctx.vr, node->ctx.va);
                }
                YIELD_THREAD();
            }
        }

        sent += chunk;

        if (sent % (CHUNK_SIZE * 20) == 0) {
            printf("\rSent %u bytes, Echoed %u bytes...   ", sent, node->bytes_received);
            fflush(stdout);
        }
        YIELD_THREAD();
    }
    return sent;
}

/** @brief Wait for echo replies up to timeout. */
static void wait_for_echoes(physical_node_t *node, uint32_t expected, int timeout_ms)
{
    while (node->bytes_received < expected && timeout_ms > 0 && node->running) {
        sleep_ms(10);
        timeout_ms -= 10; /* Fixed from 100 to 10 */
    }
    
    /* Give it an extra moment to finish printing/processing the absolute last frames */
    if (node->bytes_received == expected) {
        sleep_ms(100);
    }

    if (timeout_ms <= 0 && node->bytes_received < expected) {
        printf("\n[Warning] Timeout waiting for final echoes.\n");
        printf("  -> Stats: RX Frames parsed=%u, CRC Errors=%u\n",
               node->ctx.stats_input_frames, node->ctx.stats_crc_errors);
    }
}

/** @brief Print test results and verify data integrity. Returns true on pass. */
static bool verify_results(physical_node_t *node,
                           const uint8_t *original, uint32_t data_len,
                           uint32_t sent_bytes, double duration, double *out_kbps)
{
    /* Calculate physical throughput with HDLC overhead.
     * For each chunk, HDLC adds:
     * - 2 bytes Flags (0x7E)
     * - 1 byte Address
     * - 1 byte Control
     * - 2 bytes FCS (CRC16)
     * = 6 bytes of basic overhead per frame.
     * *Note: Does not perfectly account for byte-stuffing which varies by payload. */
    uint32_t num_frames_sent = (data_len + CHUNK_SIZE - 1) / CHUNK_SIZE;
    uint32_t total_overhead = num_frames_sent * 6; /* 6 bytes generic framing overhead per frame */
    
    /* Since the target echoes the data back, the total bytes physically moving across the wire
       is (Payload + Overhead) sent * 2 (Tx and Rx). We only calculate the ONE-WAY throughput. */
    double physical_bytes_sent = data_len + total_overhead;
    double kbps = (physical_bytes_sent * 8) / (duration * 1000.0);
    if (out_kbps) *out_kbps = kbps;

    printf("\n\n--- Test Results (Window %u) ---\n", node->ctx.window_size);
    printf("Total Sent    : %u bytes\n", sent_bytes);
    printf("Total Received: %u bytes\n", node->bytes_received);
    printf("Frames Rcvd   : %u\n",       node->frames_received);
    printf("Time Taken    : %.2f seconds\n", duration);
    printf("Throughput    : %.2f kbps\n", kbps);

    if (node->bytes_received == data_len) {
        bool match = (memcmp(original, node->recv_buffer, data_len) == 0);
        if (match) {
            printf("%s[PASS] Window %u: Perfect Match!%s\n", COL_GREEN, node->ctx.window_size, COL_RESET);
            return true;
        } else {
            uint32_t pos = 0;
            for (uint32_t i = 0; i < data_len; i++) {
                if (original[i] != node->recv_buffer[i]) { pos = i; break; }
            }
            printf("%s[FAIL] Window %u: Data mismatch at byte %u: sent=0x%02X, recv=0x%02X%s\n", 
                   COL_RED, node->ctx.window_size, pos, original[pos], node->recv_buffer[pos], COL_RESET);
            return false;
        }
    } else if (node->bytes_received > 0) {
        printf("%s[FAIL] Window %u: Size mismatch: sent=%u, received=%u%s\n", 
               COL_RED, node->ctx.window_size, data_len, node->bytes_received, COL_RESET);
        return false;
    } else {
        printf("%s[FAIL] Window %u: No echo data received%s\n", COL_RED, node->ctx.window_size, COL_RESET);
        return false;
    }
}

/* ================================================================
 *  Node Init / Cleanup
 * ================================================================ */

/** @brief Initialize node: open serial, init HDLC, start RX thread. */
static bool node_init(physical_node_t *node, uint32_t recv_len, uint8_t window_size)
{
    memset(node, 0, sizeof(*node));

    node->recv_buffer = (uint8_t *)malloc(recv_len);
    if (!node->recv_buffer) return false;
    node->recv_buffer_len = recv_len;
    memset(node->recv_buffer, 0, recv_len);

    node->port = serial_open(SERIAL_PORT);
    if (node->port == SERIAL_INVALID) {
        free(node->recv_buffer);
        return false;
    }

    mutex_init(&node->ctx_lock);
    node->running = true;

    atc_hdlc_init(&node->ctx,
              node->input_buffer, sizeof(node->input_buffer),
              node->retransmit_buffer, sizeof(node->retransmit_buffer),
              ATC_HDLC_DEFAULT_RETRANSMIT_TIMEOUT, ATC_HDLC_DEFAULT_ACK_DELAY_TIMEOUT,
              window_size, 10,  /* dynamic window size, retries=10 */
              node_output_cb, node_on_frame_cb, node_state_cb, node);
    atc_hdlc_configure_station(&node->ctx, ATC_HDLC_ROLE_COMBINED, ATC_HDLC_MODE_ABM, 0x01, 0x02);

    node->rx_thread = thread_create(node);
    return true;
}

/** @brief Stop RX thread, close serial, free resources. */
static void node_cleanup(physical_node_t *node)
{
    node->running = false;
    thread_join(node->rx_thread);
    mutex_destroy(&node->ctx_lock);
    serial_close(node->port);
    free(node->recv_buffer);
}

/* ================================================================
 *  Main
 * ================================================================ */
int main(void)
{
    printf("Starting Physical Target Test on %s @ %d baud\n", SERIAL_PORT, BAUD_RATE);

    /* 1. Load test data */
    uint32_t pdf_size = 0;
    uint8_t *pdf_data = load_file(PDF_PATH, &pdf_size);
    if (!pdf_data || pdf_size == 0) {
        test_fail("Physical Target PDF Load", "Cannot load test PDF file.");
        return 1;
    }
    printf("Loaded %s (%u bytes)\n", PDF_PATH, pdf_size);

    struct {
        uint8_t window;
        bool pass;
        double time_s;
        double kbps;
    } results[7];

    /* Test all window sizes 1 through 7 */
    for (uint8_t w = 1; w <= 7; w++) {
        printf("\n======================================================\n");
        printf(" TESTING WINDOW SIZE = %u\n", w);
        printf("======================================================\n");

        physical_node_t node;
        if (!node_init(&node, pdf_size, w)) {
            printf("[FAIL] Cannot initialize serial / buffers for window %u\n", w);
            results[w-1].window = w;
            results[w-1].pass = false;
            results[w-1].time_s = 0.0;
            results[w-1].kbps = 0.0;
            
            // Note: Cannot node_cleanup because node_init failed meaning resources weren't allocated
            sleep_ms(500);
            continue;
        }

        printf("Connecting to target...\n");
        if (!wait_for_connection(&node, 10000)) {
            printf("[FAIL] Failed to establish HDLC connection for window %u\n", w);
            results[w-1].window = w;
            results[w-1].pass = false;
            results[w-1].time_s = 0.0;
            results[w-1].kbps = 0.0;
            node_cleanup(&node);
            sleep_ms(500);
            continue;
        }

        printf("Connected! Sending %u bytes in %d-byte chunks...\n", pdf_size, CHUNK_SIZE);

        double t_start = get_time_s();
        uint32_t sent = send_data(&node, pdf_data, pdf_size);

        if (node.running) {
            printf("\nFinished transmitting %u bytes. Waiting for final echoes...\n", sent);
            wait_for_echoes(&node, pdf_size, 10000);
        } else {
            printf("\nTest aborted due to disconnection.\n");
        }
        
        double t_end = get_time_s();
        double duration = t_end - t_start;

        double kbps = 0.0;
        bool passed = verify_results(&node, pdf_data, pdf_size, sent, duration, &kbps);
        
        results[w-1].window = w;
        results[w-1].pass = passed;
        results[w-1].time_s = duration;
        results[w-1].kbps = kbps;

        node_cleanup(&node);
        
        // Wait briefly before starting the next test to ensure target state completes
        sleep_ms(500);
    }

    free(pdf_data);

    /* Print Summary Table */
    printf("\n\n=================================================================\n");
    printf(" WINDOW SIZE PERFORMANCE SUMMARY (%d baud)\n", BAUD_RATE);
    printf("=================================================================\n");
    printf(" | Window | Result | Time (s) | Throughput (kbps) |\n");
    printf(" |--------|--------|----------|-------------------|\n");
    for (int i = 0; i < 7; i++) {
        printf(" |   %2u   |  %s  | %8.2f | %17.2f |\n",
               results[i].window,
               results[i].pass ? "PASS" : "FAIL",
               results[i].time_s,
               results[i].kbps);
    }
    printf("=================================================================\n\n");

    int total_fails = 0;
    for (int i = 0; i < 7; i++) {
        if (!results[i].pass) total_fails++;
    }

    if (total_fails > 0) {
        printf("\n[ERROR] %d/%d window size tests FAILED.\n", total_fails, 7);
        return 1;
    }
    
    printf("\n[SUCCESS] All %d window size tests PASSED.\n", 7);
    return 0;
}
