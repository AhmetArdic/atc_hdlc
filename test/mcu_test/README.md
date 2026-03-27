# atc_hdlc — MCU Port Layer

`hdlc_mcu_port.c` provides a ready-to-use HDLC station on any bare-metal MCU.
You only need to implement three platform functions declared in `hdlc_platform.h`.

## Files

| File | Purpose |
|------|---------|
| `hdlc_platform.h` | PAL contract — the three functions you must implement |
| `hdlc_mcu_port.h` | Public API (`init`, `run`, `transmit`) |
| `hdlc_mcu_port.c` | Generic implementation — add this to your build |

## Integration

1. Copy `hdlc_mcu_port.c` and `hdlc_mcu_port.h` into your project (or add the path to your build system).
2. Create a `hdlc_platform.c` file in your project and implement the three functions below.
3. Call `hdlc_port_init()` once, then `hdlc_port_run()` from your main loop.

## STM32 HAL + DMA example

The example below targets STM32 with USART + circular DMA on both RX and TX,
which is the most common bare-metal setup.  Adapt peripheral names and buffer
sizes to your board.

```c
/* hdlc_platform.c — STM32 HAL + DMA implementation */

#include "hdlc_platform.h"
#include "main.h"          /* UART/DMA handles from CubeMX */

/* ------------------------------------------------------------------ */
/*  TX ring buffer (written by HDLC, drained by DMA)                  */
/* ------------------------------------------------------------------ */
#define TX_RING_SIZE  4096u          /* must be a power of 2 */
#define TX_RING_MASK  (TX_RING_SIZE - 1u)

static uint8_t          tx_ring[TX_RING_SIZE];
static volatile uint16_t tx_head    = 0;
static volatile uint16_t tx_tail    = 0;
static volatile uint8_t  tx_dma_busy = 0;

/* Kick a DMA transfer from tx_tail toward tx_head (IRQ-safe). */
static void tx_flush_dma(void)
{
    __disable_irq();
    if (tx_dma_busy || tx_head == tx_tail) {
        __enable_irq();
        return;
    }
    uint16_t tail = tx_tail;
    uint16_t len  = (tx_head > tail) ? tx_head - tail
                                     : TX_RING_SIZE - tail;
    tx_dma_busy = 1;
    __enable_irq();

    if (HAL_UART_Transmit_DMA(&huart2, &tx_ring[tail], len) != HAL_OK) {
        __disable_irq();
        tx_dma_busy = 0;
        __enable_irq();
    }
}

/* Chain the next DMA burst when the previous one completes. */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        tx_tail     = (tx_tail + huart->TxXferSize) & TX_RING_MASK;
        tx_dma_busy = 0;
        tx_flush_dma();
    }
}

/* ------------------------------------------------------------------ */
/*  RX ring buffer (written by circular DMA, read by port_rx_read)    */
/* ------------------------------------------------------------------ */
#define RX_RING_SIZE  4096u

static uint8_t          rx_ring[RX_RING_SIZE];
static uint16_t         rx_tail = 0;

/* Start circular DMA once during system init. */
void hdlc_platform_uart_init(void)
{
    HAL_UART_Receive_DMA(&huart2, rx_ring, RX_RING_SIZE);
}

/* ------------------------------------------------------------------ */
/*  PAL implementation                                                 */
/* ------------------------------------------------------------------ */

void port_tx_byte(uint8_t byte, bool flush)
{
    uint16_t next = (tx_head + 1u) & TX_RING_MASK;
    while (next == tx_tail)         /* spin if ring is full */
        tx_flush_dma();

    tx_ring[tx_head] = byte;
    tx_head = next;

    if (flush)
        tx_flush_dma();
}

uint32_t port_tick_ms(void)
{
    return HAL_GetTick();           /* 1 ms resolution from SysTick */
}

uint16_t port_rx_read(uint8_t *buf, uint16_t max_len)
{
    uint16_t dma_head = (uint16_t)(RX_RING_SIZE
                        - __HAL_DMA_GET_COUNTER(huart2.hdmarx));
    if (dma_head == RX_RING_SIZE)
        dma_head = 0;

    uint16_t copied = 0;

    while (rx_tail != dma_head && copied < max_len) {
        buf[copied++] = rx_ring[rx_tail];
        rx_tail = (rx_tail + 1u) % RX_RING_SIZE;
    }

    return copied;
}
```

### main.c skeleton

```c
#include "hdlc_mcu_port.h"
#include "hdlc_platform.h"   /* for hdlc_platform_uart_init() */

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_USART2_UART_Init();

    hdlc_platform_uart_init();   /* start circular RX DMA */

    hdlc_port_config_t cfg = {
        .local_addr  = 0x02,
        .peer_addr   = 0x01,
        .max_retries = 10,
        .t1_ms       = 500,
        .t2_ms       = 1,
    };
    hdlc_port_init(&cfg);

    while (1)
        hdlc_port_run();
}
```

### Receiving data

Override the weak `hdlc_port_on_data` hook in your application:

```c
#include "hdlc_mcu_port.h"

/* Called by hdlc_mcu_port.c whenever a complete I-frame is received. */
void hdlc_port_on_data(const atc_hdlc_u8 *data, atc_hdlc_u16 len)
{
    /* process received payload — e.g. copy to an application queue */
    app_queue_push(data, len);
}
```

If you do not define this function the default behaviour is to echo the
payload back to the peer as a new I-frame.

### Handling link events

```c
#include "hdlc_mcu_port.h"

void hdlc_port_on_event(atc_hdlc_event_t event)
{
    switch (event) {
    case ATC_HDLC_EVENT_CONNECT_ACCEPTED:
        LED_On(LED_LINK);
        break;
    case ATC_HDLC_EVENT_LINK_FAILURE:
        LED_Off(LED_LINK);
        break;
    default:
        break;
    }
}
```

## C2000 + DriverLib example

The example below targets TI C2000 (e.g. F28379D) using the SCI peripheral
and DriverLib.  TX is FIFO-buffered; RX polls the SCI RX FIFO each
`hdlc_port_run()` iteration.  A 1 ms CPUTimer ISR drives the tick counter.

```c
/* hdlc_platform.c — TI C2000 DriverLib implementation */

#include "hdlc_platform.h"
#include "driverlib.h"
#include "device.h"

#define HDLC_SCI_BASE  SCIA_BASE

/* ------------------------------------------------------------------ */
/*  Millisecond tick (incremented by CPUTimer0 ISR)                   */
/* ------------------------------------------------------------------ */
static volatile uint32_t tick_ms = 0;

__interrupt void cpuTimer0ISR(void)
{
    tick_ms++;
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

/* Configure CPUTimer0 for a 1 ms period and register the ISR.
 * Call this once during system init before hdlc_port_init(). */
void hdlc_platform_timer_init(void)
{
    CPUTimer_stopTimer(CPUTIMER0_BASE);
    CPUTimer_setPeriod(CPUTIMER0_BASE, DEVICE_SYSCLK_FREQ / 1000UL - 1UL);
    CPUTimer_setPreScaler(CPUTIMER0_BASE, 0);
    CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
    CPUTimer_enableInterrupt(CPUTIMER0_BASE);

    Interrupt_register(INT_TIMER0, &cpuTimer0ISR);
    Interrupt_enable(INT_TIMER0);

    CPUTimer_startTimer(CPUTIMER0_BASE);
}

/* ------------------------------------------------------------------ */
/*  PAL implementation                                                 */
/* ------------------------------------------------------------------ */

void port_tx_byte(uint8_t byte, bool flush)
{
    (void)flush;   /* SCI FIFO drains autonomously — no explicit flush needed */
    SCI_writeCharBlockingFIFO(HDLC_SCI_BASE, (uint16_t)byte);
}

uint32_t port_tick_ms(void)
{
    return tick_ms;
}

uint16_t port_rx_read(uint8_t *buf, uint16_t max_len)
{
    uint16_t copied = 0;

    while (copied < max_len &&
           SCI_getRxFIFOStatus(HDLC_SCI_BASE) != SCI_FIFO_RX0)
    {
        buf[copied++] = (uint8_t)(SCI_readCharBlockingFIFO(HDLC_SCI_BASE) & 0xFFu);
    }

    return copied;
}
```

### main.c skeleton

```c
#include "driverlib.h"
#include "device.h"
#include "hdlc_mcu_port.h"
#include "hdlc_platform.h"   /* for hdlc_platform_timer_init() */

int main(void)
{
    Device_init();
    Device_initGPIO();
    Interrupt_initModule();
    Interrupt_initVectorTable();

    /* Configure SCIA pins and peripheral (baud rate, 8N1, FIFO) here
     * using SCI_setConfig() / SCI_enableFIFO() / SCI_enableModule(). */
    hdlc_platform_sci_init();    /* your SCI setup function */
    hdlc_platform_timer_init();  /* 1 ms CPUTimer0 */

    EINT;
    ERTM;

    hdlc_port_config_t cfg = {
        .local_addr  = 0x02,
        .peer_addr   = 0x01,
        .max_retries = 10,
        .t1_ms       = 500,
        .t2_ms       = 1,
    };
    hdlc_port_init(&cfg);

    while (1)
        hdlc_port_run();
}
```

### C2000-specific notes

- **SCI char width**: DriverLib `SCI_readCharBlockingFIFO` returns `uint16_t`.
  Mask with `& 0xFF` when copying into `uint8_t` buffers (shown above).
- **TX blocking**: `SCI_writeCharBlockingFIFO` spins until the TX FIFO has a
  free slot.  If you need non-blocking TX, replace it with a software ring
  buffer and a `SCI_TX_INT` ISR — the same pattern as the STM32 DMA example.
- **RX FIFO depth**: C2000 SCI FIFO is 16 levels deep.  `HDLC_PORT_RX_CHUNK`
  (default 256) is larger than the FIFO, so `port_rx_read` will drain it
  completely each call.  Set the RX FIFO interrupt level to `SCI_FIFO_RX1`
  if you use interrupt-driven RX instead of polling.
- **Interrupt acknowledge**: always call `Interrupt_clearACKGroup` at the end
  of every PIE ISR (shown in `cpuTimer0ISR` above).

---

## Build system notes

Add to your compiler flags to change buffer sizes without editing any file:

```
-DHDLC_PORT_MAX_INFO=256
-DHDLC_PORT_WINDOW=4
-DHDLC_PORT_RX_CHUNK=128
```

Add these source files to your build:

```
src/hdlc_station.c
src/hdlc_in.c
src/hdlc_out.c
src/hdlc_dispatch.c
src/hdlc_crc.c
test/mcu_test/hdlc_mcu_port.c
<your>/hdlc_platform.c
```

Include paths needed:

```
inc/
test/mcu_test/
```
