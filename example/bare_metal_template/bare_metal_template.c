/*
 * bare_metal_template.c — Bare-metal integration template.
 *
 * Shows how to wire the library into a typical MCU application:
 *   - All storage is global/static (no heap)
 *   - UART TX driven byte-by-byte via on_send callback
 *   - UART RX bytes buffered by ISR, drained by main loop
 *   - T1/T2 timer expiry signalled via volatile flags from timer ISR,
 *     handled in main loop (atc_hdlc_data_in is not ISR-safe)
 *
 * Sections marked "PLATFORM:" must be replaced with your MCU's
 * peripheral API (HAL, LL, register-level, etc.).
 *
 * This file compiles and runs on a PC as-is (stubs produce text
 * output instead of UART bytes) so you can verify the HDLC logic
 * before bringing up hardware.
 *
 * Build:
 *   make bare_metal_template   (from build/)
 */

#include "hdlc.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================================================================== */
/* Platform section — replace with your MCU's peripheral code        */
/* ================================================================== */

/*
 * PLATFORM: UART transmit one byte.
 * On a real target this writes to the UART data register and waits
 * for the TX-ready flag (or uses DMA / interrupt-driven TX).
 *
 * The library calls this once per byte; flush=true on the last byte
 * of a frame so you can trigger a DMA flush or enable the TX interrupt.
 */
static int uart_send(atc_hdlc_u8 byte, bool flush, void* user_ctx) {
    (void)flush;
    (void)user_ctx;
    /* PLATFORM: e.g. while (!(USART1->ISR & USART_ISR_TXE)); USART1->TDR = byte; */
    printf("%02X ", (unsigned)byte);
    if (flush)
        putchar('\n');
    return 0;
}

/* ------------------------------------------------------------------ */
/* RX ring buffer — ISR writes, main loop reads.                     */
/* ------------------------------------------------------------------ */

#define RX_RING_SIZE 128

static volatile atc_hdlc_u8 rx_ring[RX_RING_SIZE];
static volatile uint32_t rx_head = 0; /* written by ISR  */
static volatile uint32_t rx_tail = 0; /* read by main    */

/*
 * PLATFORM: Call this from your UART RX interrupt handler.
 * e.g. void USART1_IRQHandler(void) { uart_rx_isr(USART1->RDR); }
 */
static void uart_rx_isr(atc_hdlc_u8 byte) {
    uint32_t next = (rx_head + 1) % RX_RING_SIZE;
    if (next != rx_tail) { /* drop on overflow */
        rx_ring[rx_head] = byte;
        rx_head = next;
    }
}

/* Drain the ring buffer and feed bytes into the HDLC engine.
 * Must be called from main loop context (not from ISR). */
static void rx_drain(atc_hdlc_ctx_t* ctx) {
    while (rx_tail != rx_head) {
        atc_hdlc_u8 b = rx_ring[rx_tail];
        rx_tail = (rx_tail + 1) % RX_RING_SIZE;
        atc_hdlc_data_in(ctx, &b, 1);
    }
}

/* ------------------------------------------------------------------ */
/* Timer expiry flags — set by timer ISR, cleared in main loop.      */
/* ------------------------------------------------------------------ */

static volatile bool t1_expired_flag = false;
static volatile bool t2_expired_flag = false;

/*
 * PLATFORM: Start/stop your hardware T1 timer.
 * e.g. TIM2->ARR = ms_to_ticks(ms); TIM2->CR1 |= TIM_CR1_CEN;
 */
static void t1_start(atc_hdlc_u32 ms, void* u) {
    (void)u;
    t1_expired_flag = false;
    /* PLATFORM: arm hardware timer for `ms` milliseconds */
    (void)ms;
}
static void t1_stop(void* u) {
    (void)u;
    t1_expired_flag = false;
    /* PLATFORM: disarm hardware timer */
}

/*
 * PLATFORM: Call this from your T1 timer interrupt handler.
 * e.g. void TIM2_IRQHandler(void) { TIM2->SR = 0; t1_timer_isr(); }
 */
static void t1_timer_isr(void) {
    t1_expired_flag = true;
}

static void t2_start(atc_hdlc_u32 ms, void* u) {
    (void)u;
    t2_expired_flag = false;
    /* PLATFORM: arm T2 timer */
    (void)ms;
}
static void t2_stop(void* u) {
    (void)u;
    t2_expired_flag = false;
    /* PLATFORM: disarm T2 timer */
}
static void t2_timer_isr(void) {
    t2_expired_flag = true;
}

/* ================================================================== */
/* Application callbacks                                              */
/* ================================================================== */

static void on_data(const atc_hdlc_u8* data, atc_hdlc_u16 len, void* u) {
    (void)u;
    printf("[app] received %u bytes\n", len);
    (void)data;
}

static void on_event(atc_hdlc_event_t event, void* u) {
    (void)u;
    /* PLATFORM: application state machine — react to link events */
    switch (event) {
    case ATC_HDLC_EVENT_CONN_ACCEPTED:
        printf("[app] connected\n");
        break;
    case ATC_HDLC_EVENT_CONN_REQ:
        printf("[app] peer connected\n");
        break;
    case ATC_HDLC_EVENT_DISC_DONE:
    case ATC_HDLC_EVENT_PEER_DISC:
        printf("[app] disconnected\n");
        break;
    case ATC_HDLC_EVENT_LINK_FAILURE:
        printf("[app] link failure\n");
        break;
    case ATC_HDLC_EVENT_WINDOW_OPEN:
        printf("[app] TX window open\n");
        break;
    default:
        break;
    }
}

/* ================================================================== */
/* Static storage — sizes tuned for a small MCU                      */
/* ================================================================== */

#define WINDOW_SIZE   1
#define MAX_INFO_SIZE 64

static atc_hdlc_u8 rx_buf_mem[MAX_INFO_SIZE + 8]; /* +8 for addr/ctrl/FCS */
static atc_hdlc_u8 tx_slots[WINDOW_SIZE * MAX_INFO_SIZE];
static atc_hdlc_u32 tx_lens[WINDOW_SIZE];

static const atc_hdlc_config_t cfg = {
    .mode = ATC_HDLC_MODE_ABM,
    .address = 0x01, /* PLATFORM: this station's address */
    .max_info_size = MAX_INFO_SIZE,
    .max_retries = 3,
    .t1_ms = 500,
    .t2_ms = 20,
};

static const atc_hdlc_plat_ops_t platform = {
    .on_send = uart_send,
    .on_data = on_data,
    .on_event = on_event,
    .t1_start = t1_start,
    .t1_stop = t1_stop,
    .t2_start = t2_start,
    .t2_stop = t2_stop,
};

static atc_hdlc_txwin_t tx_win = {tx_slots, tx_lens, MAX_INFO_SIZE, WINDOW_SIZE};
static atc_hdlc_rxbuf_t rx_buf = {rx_buf_mem, sizeof(rx_buf_mem)};
static atc_hdlc_ctx_t ctx;

/* ================================================================== */
/* Main / application loop                                           */
/* ================================================================== */

int main(void) {
    /* PLATFORM: clock, GPIO, UART, timer peripheral init goes here */

    atc_hdlc_init(&ctx, (atc_hdlc_params_t){
                            .config = &cfg,
                            .platform = &platform,
                            .tx_window = &tx_win,
                            .rx_buf = &rx_buf,
                        });

    /* Initiate connection to peer at address 0x02 */
    atc_hdlc_link_setup(&ctx, 0x02);

    /* ----------------------------------------------------------------
     * Main loop.  On a bare-metal target this loop runs forever.
     * With an RTOS this becomes a task body.
     * ---------------------------------------------------------------- */
    for (int i = 0; i < 4; i++) { /* PC stub: finite iterations */

        /* 1. Drain UART RX ring buffer into the HDLC engine */
        rx_drain(&ctx);

        /* 2. Handle timer expiry (deferred from ISR) */
        if (t1_expired_flag) {
            t1_expired_flag = false;
            atc_hdlc_t1_expired(&ctx);
        }
        if (t2_expired_flag) {
            t2_expired_flag = false;
            atc_hdlc_t2_expired(&ctx);
        }

        /* 3. Application TX — only when connected and window is open */
        if (atc_hdlc_get_state(&ctx) == ATC_HDLC_STATE_CONNECTED) {
            const atc_hdlc_u8 payload[] = "sensor_data";
            atc_hdlc_error_t err = atc_hdlc_transmit_i(&ctx, payload, sizeof(payload) - 1);
            if (err == ATC_HDLC_ERR_WINDOW_FULL) {
                /* Window full — wait for WINDOW_OPEN event before retrying */
            }
        }

        /* PLATFORM: sleep or yield to RTOS scheduler */
    }

    /* ----------------------------------------------------------------
     * Simulate receiving bytes from UART (PC stub only).
     * On a real target the ISR fills rx_ring automatically.
     * ---------------------------------------------------------------- */
    const atc_hdlc_u8 fake_rx[] = {0x7E, 0x01, 0x03, 0x8B, 0xB9, 0x7E}; /* DM */
    for (atc_hdlc_u32 i = 0; i < sizeof(fake_rx); i++)
        uart_rx_isr(fake_rx[i]);
    rx_drain(&ctx);

    /* Demonstrate ISR-style timer simulation */
    t1_timer_isr();
    t2_timer_isr();

    if (t1_expired_flag) {
        t1_expired_flag = false;
        atc_hdlc_t1_expired(&ctx);
    }
    if (t2_expired_flag) {
        t2_expired_flag = false;
        atc_hdlc_t2_expired(&ctx);
    }

    return 0;
}
