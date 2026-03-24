# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ATC HDLC is a lightweight, portable HDLC (High-Level Data Link Control) protocol implementation for embedded systems with UART serial communication. It implements a subset of ISO/IEC 13239, providing:
- Binary-safe framing (flag bytes 0x7E, byte-stuffing)
- CRC-16-CCITT data integrity
- Asynchronous Balanced Mode (ABM) with Go-Back-N sliding window (modulo-8, up to 7 outstanding frames)
- UI frames (connectionless), TEST frames (link verification)

## Build & Test

```bash
# Build
mkdir build && cd build
cmake ..
make

# Run all tests
cd build && ctest --verbose

# Run a single test binary directly
cd build && ./test_hdlc
```

Test binaries: `test_hdlc`, `test_connection_management`, `test_reliable_transmission`, `test_init_validation`, `test_error_codes`, `test_virtual_com`, `test_physical_target`.

## Architecture

Three layers:

**Public API** (`inc/`): `hdlc.h` (23 functions, `atc_hdlc_` prefix), `hdlc_types.h` (types, callbacks, error codes), `hdlc_config.h` (compile-time defaults overridable via `#define`).

**Core Implementation** (`src/`):
- `src/frame/` — Stateless frame pack/unpack (`hdlc_frame.c`) and CRC-16 lookup table (`hdlc_crc.c`).
- `src/station/` — Stateful protocol engine:
  - `hdlc_station.c` — Init, link setup/disconnect, state transitions
  - `hdlc_in.c` — Byte-by-byte RX parser (byte-stuffing removal, CRC validation, reassembly)
  - `hdlc_out.c` — TX streaming engine (byte-stuffing, frame construction, flush)
  - `hdlc_frame_handlers.c` — Frame type dispatch and protocol logic (I/S/U frame handling)

**Private internals** (`src/hdlc_private.h`): Internal constants, bit-field accessors, RX state machine definitions.

### Data Flow

```
RX: UART byte → atc_hdlc_data_in() → hdlc_in.c (accumulate + unstuff)
    → CRC check → hdlc_frame_handlers.c → on_data / on_event callbacks

TX: atc_hdlc_transmit_i/ui/test() → hdlc_out.c (stuff + CRC)
    → on_send(byte, flush) callback → UART

Timers: atc_hdlc_t1_expired() (retransmit), atc_hdlc_t2_expired() (delayed ACK),
        atc_hdlc_t3_expired() (keepalive poll)
```

### Key Design Decisions

- **Callback-based platform abstraction**: `on_send`, `on_data`, `on_event`, and timer start/stop callbacks are bound at init. The library never calls `malloc` or platform I/O directly.
- **Zero-copy TX window**: Retransmit buffer is a user-provided contiguous memory block divided into `window_size` slots. `seq_to_slot[]` maps sequence numbers to slots for Go-Back-N recovery.
- **Config struct lifetime**: The `atc_hdlc_config_t*` passed at init must remain valid for the lifetime of the context.

## Ongoing Refactor (`refactor/phases` branch)

The project is being refactored toward Linux Kernel LAPB-style minimalism (see `doc/refactor_plan.md`):

- Replace complex frame-type enums with hex `#define` macros (`#define ATC_HDLC_CMD_SABM 0x2F`)
- Replace `hdlc_resolve_frame_type()` parser with direct `switch(ctrl & ~PF_BIT)` inline logic
- Replace monolithic `hdlc_frame_handlers.c` with per-state functions: `hdlc_state0_machine` (Disconnected), `hdlc_state1_machine` (Connecting), `hdlc_state2_machine` (Disconnecting), `hdlc_state3_machine` (Connected), `hdlc_state4_machine` (FRMR Error)
- Add `role` field (Primary/Secondary/Combined) to context for future NRM/ARM mode support
- **Public API (`atc_hdlc_` prefix) must not change**; only internal `src/` code is being restructured
