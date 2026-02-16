# HDLC-Like Embedded Protocol Stack

A lightweight, portable HDLC (High-Level Data Link Control) protocol implementation designed for embedded systems with UART (Asynchronous) communication.

## 🚀 Features

*   **Robust Framing**:
    *   Unambiguous Flag delimiting (`0x7E`).
    *   Deterministic Byte Stuffing (`0x7E` → `0x7D 0x5E`, `0x7D` → `0x7D 0x5D`).
*   **Data Integrity**:
    *   **CRC-16-CCITT** (Polynomial `0x1021`) with pre-computed 256-entry LUT for fast validation.
    *   Recalculate & Compare verification strategy on the receiver side.
*   **Flexible Transmission**:
    *   **Packet Mode**:
        *   **Buffered**: Construct a full `atc_hdlc_frame_t` and send it in one call using the context.
        *   **Zero-Copy**: Send frames byte-by-byte (`start` → `data` → `end`) for ultra-low memory environments.
    *   **Stateless Mode**: Pack/Unpack frames directly into memory buffers without using the `hdlc_context_t` or callbacks. Ideal for purely functional usage.
*   **Reliable Data Transfer (Go-Back-N)**:
    *   **Parametric Window Size** (1..7, configurable at init). Window=1 is Stop-and-Wait.
    *   **Cumulative Acknowledgment**: N(R) in any received frame acknowledges all frames with N(S) < N(R).
    *   **Automatic Retransmission**: On timeout, all outstanding frames from V(A) to V(S)-1 are retransmitted (Go-Back-N).
    *   **REJ (Reject) Handling**: Peer can request retransmission from a specific sequence number.
    *   **Piggyback ACK**: Outgoing I-frames carry N(R) to acknowledge received frames without a separate RR.
    *   **Configurable Retransmission Timeout**: Runtime-configurable T1 timer (default 1000ms).
    *   Zero-allocation slotted retransmit buffer — user provides a single contiguous buffer, library divides into `window_size` equal slots.
*   **Protocol Infrastructure**:
    *   Built-in support for I-Frames, S-Frames, and U-Frames with bit-field accessors.
    *   Control Field helper functions for constructing each frame type.
    *   **Asynchronous Balanced Mode (ABM)**:
        *   Full Connection Management (`SABM`, `UA`, `DISC`, `DM`).
        *   Explicit rejection of unsupported modes (`SNRM`, `SARM`) with `DM`.
        *   Connection State Machine (`DISCONNECTED` ↔ `CONNECTING` ↔ `CONNECTED` ↔ `DISCONNECTING`).
    *   **TEST Frame**: Send and auto-echo TEST frames with optional data payload for link verification.
    *   **Multi-Slave / Broadcast Support**:
        *   Broadcast Address (`0xFF`) support for UI frames.
        *   Slaves silently ignore broadcast connection management commands (`SABM`, `DISC`) to prevent bus contention.
        *   Broadcast UI frames are accepted without generating a response.
    *   Frame Type dispatcher.
*   **Developer Experience**:
    *   Modern **CMake** build system (C99).
    *   **Unit tests** covering edge cases (byte stuffing, CRC errors, overflow, fragmentation, control field loopback, reliable transmission, Go-Back-N, TEST frames).
    *   **Configurable Symbol Prefix**: All public symbols are prefixed (default `atc_`) to avoid collisions. Changeable at compile time via `ATC_HDLC_PREFIX`.
    *   **C++ compatible** (`extern "C"` wrappers).

## 📂 Project Structure

```
.
├── inc/
│   ├── hdlc.h          # Public API (init, send, receive, packet processing)
│   ├── hdlc_types.h    # Public types (frame, context, callbacks, control field)
│   └── hdlc_config.h   # Configuration (prefix, max frame length)
├── src/
│   ├── hdlc.c          # Core implementation (TX/RX engines, control helpers)
│   ├── hdlc_crc.c      # CRC-16-CCITT LUT and update function
│   ├── hdlc_crc.h      # Internal CRC API
│   └── hdlc_private.h  # Internal RX state machine definitions
├── test/
│   ├── test_hdlc.c                  # Core protocol unit tests
│   ├── test_reliable_transmission.c # Reliable TX, Go-Back-N, retransmission tests
│   ├── test_connection_management.c # State machine & connection tests
│   ├── test_common.c                # Shared test utilities (colors, assertions)
│   └── test_common.h                # Shared test header
├── CMakeLists.txt      # Root CMake configuration
└── README.md           # This file
```

## 🛠️ Build & Run

### Prerequisites
*   C Compiler (GCC recommended)
*   CMake ≥ 3.10

### Compile

```bash
mkdir build && cd build
cmake ..
make
```

On Windows with MinGW:
```bash
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
make
```

### Run Tests

```bash
cd build
ctest --verbose
```

### Example Output
```text
========================================
TEST: Basic Frame (I-Frame)
========================================
   [ON FRAME EVENT] Frame Received!
   Type: 0, Addr: FF, Ctrl: 00, Information Len: 4
   Information: 54 45 53 54
[PASS] Basic Frame

========================================
TEST: Byte Stuffing Heavy
========================================
   [ON FRAME EVENT] Frame Received!
   Type: 0, Addr: 01, Ctrl: 03, Information Len: 5
[PASS] Heavy Stuffing

...

========================================
TEST: Broadcast Behavior
========================================
Testing Broadcast UI reception...
   [ON FRAME EVENT] Frame Received!
   Type: 2, Addr: FF, Ctrl: 03, Information Len: 9
[PASS] Broadcast UI received by application.
[PASS] Broadcast UI generated NO response.
...
[PASS] Broadcast Behavior

ALL TESTS PASSED SUCCESSFULLY!
```

## 📦 Integration

To use this library in your own project:

1.  Add `src/hdlc.c` and `src/hdlc_crc.c` to your build.
2.  Add `inc/` to your include path.
3.  **Define Callbacks**:
    ```c
    // Output: Called by the library for each byte to transmit
    void my_output_byte(atc_hdlc_u8 byte, atc_hdlc_bool flush, void *user_data) {
        UART_SendByte(byte);
        if (flush) {
           // Optional: Flush hardware buffer if needed
           // UART_Flush();
        }
    }

    // On Frame: Called by the library when a valid frame is received
    void my_on_frame(const atc_hdlc_frame_t *frame, void *user_data) {
        process_frame(frame->address, frame->control.value,
                      frame->information, frame->information_len);
    }

    // On State Change: Called when the connection state changes
    void my_on_state(atc_hdlc_protocol_state_t state, void *user_data) {
        switch(state) {
            case ATC_HDLC_PROTOCOL_STATE_CONNECTED:
                printf("Connected!\n");
                break;
            case ATC_HDLC_PROTOCOL_STATE_DISCONNECTED:
                printf("Disconnected!\n");
                break;
             // ...
        }
    }
    ```

4.  **Initialize the context**:
    ```c
    atc_hdlc_context_t ctx;
    uint8_t rx_buffer[256];
    uint8_t retransmit_buffer[512]; // For reliable TX (divided into window_size slots)

    atc_hdlc_init(&ctx,
        rx_buffer, sizeof(rx_buffer),           // Input buffer
        retransmit_buffer, sizeof(retransmit_buffer), // Retransmit buffer
        HDLC_DEFAULT_RETRANSMIT_TIMEOUT_MS,     // T1 timeout (1000ms default)
        HDLC_DEFAULT_WINDOW_SIZE,               // Window size (1 = Stop-and-Wait)
        my_output_byte, my_on_frame, my_on_state, NULL);
    
    // Configure Addresses (My Address, Peer Address)
    atc_hdlc_configure_addresses(&ctx, 0x01, 0x02);

    // Initiate Connection
    atc_hdlc_connect(&ctx);
    ```

5.  **Feed received bytes into the parser**:

    > ⚠️ **ISR Safety**: `atc_hdlc_input_byte` performs an **O(N) CRC verification loop** when the closing flag (`0x7E`) is received and also invokes the user `rx_cb` callback synchronously. **Do NOT call directly from a high-frequency ISR.** Use a Ring Buffer to decouple reception from processing.

    ```c
    // ISR: Just push bytes into a ring buffer
    void UART_RX_ISR(void) {
        uint8_t byte = UART_ReadByte();
        ring_buffer_push(&rx_buf, byte);
    }

    // Main Loop: Process bytes safely (single byte)
    void main_loop(void) {
        uint8_t byte;
        while (ring_buffer_pop(&rx_buf, &byte)) {
            atc_hdlc_input_byte(&ctx, byte);
        }
    }

    // Or use bulk input for DMA / batch transfers
    void process_dma_buffer(uint8_t *buf, uint32_t len) {
        atc_hdlc_input_bytes(&ctx, buf, len);
    }
    ```

6.  **Packet Mode (Buffered)**:
    Construct a frame structure and let the library handle transmission via the `output_cb`.
    ```c
    atc_hdlc_frame_t frame = {
        .address = 0xFF,
        .control.value = 0x00,      // I-Frame
        .information_len = 4
    };
    memcpy(frame.information, "TEST", 4);
    atc_hdlc_output_frame(&ctx, &frame);
    ```

7.  **Packet Mode (Zero-Copy)**:
    For memory-constrained devices where allocating a full frame buffer is not feasible:
    ```c
    // Start: sends Flag + Address + Control (with CRC init)
    atc_hdlc_output_packet_start(&ctx, 0x01, 0x03);

    // Data: byte-by-byte or array (stuffing handled automatically)
    atc_hdlc_output_packet_information_byte(&ctx, 0xAA);
    uint8_t payload[] = {0x10, 0x20, 0x30};
    atc_hdlc_output_packet_information_bytes(&ctx, payload, 3);

    // End: sends CRC + Flag
    atc_hdlc_output_packet_end(&ctx);
    ```

8.  **Stateless Mode (Pack)**:
    Useful when you need to serialize a frame into a buffer without using the library's context or callbacks.
    ```c
    uint8_t buffer[128];
    uint32_t len = 0;
    atc_hdlc_frame_t frame = { ... }; // Setup frame fully
    
    if (atc_hdlc_frame_pack(&frame, buffer, sizeof(buffer), &len)) {
        // buffer now contains the encoded frame (Flags + Stuffing + CRC)
        // e.g. HAL_UART_Transmit(&huart1, buffer, len, 100);
    }
    ```

9.  **Stateless Mode (Unpack)**:
    Useful when you have a raw buffer containing a full frame and want to parse it without maintaining a receiver state machine.
    ```c
    uint8_t raw_buffer[] = {0x7E, 0xFF, ... , 0x7E}; // Received raw data
    atc_hdlc_frame_t frame;
    uint8_t flat_buffer[128]; // Destination for decoded data
    
    // Decodes, verifies CRC, unstuffs, and populates 'frame'
    if (atc_hdlc_frame_unpack(raw_buffer, sizeof(raw_buffer), &frame, flat_buffer, sizeof(flat_buffer))) {
        // Frame is valid!
        process_frame(frame.address, frame.control.value, frame.information, frame.information_len);
    }
    ```

## ⚙️ Configuration

Configuration is done in `inc/hdlc_config.h`:

| Parameter | Default | Description |
|---|---|---|
| `ATC_HDLC_PREFIX` | `atc_` | Symbol prefix for all public API functions and types |
| `HDLC_DEFAULT_RETRANSMIT_TIMEOUT_MS` | `1000` | Default retransmission (T1) timeout in milliseconds |
| `HDLC_DEFAULT_WINDOW_SIZE` | `1` | Default transmit window size for Go-Back-N (1..7) |

## 📖 API Reference

### Packet Mode (Formatted & Zero-Copy)

| Function | Description |
|---|---|
| `atc_hdlc_init()` | Initialize context and bind callbacks |
| `atc_hdlc_input_byte()` | Feed a single received byte into the parser |
| `atc_hdlc_input_bytes()` | Feed a byte array into the parser (bulk) |
| `atc_hdlc_output_frame()` | Send a complete frame (buffered) |
| `atc_hdlc_output_packet_start()` | Begin packet TX (Flag + Address + Control) |
| `atc_hdlc_output_packet_information_byte()` | Send a single data byte (with stuffing) |
| `atc_hdlc_output_packet_information_bytes()` | Send a data array (with stuffing) |
| `atc_hdlc_output_packet_end()` | Finalize packet TX (CRC + Flag) |
| `atc_hdlc_send_ui()` | Send unacknowledged data (UI Frame) |
| `atc_hdlc_send_test()` | Send a TEST frame with optional data payload |

### Reliable Transmission (Go-Back-N)

| Function | Description |
|---|---|
| `atc_hdlc_output_i()` | Send a reliable I-frame (queued in the send window) |
| `atc_hdlc_tick()` | Periodic timer tick — handles retransmission timeouts |

### Connection Management

| Function | Description |
|---|---|
| `atc_hdlc_configure_addresses()` | Set source and destination addresses |
| `atc_hdlc_connect()` | Initiate connection (sends SABM) |
| `atc_hdlc_disconnect()` | Terminate connection (sends DISC) |
| `atc_hdlc_is_connected()` | Check if currently connected |

### Stateless Mode

| Function | Description |
|---|---|
| `atc_hdlc_frame_pack()` | Pack (serialize) a frame into a memory buffer |
| `atc_hdlc_frame_unpack()` | Unpack (deserialize) a raw frame from a memory buffer |

### Control Field Helpers

| Function | Description |
|---|---|
| `atc_hdlc_create_i_ctrl(ns, nr, pf)` | Create I-Frame control byte |
| `atc_hdlc_create_s_ctrl(s_bits, nr, pf)` | Create S-Frame control byte |
| `atc_hdlc_create_u_ctrl(m_lo, m_hi, pf)` | Create U-Frame control byte |

### Callback Signatures

```c
typedef void (*atc_hdlc_output_byte_cb_t)(atc_hdlc_u8 byte, atc_hdlc_bool flush, void *user_data);
typedef void (*atc_hdlc_on_frame_cb_t)(const atc_hdlc_frame_t *frame, void *user_data);
typedef void (*atc_hdlc_on_state_change_cb_t)(atc_hdlc_protocol_state_t state, void *user_data);
```
