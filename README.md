# HDLC-Like Embedded Protocol Stack

A lightweight, portable HDLC (High-Level Data Link Control) protocol implementation designed for embedded systems with UART (Asynchronous) communication.

## Protocol Features

This library implements a highly capable subset of the ISO/IEC 13239 HDLC standard, specifically tailored for asynchronous serial communication:

- **Framing & Transparency**: Standard `0x7E` flag boundaries with `0x7D` byte-stuffing inversion for binary-transparent links.
- **Data Integrity**: 16-bit CRC-CCITT verification discarding corrupted frames instantly.
- **Asynchronous Balanced Mode (ABM)**: Full connection lifecycle management using `SABM`, `UA`, `DISC`, and `DM` frames. Peer-to-peer topology where either side can initiate or disconnect.
- **Reliable Data Transfer (Go-Back-N)**: Sliding window protocol (Modulo-8) with up to 7 outstanding I-frames. Uses `REJ` frames for swift error recovery.
- **Piggybacked & Cumulative ACKs**: Information (I) frames carry receive sequence numbers `N(R)`. `RR` frames are only sent when delayed ACK (T2) times out, reducing overhead.
- **Connectionless Data**: Unnumbered Information (`UI`) frames for broadcast or unacknowledged low-latency messages.
- **Link Verification**: Automatic `TEST` frame responding with optional payload echoing.
- **Collision Avoidance**: SABM contention resolution delay via address prioritization if both stations try to connect simultaneously.
- **Flow Control**: RNR (Receive Not Ready) support with `atc_hdlc_set_local_busy()`.
- **Frame Reject (FRMR) Handling**: Full generation and parsing of FRMR payload to gracefully report irrecoverable protocol violations.
- **Abort Support**: `atc_hdlc_abort()` for line break/framing error recovery.

## Roadmap (Upcoming Features)

- **[TODO] Selective Reject (SREJ)**: Upgrading from Go-Back-N to Selective Repeat for higher efficiency on lossy links.
- **[TODO] Extended Sequence Numbers (Modulo-128)**: Supporting extended control fields for `SABME`/I-frames.
- **[TODO] Extended Addressing**: Multi-byte station addresses for large networks.
- **[TODO] Parameter Negotiation (XID)**: Dynamic negotiation of Window Size, Modulus, Timers, and Max Frame Length.

## Project Structure

```
.
├── inc/
│   ├── hdlc.h              # Public API (atc_hdlc_* functions)
│   ├── hdlc_types.h        # Types, callbacks, error codes, context
│   └── hdlc_config.h       # Configuration defaults (overridable via #define)
├── src/
│   ├── CMakeLists.txt
│   ├── hdlc_private.h      # Internal constants, bit-field accessors, timer helpers
│   ├── hdlc_frame.h        # Frame constants, field macros (FLAG, ESC, CTRL_*)
│   ├── hdlc_crc.c          # CRC-16-CCITT LUT and update function
│   ├── hdlc_crc.h
│   ├── hdlc_station.c      # Init, link setup/disconnect, state transitions
│   ├── hdlc_in.c           # Byte-by-byte RX parser (byte-stuffing removal, CRC)
│   ├── hdlc_dispatch.c     # Frame dispatch and receive-side protocol logic
│   └── hdlc_out.c          # TX streaming engine (byte-stuffing, frame construction)
├── test/
│   ├── test_hdlc.c
│   ├── test_reliable_transmission.c
│   ├── test_connection_management.c
│   ├── test_init_validation.c
│   ├── test_error_codes.c
│   ├── test_virtual_com.c
│   ├── test_virtual_pipe.c
│   ├── test_virtual_pipe.h
│   ├── test_physical_target.c
│   ├── test_common.c
│   └── test_common.h
├── CMakeLists.txt
└── README.md
```

## Architecture

Three layers:

**Public API** (`inc/`): `hdlc.h`, `hdlc_types.h`, `hdlc_config.h`.

**Core Implementation** (`src/`):
- `hdlc_frame.h` — Frame constants and field accessor macros.
- `hdlc_crc.c` — CRC-16-CCITT lookup table.
- `hdlc_station.c` — Init, link setup/disconnect, state transitions.
- `hdlc_in.c` — RX parser (byte-stuffing removal, CRC validation, reassembly).
- `hdlc_dispatch.c` — Frame dispatch and receive-side protocol logic (I/S/U handling).
- `hdlc_out.c` — TX streaming engine (byte-stuffing, frame construction, flush).

### Data Flow

```
RX: UART byte → atc_hdlc_data_in() → hdlc_in.c (accumulate + unstuff)
    → CRC check → frame handlers → on_data / on_event callbacks

TX: atc_hdlc_transmit_i/ui/test() → hdlc_out.c (stuff + CRC)
    → on_send(byte, flush) callback → UART

Timers: User calls atc_hdlc_t1_expired() / atc_hdlc_t2_expired() from timer ISR
```

## Build & Run

```bash
mkdir build && cd build
cmake ..
make

# Run tests
ctest --verbose
```

## Integration

### 1. Define Callbacks

```c
int my_on_send(atc_hdlc_u8 byte, atc_hdlc_bool flush, void *ctx) {
    UART_SendByte(byte);
    if (flush) { /* flush hardware buffer */ }
    return 0;
}

void my_on_data(const atc_hdlc_u8 *payload, atc_hdlc_u16 len, void *ctx) {
    // Process received data
}

void my_on_event(atc_hdlc_event_t event, void *ctx) {
    switch (event) {
        case ATC_HDLC_EVENT_CONNECT_ACCEPTED:    /* UA received for our SABM */     break;
        case ATC_HDLC_EVENT_INCOMING_CONNECT:    /* peer sent SABM, auto-accepted */ break;
        case ATC_HDLC_EVENT_DISCONNECT_COMPLETE: /* UA received for our DISC */      break;
        case ATC_HDLC_EVENT_PEER_DISCONNECT:     /* peer sent DISC */                break;
        case ATC_HDLC_EVENT_PEER_REJECT:         /* peer sent DM */                  break;
        case ATC_HDLC_EVENT_PROTOCOL_ERROR:      /* peer sent FRMR */                break;
        case ATC_HDLC_EVENT_LINK_FAILURE:        /* N2 retries exceeded */           break;
        case ATC_HDLC_EVENT_REMOTE_BUSY_ON:      /* peer sent RNR */                 break;
        case ATC_HDLC_EVENT_REMOTE_BUSY_OFF:     /* peer sent RR after RNR */        break;
        case ATC_HDLC_EVENT_WINDOW_OPEN:         /* TX window slot freed */          break;
        default: break;
    }
}

void my_t1_start(atc_hdlc_u32 ms, void *ctx) { start_timer(ms); }
void my_t1_stop(void *ctx) { stop_timer(); }
void my_t2_start(atc_hdlc_u32 ms, void *ctx) { start_timer(ms); }
void my_t2_stop(void *ctx) { stop_timer(); }
```

### 2. Initialize Context

```c
atc_hdlc_context_t ctx;

// Configuration
atc_hdlc_config_t config = {
    .mode = ATC_HDLC_MODE_ABM,
    .address = 0x01,
    .window_size = 3,
    .max_frame_size = 256,
    .max_retries = 3,
    .t1_ms = 1000,
    .t2_ms = 50,
};

// Platform callbacks
atc_hdlc_platform_t platform = {
    .on_send = my_on_send,
    .on_data = my_on_data,
    .on_event = my_on_event,
    .user_ctx = NULL,
    .t1_start = my_t1_start,
    .t1_stop = my_t1_stop,
    .t2_start = my_t2_start,
    .t2_stop = my_t2_stop,
};

// TX window (for reliable I-frames)
uint8_t tx_slots[3 * 256];  // slot_count * slot_capacity
uint32_t tx_slot_lens[3];
atc_hdlc_tx_window_t tx_window = {
    .slots = tx_slots,
    .slot_lens = tx_slot_lens,
    .slot_capacity = 256,
    .slot_count = 3,
};

// RX buffer
uint8_t rx_buffer[512];
atc_hdlc_rx_buffer_t rx_buf = {
    .buffer = rx_buffer,
    .capacity = sizeof(rx_buffer),
};

// Init
atc_hdlc_params_t params = {
    .config    = &config,
    .platform  = &platform,
    .tx_window = &tx_window,
    .rx_buf    = &rx_buf,
};
atc_hdlc_init(&ctx, params);

// Connect
atc_hdlc_link_setup(&ctx, 0x02);  // peer address
```

### 3. Feed Received Bytes

> **Note**: `atc_hdlc_data_in` is **not ISR-safe**. Use a ring buffer to decouple reception.

```c
// ISR: push to ring buffer
void UART_RX_ISR(void) {
    ring_buffer_push(&rx_buf, UART_ReadByte());
}

// Main loop
void main_loop(void) {
    uint8_t byte;
    while (ring_buffer_pop(&rx_buf, &byte)) {
        atc_hdlc_data_in(&ctx, &byte, 1);
    }
}
```

### 4. Send Data

```c
// Reliable I-frame (queued in TX window)
atc_hdlc_transmit_i(&ctx, (atc_hdlc_u8 *)"Hello", 5);

// Unacknowledged UI-frame
atc_hdlc_transmit_ui(&ctx, 0xFF, (atc_hdlc_u8 *)"Broadcast", 10);

// TEST frame
atc_hdlc_transmit_test(&ctx, 0x02, (atc_hdlc_u8 *)"ping", 4);
```

### 5. Streaming TX (Low-Memory)

```c
atc_hdlc_transmit_ui_start(&ctx, 0x02);
atc_hdlc_transmit_ui_data(&ctx, (atc_hdlc_u8 *)"chunk1", 6);
atc_hdlc_transmit_ui_data(&ctx, (atc_hdlc_u8 *)"chunk2", 6);
atc_hdlc_transmit_ui_end(&ctx);
```

### 6. Timer Interrupts

```c
void T1_TIMER_IRQ(void) {  // Retransmission timeout
    atc_hdlc_t1_expired(&ctx);
}

void T2_TIMER_IRQ(void) {  // Delayed ACK timeout
    atc_hdlc_t2_expired(&ctx);
}
```

## API Reference

### Initialization & Connection

| Function | Description |
|---|---|
| `atc_hdlc_init()` | Initialize context with config and callbacks |
| `atc_hdlc_link_setup()` | Initiate connection (sends SABM) |
| `atc_hdlc_disconnect()` | Terminate connection (sends DISC) |
| `atc_hdlc_link_reset()` | Reset and reconnect (after FRMR) |
| `atc_hdlc_abort()` | Abort on line break/framing error |
| `atc_hdlc_get_state()` | Get current state |

### Data Transfer

| Function | Description |
|---|---|
| `atc_hdlc_transmit_i()` | Send reliable I-frame |
| `atc_hdlc_transmit_ui()` | Send UI-frame (connectionless) |
| `atc_hdlc_transmit_test()` | Send TEST frame |
| `atc_hdlc_transmit_ui_start()` | Begin streaming UI TX |
| `atc_hdlc_transmit_ui_data()` | Add bytes to TX stream |
| `atc_hdlc_transmit_ui_end()` | Finish TX stream |
| `atc_hdlc_data_in()` | Feed received bytes |

### Timers

| Function | Description |
|---|---|
| `atc_hdlc_t1_expired()` | Call from T1 timer ISR (retransmission) |
| `atc_hdlc_t2_expired()` | Call from T2 timer ISR (delayed ACK) |

### Flow Control

| Function | Description |
|---|---|
| `atc_hdlc_set_local_busy()` | Set local RNR (tell peer to pause) |

## Configuration

### Runtime Configuration (`atc_hdlc_config_t`)

Set these fields before calling `atc_hdlc_init()`:

| Field | Default | Description |
|---|---|---|
| `mode` | `ATC_HDLC_MODE_ABM` | Operating mode (only ABM supported) |
| `address` | — | Local station address |
| `window_size` | `1` | Sliding window size (1–7) |
| `max_frame_size` | `256` | Maximum information field size (MRU) |
| `max_retries` | `3` | N2 retry limit before link failure |
| `t1_ms` | `1000` | T1 retransmission timeout (ms) |
| `t2_ms` | `10` | T2 delayed ACK timeout (ms, must be < t1_ms) |

### Compile-Time Defaults (`inc/hdlc_config.h`)

Override these macros before including `hdlc.h`:

| Macro | Default | Description |
|---|---|---|
| `ATC_HDLC_DEFAULT_T1_TIMEOUT` | `1000` | Default T1 timeout (ms) |
| `ATC_HDLC_DEFAULT_T2_TIMEOUT` | `10` | Default T2 timeout (ms) |
| `ATC_HDLC_DEFAULT_N2_RETRY_COUNT` | `3` | Default N2 retry limit |
| `ATC_HDLC_DEFAULT_WINDOW_SIZE` | `1` | Default window size (1–7) |
| `ATC_HDLC_ENABLE_DEBUG_LOGS` | `0` | Enable debug logging |
| `ATC_HDLC_LOG_LEVEL` | `WRN` | Verbosity ceiling (`ERR`/`WRN`/`INFO`/`DBG`) |

To redirect debug output on bare-metal targets:

```c
#define ATC_HDLC_LOG_IMPL(level, fmt, ...) \
    my_log_function(level, fmt, ##__VA_ARGS__)
#include "hdlc.h"
```

## License

GNU General Public License v3.0 (GPLv3). See [LICENSE](LICENSE).
