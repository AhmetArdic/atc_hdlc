# HDLC-Like Embedded Protocol Stack

This project provides a high-performance, scalable HDLC (High-Level Data Link Control) protocol implementation designed for embedded systems, specifically targeting UART (Asynchronous) communication.

## 🚀 Features

*   **HDLC Framing**:
    *   Flag delimiting (`0x7E`).
    *   Robust Byte Stuffing (Escape `0x7E` -> `0x7D 0x5E`, `0x7D` -> `0x7D 0x5D`).
    *   Frame length validation (MTU checks).
*   **Data Integrity**:
    *   **CRC-16-CCITT** validation for all frames.
    *   Robust verification logic (Recalculate & Compare) tolerant of various transmission orders.
*   **Flexible Transmission Modes**:
    *   **Buffered Mode**: Send complete frames from a pre-allocated buffer.
    *   **Streaming Mode (Zero-Copy)**: Send frames byte-by-byte (Start -> Data -> End) to minimize memory usage on constrained devices.
*   **Protocol Infrastructure**:
    *   Built-in support for standard HDLC Frame types (I-Frame, S-Frame, U-Frame).
    *   Dispatcher architecture ready for Asynchronous Balanced Mode (ABM) logic.
*   **Build System**:
    *   Modern **CMake** build system for easy integration and cross-platform compilation.
    *   Comprehensive **Unit Tests** covering edge cases (Stuffing, CRC errors, Overflows).

## 📂 Project Structure

```
.
├── inc/            # Public Header files (hdlc.h, hdlc_types.h, hdlc_config.h)
├── src/            # Source implementation (hdlc.c, hdlc_crc.c)
├── test/           # Unit tests (hdlc_test.c) and CMake test config
├── CMakeLists.txt  # Root CMake configuration
└── README.md       # This file
```

## 🛠️ Build & Run Instructions

### Prerequisites
*   C Compiler (GCC recommended)
*   CMake (Version 3.10 or higher)

### How to Compile
1.  Create a build directory:
    ```bash
    mkdir build && cd build
    ```
2.  Configure the project with CMake:
    ```bash
    cmake ..
    ```
3.  Compile the library and tests:
    ```bash
    make
    ```

### How to Run Tests
This project includes a comprehensive test suite to verify framing, parsing, and CRC logic.

Run the tests using `ctest`:
```bash
ctest --verbose
```

### Example Output
```text
test 1
    Start 1: HDLC_Unit_Tests

1: Test command: .../hdlc_test
1: === Test: Basic Frame ===
1: PASS
1: === Test: Byte Stuffing (Edge Case) ===
1: PASS
...
100% tests passed, 0 tests failed out of 1
```

## 📦 Integration

To use this library in your own project:
1.  Add the `src/` files to your build.
2.  Add `inc/` to your include path.
3.  Initialize the context:
    ```c
    hdlc_context_t ctx;
    hdlc_init(&ctx, my_tx_callback, my_rx_callback, NULL);
    ```
4.  Feed received bytes into the parser:
    ```c
    void UART_ISR(uint8_t byte) {
        hdlc_input_byte(&ctx, byte);
    }
    ```
