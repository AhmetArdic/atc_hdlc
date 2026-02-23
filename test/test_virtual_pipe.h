#ifndef TEST_VIRTUAL_PIPE_H
#define TEST_VIRTUAL_PIPE_H

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include <stdint.h>
#include <stdbool.h>

/* OS Abstractions for threading and synchronization */
#ifdef _WIN32
#include <windows.h>
#include <process.h>
typedef HANDLE thread_t;
typedef CRITICAL_SECTION mutex_t;
#define MUTEX_INIT(m) InitializeCriticalSection(m)
#define MUTEX_LOCK(m) EnterCriticalSection(m)
#define MUTEX_UNLOCK(m) LeaveCriticalSection(m)
#define MUTEX_DESTROY(m) DeleteCriticalSection(m)
#define SLEEP_MS(ms) Sleep(ms)
#else
#include <pthread.h>
#include <unistd.h>
#include <time.h>
typedef pthread_t thread_t;
typedef pthread_mutex_t mutex_t;
#define MUTEX_INIT(m) pthread_mutex_init(m, NULL)
#define MUTEX_LOCK(m) pthread_mutex_lock(m)
#define MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#define MUTEX_DESTROY(m) pthread_mutex_destroy(m)
#define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

// Shared timing and thread-yield utilities
void YIELD_THREAD(void);
double get_time_s(void);

// Generic thread creation/join functions
// The func takes a void* arg and returns a void*
typedef void* (*thread_func_t)(void*);

void thread_create(thread_t *t, thread_func_t func, void *arg);
void thread_join(thread_t t);

/* Pipe Queue for simulating serial connections in memory */
typedef struct {
    uint8_t buffer[65536];
    int head;
    int tail;
    mutex_t lock;
} pipe_queue_t;

void pipe_init(pipe_queue_t *q);
void pipe_destroy(pipe_queue_t *q);
int pipe_write(pipe_queue_t *q, const uint8_t *data, int len);
int pipe_read(pipe_queue_t *q, uint8_t *data, int max_len);

#endif // TEST_VIRTUAL_PIPE_H
