#include "test_virtual_pipe.h"
#include <stdlib.h>

#ifdef _WIN32

void YIELD_THREAD(void) {
    if (!SwitchToThread()) {
        Sleep(0);
    }
}

double get_time_s(void) {
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    LARGE_INTEGER time;
    QueryPerformanceCounter(&time);
    return (double)time.QuadPart / (double)freq.QuadPart;
}

// Windows thread stub to match pthread signature
typedef struct {
    thread_func_t func;
    void* arg;
} thread_stub_args_t;

static DWORD WINAPI thread_stub(LPVOID lpParam) {
    thread_stub_args_t* args = (thread_stub_args_t*)lpParam;
    thread_func_t func = args->func;
    void* arg = args->arg;
    free(args);
    func(arg);
    return 0;
}

void thread_create(thread_t* t, thread_func_t func, void* arg) {
    thread_stub_args_t* args = malloc(sizeof(thread_stub_args_t));
    if (args) {
        args->func = func;
        args->arg = arg;
        *t = CreateThread(NULL, 0, thread_stub, args, 0, NULL);
    }
}

void thread_join(thread_t t) {
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}

#else

void YIELD_THREAD(void) {
    usleep(100);
}

double get_time_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void thread_create(thread_t* t, thread_func_t func, void* arg) {
    pthread_create(t, NULL, func, arg);
}

void thread_join(thread_t t) {
    pthread_join(t, NULL);
}

#endif

// Pipe Queue Implementation
void pipe_init(pipe_queue_t* q) {
    q->head = 0;
    q->tail = 0;
    MUTEX_INIT(&q->lock);
}

void pipe_destroy(pipe_queue_t* q) {
    MUTEX_DESTROY(&q->lock);
}

int pipe_write(pipe_queue_t* q, const uint8_t* data, int len) {
    MUTEX_LOCK(&q->lock);
    int written = 0;
    for (int i = 0; i < len; i++) {
        int next_head = (q->head + 1) % sizeof(q->buffer);
        if (next_head == q->tail)
            break;
        q->buffer[q->head] = data[i];
        q->head = next_head;
        written++;
    }
    MUTEX_UNLOCK(&q->lock);
    return written;
}

int pipe_read(pipe_queue_t* q, uint8_t* data, int max_len) {
    MUTEX_LOCK(&q->lock);
    int read_count = 0;
    while (read_count < max_len && q->tail != q->head) {
        data[read_count++] = q->buffer[q->tail];
        q->tail = (q->tail + 1) % sizeof(q->buffer);
    }
    MUTEX_UNLOCK(&q->lock);
    return read_count;
}
