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
    *   **Buffered Mode**: Construct a full `atc_hdlc_frame_t` and send it in one call.
    *   **Streaming Mode (Zero-Copy)**: Send frames byte-by-byte (`start` → `data` → `end`) for ultra-low memory environments.
    *   **Buffer Encoding**: Encode a frame directly into a memory buffer without immediate transmission.
*   **Protocol Infrastructure**:
    *   Built-in support for I-Frames, S-Frames, and U-Frames with bit-field accessors.
    *   Control Field helper functions for constructing each frame type.
    *   Frame Type dispatcher (ready for ABM logic).
*   **Developer Experience**:
    *   Modern **CMake** build system (C99).
    *   **Unit tests** covering edge cases (byte stuffing, CRC errors, overflow, fragmentation, control field loopback, streaming API).
    *   **Configurable Symbol Prefix**: All public symbols are prefixed (default `atc_`) to avoid collisions. Changeable at compile time via `ATC_HDLC_PREFIX`.
    *   **C++ compatible** (`extern "C"` wrappers).

## 📂 Project Structure

```
.
├── inc/
│   ├── hdlc.h          # Public API (init, send, receive, streaming)
│   ├── hdlc_types.h    # Public types (frame, context, callbacks, control field)
│   └── hdlc_config.h   # Configuration (prefix, max frame length)
├── src/
│   ├── hdlc.c          # Core implementation (TX/RX engines, control helpers)
│   ├── hdlc_crc.c      # CRC-16-CCITT LUT and update function
│   ├── hdlc_crc.h      # Internal CRC API
│   └── hdlc_private.h  # Internal RX state machine definitions
├── test/
│   └── hdlc_test.c     # Unit test suite
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
STARTING COMPREHENSIVE HDLC TEST SUITE
----------------------------------------

========================================
TEST: Basic Frame (I-Frame)
========================================
TX Buffer (10 bytes): 7E FF 00 54 45 53 54 80 55 7E
   [RX EVENT] Frame Received!
   Type: 0, Addr: FF, Ctrl: 00, Information Len: 4
   Information: 54 45 53 54
[PASS] Basic Frame

========================================
TEST: Heavy Byte Stuffing
========================================
TX Buffer (Stuffed) (17 bytes): 7E 01 03 7D 5E 7D 5E 7D 5D 7D 5D 7D 5E 00 19 0C 7E
[PASS] Heavy Stuffing

...

========================================
TEST: Control Field - I-Frame Loopback
========================================
Generated I-Frame Ctrl Value: 0x7A
Received: Type=I, N(S)=5, N(R)=3, P/F=1
[PASS] I-Frame Loopback

ALL TESTS PASSED SUCCESSFULLY!
1/1 Test #1: HDLC_Unit_Tests ..................   Passed    0.00 sec
100% tests passed, 0 tests failed out of 1
```

## 📦 Integration

To use this library in your own project:

1.  Add `src/hdlc.c` and `src/hdlc_crc.c` to your build.
2.  Add `inc/` to your include path.
3.  **Define Callbacks**:
    ```c
    // TX: Called by the library for each byte to transmit
    void my_tx_byte(atc_hdlc_u8 byte, atc_hdlc_bool flush, void *user_data) {
        UART_SendByte(byte);
        if (flush) {
           // Optional: Flush hardware buffer if needed
           // UART_Flush();
        }
    }

    // RX: Called by the library when a valid frame is received
    void my_rx_frame(const atc_hdlc_frame_t *frame, void *user_data) {
        process_frame(frame->address, frame->control.value,
                      frame->information, frame->information_len);
    }
    ```

4.  **Initialize the context**:
    ```c
    atc_hdlc_context_t ctx;
    atc_hdlc_init(&ctx, my_tx_byte, my_rx_frame, NULL);
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

6.  **Send Data (Buffered Mode)**:
    ```c
    atc_hdlc_frame_t frame = {
        .address = 0xFF,
        .control.value = 0x00,      // I-Frame
        .information_len = 4
    };
    memcpy(frame.information, "TEST", 4);
    atc_hdlc_send_frame(&ctx, &frame);
    ```

7.  **Send Data (Streaming / Zero-Copy Mode)**:
    For memory-constrained devices where allocating a full frame buffer is not feasible:
    ```c
    // Start: sends Flag + Address + Control (with CRC init)
    atc_hdlc_send_packet_start(&ctx, 0x01, 0x03);

    // Data: byte-by-byte or array (stuffing handled automatically)
    atc_hdlc_send_packet_information_byte(&ctx, 0xAA);
    uint8_t payload[] = {0x10, 0x20, 0x30};
    atc_hdlc_send_packet_information_bytes_array(&ctx, payload, 3);

    // End: sends CRC + Flag
    atc_hdlc_send_packet_end(&ctx);
    ```

8.  **Encode to Buffer**:
    Useful when you need to send the frame via a different transport or store it.
    ```c
    uint8_t buffer[128];
    uint32_t len = 0;
    atc_hdlc_frame_t frame = { ... }; // Setup frame fully
    
    if (atc_hdlc_encode_frame(&frame, buffer, sizeof(buffer), &len)) {
        // buffer now contains the encoded frame (Flags + Stuffing + CRC)
        // e.g. HAL_UART_Transmit(&huart1, buffer, len, 100);
    }
    ```

9.  **Decode from Buffer**:
    Useful when you have a raw buffer containing a full frame (e.g. from a packet-based radio or DMA transfer) and want to parse it.
    ```c
    uint8_t raw_buffer[] = {0x7E, 0xFF, ... , 0x7E}; // Received raw data
    atc_hdlc_frame_t frame;
    uint8_t flat_buffer[128]; // Destination for decoded data
    
    // Decodes, verifies CRC, unstuffs, and populates 'frame'
    if (atc_hdlc_decode_frame(raw_buffer, sizeof(raw_buffer), &frame, flat_buffer, sizeof(flat_buffer))) {
        // Frame is valid!
        process_frame(frame.address, frame.control.value, frame.information, frame.information_len);
    }
    ```

## ⚙️ Configuration

Configuration is done in `inc/hdlc_config.h`:

| Parameter | Default | Description |
|---|---|---|
| `ATC_HDLC_PREFIX` | `atc_` | Symbol prefix for all public API functions and types |
| `HDLC_MAX_FRAME_LEN` | `256` | Maximum raw frame buffer size in bytes (including overhead) |

## 📖 API Reference

### Core Functions

| Function | Description |
|---|---|
| `atc_hdlc_init()` | Initialize context and bind callbacks |
| `atc_hdlc_input_byte()` | Feed a single received byte into the parser |
| `atc_hdlc_input_bytes()` | Feed a byte array into the parser (bulk) |
| `atc_hdlc_send_frame()` | Send a complete frame (buffered) |
| `atc_hdlc_encode_frame()` | Encode a frame into a memory buffer |
| `atc_hdlc_decode_frame()` | Decode a raw frame from a memory buffer |

### Streaming API

| Function | Description |
|---|---|
| `atc_hdlc_send_packet_start()` | Begin streaming TX (Flag + Address + Control) |
| `atc_hdlc_send_packet_information_byte()` | Send a single data byte (with stuffing) |
| `atc_hdlc_send_packet_information_bytes_array()` | Send a data array (with stuffing) |
| `atc_hdlc_send_packet_end()` | Finalize streaming TX (CRC + Flag) |

### Control Field Helpers

| Function | Description |
|---|---|
| `atc_hdlc_create_i_ctrl(ns, nr, pf)` | Create I-Frame control byte |
| `atc_hdlc_create_s_ctrl(s_bits, nr, pf)` | Create S-Frame control byte |
| `atc_hdlc_create_u_ctrl(m_lo, m_hi, pf)` | Create U-Frame control byte |

### Callback Signatures

```c
typedef void (*atc_hdlc_tx_byte_cb_t)(atc_hdlc_u8 byte, atc_hdlc_bool flush, void *user_data);
typedef void (*atc_hdlc_on_frame_cb_t)(const atc_hdlc_frame_t *frame, void *user_data);
```
