# HDLC Library — Architecture Conformance Refactor Plan

> **Reference document:** `hdlc_architecture_context.md` (ISO/IEC 13239, ABM mode, C99, bare-metal/RTOS/Linux embedded)
>
> **Goal:** Bring the existing implementation into full conformance with the architectural design document.
>
> **Conventions:**
> - All source code comments: **English**
> - All documentation: **Doxygen-compatible**
> - Zero dynamic memory allocation (invariant — never break this)
> - All phases must compile and pass existing tests before the next phase begins

---

## Decisions Made

| # | Topic | Decision |
|---|-------|----------|
| 1 | Directory structure | Physical split into `src/frame/` and `src/station/` subdirectories |
| 2 | `peer_address` | Provided at connect time (`atc_hdlc_link_setup(ctx, peer_addr)`), not in config |
| 3 | Output callback strategy | Keep byte-based streaming as primary path; add error return to callback (`void` → `int`). Optional buffer-based send as secondary path (Phase 11) |
| 4 | `retransmit_lens` / `tx_seq_to_slot` arrays | Injected by the user as pointers (part of `atc_hdlc_tx_window_t`) |
| 5 | Test updates | Incremental — only update tests broken by the current phase |

---

## Include Dependency Map (Pre-Refactor)

```
inc/hdlc_config.h
    └── inc/hdlc_types.h
            └── inc/hdlc.h
                    ├── src/hdlc_private.h      (also includes hdlc_types.h directly)
                    │       └── (used by all src/*.c)
                    └── src/hdlc_crc.h          (also includes hdlc_types.h directly)
                            └── src/hdlc_crc.c

src/hdlc.c              → hdlc.h, hdlc_private.h
src/hdlc_input.c        → hdlc.h, hdlc_crc.h, hdlc_private.h
src/hdlc_output.c       → hdlc.h, hdlc_crc.h, hdlc_private.h
src/hdlc_frame.c        → hdlc.h, hdlc_crc.h, hdlc_private.h
src/hdlc_frame_handlers.c → hdlc.h, hdlc_private.h

test/*.c                → ../inc/hdlc.h
test/test_hdlc.c        → ../inc/hdlc.h, ../src/hdlc_private.h   ← path changes in Phase 0
test/test_reliable_transmission.c → same
test/test_connection_management.c → same
```

---

## Phase Status Legend

| Symbol | Meaning |
|--------|---------|
| ⬜ | Pending |
| 🔄 | In Progress |
| ✅ | Complete |
| ❌ | Blocked |

---

## PHASE 0 — Directory Reorganisation ✅

**Goal:** Reflect the two-layer architecture (Frame + Station) in the physical directory structure.

**Status:** `COMPLETE`

### New Directory Structure

```
src/
├── frame/
│   ├── hdlc_frame.c          (moved from src/)
│   ├── hdlc_crc.c            (moved from src/)
│   └── hdlc_crc.h            (moved from src/)
├── station/
│   ├── hdlc_station.c        (renamed from src/hdlc.c)
│   ├── hdlc_input.c          (moved from src/)
│   ├── hdlc_output.c         (moved from src/)
│   └── hdlc_frame_handlers.c (moved from src/)
├── hdlc_private.h            (stays at src/ root — shared by both layers)
└── CMakeLists.txt            (updated)
```

### Include Path Changes After Move

| File | Old include | New include |
|------|-------------|-------------|
| `src/frame/hdlc_frame.c` | `"../inc/hdlc.h"` | `"../../inc/hdlc.h"` |
| `src/frame/hdlc_frame.c` | `"hdlc_crc.h"` | `"hdlc_crc.h"` (same dir) |
| `src/frame/hdlc_frame.c` | `"hdlc_private.h"` | `"../hdlc_private.h"` |
| `src/frame/hdlc_crc.c` | `"hdlc_crc.h"` | `"hdlc_crc.h"` (same dir) |
| `src/frame/hdlc_crc.h` | `"../inc/hdlc_types.h"` | `"../../inc/hdlc_types.h"` |
| `src/station/hdlc_station.c` | `"../inc/hdlc.h"` | `"../../inc/hdlc.h"` |
| `src/station/hdlc_station.c` | `"hdlc_private.h"` | `"../hdlc_private.h"` |
| `src/station/hdlc_input.c` | `"../inc/hdlc.h"` | `"../../inc/hdlc.h"` |
| `src/station/hdlc_input.c` | `"hdlc_crc.h"` | `"../frame/hdlc_crc.h"` |
| `src/station/hdlc_input.c` | `"hdlc_private.h"` | `"../hdlc_private.h"` |
| `src/station/hdlc_output.c` | `"../inc/hdlc.h"` | `"../../inc/hdlc.h"` |
| `src/station/hdlc_output.c` | `"hdlc_crc.h"` | `"../frame/hdlc_crc.h"` |
| `src/station/hdlc_output.c` | `"hdlc_private.h"` | `"../hdlc_private.h"` |
| `src/station/hdlc_frame_handlers.c` | `"../inc/hdlc.h"` | `"../../inc/hdlc.h"` |
| `src/station/hdlc_frame_handlers.c` | `"hdlc_private.h"` | `"../hdlc_private.h"` |
| `src/hdlc_private.h` | `"../inc/hdlc_types.h"` | `"../inc/hdlc_types.h"` (unchanged) |
| `test/test_hdlc.c` | `"../src/hdlc_private.h"` | `"../src/hdlc_private.h"` (unchanged) |
| `test/test_reliable_transmission.c` | same | (unchanged) |
| `test/test_connection_management.c` | same | (unchanged) |

### Tasks
- [x] Create `src/frame/` and `src/station/` directories
- [x] Move/rename files
- [x] Update all `#include` paths listed above
- [x] Update `src/CMakeLists.txt` (add subdirectories, update source list)
- [x] Verify build: `cmake --build` — **PASS** (clean build, 0 errors)
- [x] Verify all existing tests pass — **3/3 PASS** (HDLC_Unit_Tests, ConnectionManagementTest, ReliableTransmissionTest)

### Files Changed
`src/CMakeLists.txt`, all `src/*.c`, `src/hdlc_crc.h`, `src/hdlc_private.h`

### Breaking Changes
None — public API (`inc/`) is untouched.

---

## PHASE 1 — Core Type System ✅

**Goal:** Define all new structural types required by the architecture document. No existing code is changed — only additive.

**Status:** `COMPLETE`

### New Types in `inc/hdlc_types.h`

#### Error Code Enum
```c
typedef enum {
    ATC_HDLC_OK                      =  0,
    /* Frame errors */
    ATC_HDLC_ERR_FCS                 = -1,
    ATC_HDLC_ERR_SHORT_FRAME         = -2,
    /* Resource errors */
    ATC_HDLC_ERR_BUFFER_FULL         = -3,
    ATC_HDLC_ERR_NO_BUFFER           = -4,
    /* Protocol errors */
    ATC_HDLC_ERR_SEQUENCE            = -5,
    ATC_HDLC_ERR_INVALID_COMMAND     = -6,
    ATC_HDLC_ERR_FRMR                = -7,
    /* State errors */
    ATC_HDLC_ERR_INVALID_STATE       = -8,
    ATC_HDLC_ERR_UNSUPPORTED_MODE    = -9,
    /* Timing errors */
    ATC_HDLC_ERR_MAX_RETRY           = -10,
    /* Parameter errors */
    ATC_HDLC_ERR_INVALID_PARAM       = -11,
    ATC_HDLC_ERR_INCONSISTENT_BUFFER = -12,
    /* Flow control errors */
    ATC_HDLC_ERR_REMOTE_BUSY         = -13,
    ATC_HDLC_ERR_WINDOW_FULL         = -14,
    ATC_HDLC_ERR_FRAME_TOO_LARGE     = -15,
    ATC_HDLC_ERR_TEST_PENDING        = -16,
} atc_hdlc_error_t;
```

#### Configuration Struct
```c
typedef struct {
    atc_hdlc_link_mode_t mode;       /**< Operating mode (first version: ABM only) */
    atc_hdlc_u8          address;    /**< Local station address */
    atc_hdlc_u8          window_size;    /**< Sliding window size, 1-7 */
    atc_hdlc_u32         max_frame_size; /**< Maximum information field size (MRU), in octets */
    atc_hdlc_u8          max_retries;    /**< N2: max retransmission count before link failure */
    atc_hdlc_u32         t1_ms;      /**< Retransmission timer (ms) */
    atc_hdlc_u32         t2_ms;      /**< Acknowledgement delay timer (ms), must be < t1_ms */
    atc_hdlc_u32         t3_ms;      /**< Idle/keep-alive timer (ms) */
    atc_hdlc_bool        use_extended; /**< Extended (mod-128) mode flag; must be false in v1 */
} atc_hdlc_config_t;
```

#### Platform Integration Struct
```c
/** @brief Byte-output callback. Returns 0 on success, negative on error. */
typedef int (*atc_hdlc_send_fn)(atc_hdlc_u8 byte, atc_hdlc_bool flush, void *user_ctx);

/** @brief Verified payload delivery callback. */
typedef void (*atc_hdlc_on_data_fn)(const atc_hdlc_u8 *payload,
                                     atc_hdlc_u16 len, void *user_ctx);

/** @brief Asynchronous event notification callback. */
typedef void (*atc_hdlc_on_event_fn)(atc_hdlc_event_t event, void *user_ctx);

typedef struct {
    atc_hdlc_send_fn     send;     /**< Physical byte transmission (error-returning) */
    atc_hdlc_on_data_fn  on_data;  /**< Payload delivery to upper layer */
    atc_hdlc_on_event_fn on_event; /**< State/error/diagnostic event notification */
    void                *user_ctx; /**< Opaque pointer passed to all callbacks */
} atc_hdlc_platform_t;
```

#### TX Window Buffer Descriptor
```c
typedef struct {
    atc_hdlc_u8  *slots;       /**< User-allocated slot array (slot_count * slot_capacity octets) */
    atc_hdlc_u32 *slot_lens;   /**< Per-slot payload length array (slot_count elements, user-alloc) */
    atc_hdlc_u8  *seq_to_slot; /**< Sequence-number-to-slot mapping (window_size elements, user-alloc) */
    atc_hdlc_u32  slot_capacity; /**< Capacity of each slot in octets (must be >= max_frame_size) */
    atc_hdlc_u8   slot_count;    /**< Number of slots (must equal window_size) */
} atc_hdlc_tx_window_t;
```

#### RX Buffer Descriptor
```c
typedef struct {
    atc_hdlc_u8  *buffer;   /**< User-allocated receive buffer */
    atc_hdlc_u32  capacity; /**< Buffer capacity in octets (must be >= max_frame_size) */
} atc_hdlc_rx_buffer_t;
```

#### Statistics Struct
```c
typedef struct {
    /* Transmission */
    atc_hdlc_u32 tx_i_frames;           /**< I-frames transmitted */
    atc_hdlc_u32 tx_bytes;              /**< Information bytes transmitted */
    /* Reception */
    atc_hdlc_u32 rx_i_frames;           /**< I-frames received and accepted */
    atc_hdlc_u32 rx_bytes;              /**< Information bytes received */
    /* Errors */
    atc_hdlc_u32 fcs_errors;            /**< Frames discarded due to FCS mismatch */
    atc_hdlc_u32 frmr_count;            /**< FRMR frames received */
    atc_hdlc_u32 timeout_count;         /**< T1 timeout occurrences */
    /* Flow control */
    atc_hdlc_u32 rej_sent;              /**< REJ frames sent */
    atc_hdlc_u32 rej_received;          /**< REJ frames received */
    atc_hdlc_u32 rnr_sent;              /**< RNR frames sent */
    atc_hdlc_u32 rnr_received;          /**< RNR frames received */
    atc_hdlc_u32 local_busy_transitions; /**< Local busy state entry count */
    /* Diagnostics */
    atc_hdlc_u32 test_sent;             /**< TEST frames sent */
    atc_hdlc_u32 test_success;          /**< TEST frames with matching response */
    atc_hdlc_u32 test_failed;           /**< TEST frames that timed out or mismatched */
} atc_hdlc_stats_t;
```

#### TEST Result Struct
```c
typedef struct {
    atc_hdlc_bool success;     /**< true if peer echoed the correct payload */
    atc_hdlc_bool timed_out;   /**< true if T1 expired before a response arrived */
    atc_hdlc_u16  payload_len; /**< Length of the test pattern that was sent */
} atc_hdlc_test_result_t;
```

### Extended State Enum (replaces existing 4-state enum)
```c
typedef enum {
    ATC_HDLC_STATE_DISCONNECTED,   /**< No logical connection */
    ATC_HDLC_STATE_CONNECTING,     /**< SABM sent, waiting for UA; T1 running */
    ATC_HDLC_STATE_CONNECTED,      /**< Active data transfer */
    ATC_HDLC_STATE_FRMR_ERROR,     /**< Irrecoverable protocol error; only reset/disconnect valid */
    ATC_HDLC_STATE_DISCONNECTING,  /**< DISC sent, waiting for UA; T1 running */
} atc_hdlc_state_t;
/* Sub-conditions within CONNECTED are context boolean flags:
 *   remote_busy    — peer sent RNR; TX I-frames suspended
 *   local_busy     — local RNR sent; peer TX throttled
 *   rej_exception  — REJ sent; Go-Back-N retransmission in progress
 */
```

### Extended Event Enum (additive)
```c
/* Added to existing atc_hdlc_event_t: */
ATC_HDLC_EVENT_RESET,           /**< Link was reset (SABM re-sent after internal reset) */
ATC_HDLC_EVENT_REMOTE_BUSY_ON,  /**< Peer sent RNR; outgoing data suspended */
ATC_HDLC_EVENT_REMOTE_BUSY_OFF, /**< Peer sent RR after RNR; outgoing data resumed */
ATC_HDLC_EVENT_WINDOW_OPEN,     /**< TX window slot freed; application may send again */
ATC_HDLC_EVENT_TEST_RESULT,     /**< TEST frame round-trip complete (see test_result field) */
```

### Tasks
- [x] Add `atc_hdlc_error_t` to `hdlc_types.h`
- [x] Add `atc_hdlc_config_t` to `hdlc_types.h`
- [x] Add `atc_hdlc_platform_t` + callback typedefs to `hdlc_types.h`
- [x] Add `atc_hdlc_tx_window_t` and `atc_hdlc_rx_buffer_t` to `hdlc_types.h`
- [x] Add `atc_hdlc_stats_t` to `hdlc_types.h`
- [x] Add `atc_hdlc_test_result_t` to `hdlc_types.h`
- [x] Replace 4-state enum with 8-state `atc_hdlc_state_t` in `hdlc_types.h`; legacy `atc_hdlc_protocol_state_t` kept as typedef alias + macro shims for backward compat
- [x] Extend event enum with new events (`RESET`, `REMOTE_BUSY_ON/OFF`, `WINDOW_OPEN`, `TEST_RESULT`)
- [x] Update `atc_hdlc_context_t` with new pointer fields, `stats`, `test_result`, `remote_busy`, `local_busy`, `test_pending`, `t3_timer`; old flat fields kept (`@deprecated`) until Phase 2
- [x] Migrate `ctx->stats_*` flat fields → `ctx->stats.*` in all `src/station/*.c`
- [x] Migrate state enum values in `src/station/*.c`, `src/hdlc_private.h`, affected test files
- [x] Verify build: **PASS** (0 errors, 0 new warnings)
- [x] Verify tests: **3/3 PASS**

### Files Changed
`inc/hdlc_types.h`

### Breaking Changes
Enum rename: `atc_hdlc_protocol_state_t` → `atc_hdlc_state_t`; values renamed. Tests that reference state values must be updated.

---

## PHASE 2 — Init / Reset Refactor ✅

**Goal:** Replace the 13-parameter `atc_hdlc_init()` with a clean struct-based signature. Return error codes. Add all consistency checks.

**Status:** `COMPLETE`

### New Public API Signatures

```c
/**
 * @brief Initialise an HDLC station context.
 *
 * @param ctx       Station context (user-allocated, zero-initialised by this call).
 * @param config    Protocol configuration (must remain valid for the lifetime of ctx).
 * @param platform  Platform integration callbacks (must remain valid for the lifetime of ctx).
 * @param tx_window TX retransmit window descriptor (NULL disables reliable I-frame TX).
 * @param rx_buf    RX buffer descriptor.
 * @return ATC_HDLC_OK on success, negative error code otherwise.
 */
atc_hdlc_error_t atc_hdlc_init(atc_hdlc_context_t        *ctx,
                                 const atc_hdlc_config_t   *config,
                                 const atc_hdlc_platform_t *platform,
                                 atc_hdlc_tx_window_t      *tx_window,
                                 atc_hdlc_rx_buffer_t      *rx_buf);

/**
 * @brief Initiate a connection with the peer (sends SABM).
 *
 * @param ctx       Initialised station context.
 * @param peer_addr Remote station address.
 * @return ATC_HDLC_OK or ATC_HDLC_ERR_INVALID_STATE / ATC_HDLC_ERR_UNSUPPORTED_MODE.
 */
atc_hdlc_error_t atc_hdlc_link_setup(atc_hdlc_context_t *ctx, atc_hdlc_u8 peer_addr);
```

### Consistency Checks in `atc_hdlc_init()`
1. `ctx != NULL`, `config != NULL`, `platform != NULL`, `rx_buf != NULL` → `ERR_INVALID_PARAM`
2. `config->mode == ATC_HDLC_MODE_ABM` → else `ERR_UNSUPPORTED_MODE`
3. `config->use_extended == false` → else `ERR_UNSUPPORTED_MODE`
4. `config->window_size` in `[1, 7]` → else `ERR_INVALID_PARAM`
5. `platform->send != NULL` → else `ERR_INVALID_PARAM`
6. `rx_buf->buffer != NULL && rx_buf->capacity >= config->max_frame_size` → else `ERR_INCONSISTENT_BUFFER`
7. If `tx_window != NULL`:
   - `tx_window->slots != NULL` → else `ERR_INCONSISTENT_BUFFER`
   - `tx_window->slot_lens != NULL` → else `ERR_INCONSISTENT_BUFFER`
   - `tx_window->seq_to_slot != NULL` → else `ERR_INCONSISTENT_BUFFER`
   - `tx_window->slot_count == config->window_size` → else `ERR_INCONSISTENT_BUFFER`
   - `tx_window->slot_capacity >= config->max_frame_size` → else `ERR_INCONSISTENT_BUFFER`

### Removed Functions
- `atc_hdlc_configure_station()` — absorbed into `atc_hdlc_config_t` + `atc_hdlc_link_setup(peer_addr)`

### Context Struct Changes
- Remove: `output_byte_cb`, `on_frame_cb`, `on_state_change_cb`, `user_data` (free-standing fields)
- Remove: `retransmit_buffer`, `retransmit_buffer_len`, `retransmit_slot_size`, `input_buffer`, `input_buffer_len`
- Remove: `retransmit_lens[8]`, `tx_seq_to_slot[8]`
- Remove: `ack_delay_timeout`, `retransmit_timeout` (now in `config->t2_ms`, `config->t1_ms`)
- Remove: `stats_input_frames`, `stats_output_frames`, `stats_crc_errors`
- Add: `const atc_hdlc_config_t *config`
- Add: `const atc_hdlc_platform_t *platform`
- Add: `atc_hdlc_tx_window_t *tx_window`
- Add: `atc_hdlc_rx_buffer_t *rx_buf`
- Add: `atc_hdlc_stats_t stats`
- Add: `atc_hdlc_test_result_t test_result`
- Add: `atc_hdlc_u8 next_tx_slot` (stays; managed internally)
- Keep: all timer counter fields (`ack_timer`, `retransmit_timer`, `contention_timer`)
- Keep: `vs`, `vr`, `va`, `window_size` (cache from config for hot-path speed)
- Keep: `input_state`, `input_index`, `rej_exception`, `retry_count`
- Keep: `output_crc`, `current_state`, `peer_address`

### Tasks
- [x] Refactor `atc_hdlc_context_t` — all deprecated fields removed
- [x] Rewrite `atc_hdlc_init()` with new signature + full consistency checks
- [x] Remove `atc_hdlc_configure_station()` from API and implementation
- [x] Update `atc_hdlc_link_setup()` to accept `peer_addr`, add state pre-condition guard
- [x] Update `atc_hdlc_disconnect()` return type: `bool` → `atc_hdlc_error_t`
- [x] Update `atc_hdlc_output_frame_i/ui/test()` return types: `bool` → `atc_hdlc_error_t`
- [x] Remove old callback typedefs; platform integration via `atc_hdlc_platform_t`
- [x] Update all internal `ctx->` field accesses in all `src/station/*.c` and `src/frame/*.c`
- [x] Add new public API: `atc_hdlc_link_reset()`, `atc_hdlc_set_local_busy()`, query functions
- [x] Update `test_common.c/h`: new `mock_send_cb`, `mock_on_data_cb`, `setup_test_context()` with new init
- [x] Update all test files to new API (init, link_setup, callbacks, return types)
- [x] Verify build: **PASS** (0 errors)
- [x] Verify tests: **3/3 PASS**

### Files Changed
`inc/hdlc_types.h`, `inc/hdlc.h`, `src/station/hdlc_station.c`, `src/station/hdlc_input.c`,
`src/station/hdlc_output.c`, `src/station/hdlc_frame_handlers.c`,
`test/test_common.c`, `test/test_common.h`, and the three test files above.

### Breaking Changes
**High impact.** All callers of `atc_hdlc_init()` and `atc_hdlc_configure_station()` must be updated.

---

## PHASE 3a — Coding Convention Cleanup ✅

**Goal:** Unify naming conventions across the entire codebase following Linux LAPB patterns.

**Status:** `COMPLETE`

### Naming Convention (adopted)

| Scope | Rule | Example |
|---|---|---|
| File names — RX/TX | `_in` / `_out` suffix | `hdlc_in.c`, `hdlc_out.c` |
| Public RX API | `atc_hdlc_data_in*` | `atc_hdlc_data_in`, `atc_hdlc_data_in_bytes` |
| Public TX API | `atc_hdlc_transmit_*` | `atc_hdlc_transmit_i`, `atc_hdlc_transmit_ui` |
| Platform struct field | `.on_send` | consistent with `.on_data`, `.on_event` |
| Context RX fields | `rx_` prefix | `rx_state`, `rx_index`, `rx_frame` |
| Context TX fields | `tx_` prefix | `tx_crc` |
| Timer fields | `t1_timer`, `t2_timer`, `t3_timer` | replaces `retransmit_timer`, `ack_timer` |
| Internal functions | `hdlc_` prefix | `hdlc_process_complete_frame`, `hdlc_pack_escaped` |
| Internal macros | `HDLC_` prefix | `HDLC_FLAG_LEN`, `HDLC_MIN_FRAME_LEN` |
| Internal ctrl constructors | `hdlc_create_*` | `hdlc_create_i_ctrl`, `hdlc_create_u_ctrl` |
| RX state enum | `hdlc_rx_state_t` / `HDLC_RX_STATE_*` | replaces `hdlc_input_state_t` |

### Renames Applied

**Files:** `hdlc_input.c` → `hdlc_in.c`, `hdlc_output.c` → `hdlc_out.c`

**Public API:**
- `atc_hdlc_input_byte/bytes` → `atc_hdlc_data_in / atc_hdlc_data_in_bytes`
- `atc_hdlc_output_frame_i/ui/test` → `atc_hdlc_transmit_i/ui/test`
- `atc_hdlc_output_frame_start/end/information_byte/bytes` → `atc_hdlc_transmit_start/end/data_byte/data_bytes`
- `atc_hdlc_output_frame_start_ui/test` → `atc_hdlc_transmit_start_ui/test`
- `atc_hdlc_platform_t::send` → `::on_send`

**Context struct:**
- `input_frame_buffer` → `rx_frame`
- `input_index` → `rx_index`
- `input_state` → `rx_state`
- `output_crc` → `tx_crc`
- `ack_timer` → `t2_timer`
- `retransmit_timer` → `t1_timer`

**Internal (src/ only):**
- `process_complete_frame` → `hdlc_process_complete_frame`
- `output_byte_to_callback` → `hdlc_write_byte`
- `pack_escaped` → `hdlc_pack_escaped`
- `pack_escaped_crc_update` → `hdlc_pack_escaped_crc`
- `frame_pack_core` → `hdlc_frame_pack_core`
- `atc_hdlc_output_frame` (internal) → `hdlc_transmit_frame`
- `atc_hdlc_create_*_ctrl` → `hdlc_create_*_ctrl`
- `hdlc_input_state_t` / `HDLC_INPUT_STATE_*` → `hdlc_rx_state_t` / `HDLC_RX_STATE_*`

**Private header macros:**
- `ATC_HDLC_FLAG_LEN / ADDRESS_LEN / CONTROL_LEN / FCS_LEN` → `HDLC_*`
- `ATC_HDLC_MIN_FRAME_LEN` → `HDLC_MIN_FRAME_LEN`
- `ATC_HDLC_FRAME_TYPE_MASK_* / VAL_*` → `HDLC_FRAME_TYPE_MASK_* / VAL_*`

### Tasks
- [x] `git mv` file renames
- [x] Update all `#include` references
- [x] Apply all renames in `src/`, `inc/`, `test/`
- [x] Update `src/CMakeLists.txt`
- [x] Build: **PASS** (0 errors)
- [x] Tests: **3/3 PASS** (test_hdlc, test_connection_management, test_reliable_transmission)

### Files Changed
All `src/station/*.c`, `src/frame/hdlc_frame.c`, `src/hdlc_private.h`, `inc/hdlc.h`, `inc/hdlc_types.h`, `src/CMakeLists.txt`, all `test/*.c`

---

## PHASE 3b — Timer Architecture Refactor ✅

**Goal:** Replace the tick-based timer model with platform-driven start/stop callbacks and explicit expiry entry points, following Linux LAPB `lapb_start_t1timer` / `lapb_stop_t1timer` pattern.

**Status:** `COMPLETE`

### Design

The tick model requires the application to call `atc_hdlc_tick()` periodically, which couples the library to a fixed time base and prevents use of hardware timers or OS timer APIs. The new model inverts control: the library calls `t1_start(ms)` / `t1_stop()` at the right moments, and the platform fires `atc_hdlc_t1_expired()` when the timer elapses.

```
Library:                          Platform:
  t1_start(t1_ms) ─────────────► OS/HW timer starts
                                  ... (t1_ms elapses) ...
  atc_hdlc_t1_expired(ctx) ◄──── timer callback
```

### New Platform Callbacks (added to `atc_hdlc_platform_t`)

```c
typedef void (*atc_hdlc_timer_start_fn)(atc_hdlc_u32 ms, void *user_ctx);
typedef void (*atc_hdlc_timer_stop_fn)(void *user_ctx);

/* In atc_hdlc_platform_t: */
atc_hdlc_timer_start_fn t1_start; /**< Start T1 retransmission timer (mandatory if I-frames used). */
atc_hdlc_timer_stop_fn  t1_stop;  /**< Stop T1. */
atc_hdlc_timer_start_fn t2_start; /**< Start T2 delayed-ACK timer. */
atc_hdlc_timer_stop_fn  t2_stop;  /**< Stop T2. */
atc_hdlc_timer_start_fn t3_start; /**< Start T3 idle/keep-alive timer. */
atc_hdlc_timer_stop_fn  t3_stop;  /**< Stop T3. */
```

All timer callbacks are optional (NULL = timer not used).

### New Public API

```c
void atc_hdlc_t1_expired(atc_hdlc_context_t *ctx);
void atc_hdlc_t2_expired(atc_hdlc_context_t *ctx);
void atc_hdlc_t3_expired(atc_hdlc_context_t *ctx);
```

### Removed Public API

```c
void atc_hdlc_tick(atc_hdlc_context_t *ctx);           /* removed */
atc_hdlc_u32 atc_hdlc_get_next_timeout_ticks(...);     /* removed */
```

### Internal helpers (in hdlc_private.h)

```c
static inline void hdlc_t1_start(atc_hdlc_context_t *ctx);
static inline void hdlc_t1_stop(atc_hdlc_context_t *ctx);
static inline void hdlc_t2_start(atc_hdlc_context_t *ctx);
static inline void hdlc_t2_stop(atc_hdlc_context_t *ctx);
static inline void hdlc_t3_start(atc_hdlc_context_t *ctx);
static inline void hdlc_t3_stop(atc_hdlc_context_t *ctx);
```

### Tasks
- [x] Add timer typedef + 6 callback fields to `atc_hdlc_platform_t`
- [x] Remove `t1_timer`, `t2_timer`, `t3_timer` countdown fields from context
- [x] Add `hdlc_t1_start/stop`, `hdlc_t2_start/stop`, `hdlc_t3_start/stop` inline helpers to `hdlc_private.h`
- [x] Implement `atc_hdlc_t1_expired()`, `atc_hdlc_t2_expired()`, `atc_hdlc_t3_expired()` in `hdlc_station.c`
- [x] Remove `atc_hdlc_tick()` and `atc_hdlc_get_next_timeout_ticks()` from API and implementation
- [x] Replace all `ctx->t1_timer = ...` / countdown logic with `hdlc_t1_start/stop` calls
- [x] Update `atc_hdlc_has_pending_ack()` — T2 active state tracked via boolean flag
- [x] Update all test files: thread loops simulate timers via periodic `atc_hdlc_t1/t2_expired()` calls
- [x] Build: **PASS**
- [x] Tests: **4/4 PASS** (including test_virtual_com)

### Files Changed
`inc/hdlc_types.h`, `inc/hdlc.h`, `src/hdlc_private.h`, `src/station/hdlc_station.c`,
`src/station/hdlc_out.c`, `src/station/hdlc_frame_handlers.c`, all `test/*.c`

---

## PHASE 4 — State Machine Expansion ⬜

**Goal:** Implement FRMR lock-down and transition guards. Sub-conditions within
CONNECTED (remote busy, local busy, reject-recovery) are modelled as boolean
flags in the context, not as separate states — consistent with ISO/IEC 13239.

**Status:** `PENDING`

### State Model (5 states)

```
DISCONNECTED → CONNECTING → CONNECTED → DISCONNECTING → DISCONNECTED
                                ↕ (lock-down)
                           FRMR_ERROR
```

Sub-conditions within CONNECTED (tracked by context boolean flags):
- `remote_busy`   — peer sent RNR; TX I-frames suspended.
- `local_busy`    — local RNR sent; peer TX throttled.
- `rej_exception` — REJ sent; Go-Back-N retransmission in progress.

### State Transition Rules

| From | Event | To | Notes |
|------|-------|----|-------|
| DISCONNECTED | `link_setup()` | CONNECTING | Sends SABM(P=1), starts T1 |
| CONNECTING | UA received | CONNECTED | Resets V(S)/V(R)/V(A) |
| CONNECTING | SABM received | CONNECTED | Contention winner; sends UA |
| CONNECTING | DM received | DISCONNECTED | Peer rejected |
| CONNECTING | T1 expires ≤ N2 | CONNECTING | SABM retry + T1 restart |
| CONNECTING | T1 expires > N2 | DISCONNECTED | Link failure event |
| CONNECTED | RNR received | CONNECTED | Sets `remote_busy` flag |
| CONNECTED | RR received (after RNR) | CONNECTED | Clears `remote_busy` flag |
| CONNECTED | `set_local_busy(true)` | CONNECTED | Sets `local_busy`, sends RNR |
| CONNECTED | `set_local_busy(false)` | CONNECTED | Clears `local_busy`, sends RR |
| CONNECTED | I-frame OOS | CONNECTED | Sets `rej_exception`, sends REJ |
| CONNECTED | V(A) advances past REJ | CONNECTED | Clears `rej_exception` |
| CONNECTED | FRMR received | FRMR_ERROR | Lock-down |
| CONNECTED | DISC received | DISCONNECTED | Sends UA, resets state |
| CONNECTED | `disconnect()` | DISCONNECTING | Sends DISC(P=1), starts T1 |
| FRMR_ERROR | `link_reset()` | CONNECTING | Only valid operation |
| FRMR_ERROR | `disconnect()` | DISCONNECTING | Only valid operation |
| FRMR_ERROR | anything else | FRMR_ERROR | Returns `ERR_INVALID_STATE` |
| DISCONNECTING | UA received | DISCONNECTED | |
| DISCONNECTING | T1 expires ≤ N2 | DISCONNECTING | DISC retry + T1 restart |
| DISCONNECTING | T1 expires > N2 | DISCONNECTED | Link failure event |

### Tasks
- [ ] Update `hdlc_set_protocol_state()` to enforce transition guards
- [ ] `hdlc_process_frmr()`: transition to `STATE_FRMR_ERROR` (was: `DISCONNECTED`)
- [ ] Add `FRMR_ERROR` guard in all public API entry points
- [ ] Update `atc_hdlc_is_connected()`: return true only for `CONNECTED`
- [ ] Update all references to old state enum values in tests

### Files Changed
`inc/hdlc_types.h`, `src/station/hdlc_station.c`, `src/station/hdlc_frame_handlers.c`,
`inc/hdlc.h`, test files (enum value references).

---

## PHASE 4 — T3 Timer + T1 Connecting/Disconnecting Retry ⬜

**Goal:** Add the missing T3 idle/keep-alive timer. Extend T1 to cover CONNECTING and DISCONNECTING states.

**Status:** `PENDING`

### Timer Behaviour Summary

| Timer | Trigger | Action on Expiry |
|-------|---------|-----------------|
| T1 | Send of SABM / I-frame with outstanding ACK / DISC | Retry (SABM or DISC) or Go-Back-N enquiry RR(P=1). Max N2 → link failure |
| T2 | Receipt of in-sequence I-frame | Send standalone RR (piggyback window expired) |
| T3 | Reset on every received frame | Send RR(P=1) keep-alive, start T1 |

### New Context Fields
```c
atc_hdlc_u32 t3_timer;   /**< Countdown for idle keep-alive (T3) */
```

### New Public API
```c
/**
 * @brief Query the time until the nearest timer expiry.
 *
 * Allows tickless/low-power schedulers to sleep for the exact duration
 * before calling atc_hdlc_tick().
 *
 * @param ctx Initialised station context.
 * @return Milliseconds until next expiry, or UINT32_MAX if no active timers.
 */
atc_hdlc_u32 atc_hdlc_get_next_timeout_ms(const atc_hdlc_context_t *ctx);
```

### `atc_hdlc_tick()` Extended Logic
```
if CONNECTING:
    T1 countdown → if expired: retry SABM or fail
if DISCONNECTING:
    T1 countdown → if expired: retry DISC or fail
if T2 active:
    countdown → send standalone RR
if CONNECTED (any sub-condition flag):
    T3 countdown → if expired: send RR(P=1), start T1
    if outstanding frames:
        T1 countdown → retry enquiry
```

### Tasks
- [ ] Add `t3_timer` field to context
- [ ] Start T3 on entering CONNECTED
- [ ] Reset T3 at the top of `process_complete_frame()`
- [ ] Implement T3 expiry: send `RR(P=1)`, start T1
- [ ] Implement T1 in CONNECTING: start after `link_setup()`, retry SABM on expiry
- [ ] Implement T1 in DISCONNECTING: start after `disconnect()`, retry DISC on expiry
- [ ] Implement `atc_hdlc_get_next_timeout_ms()` in `hdlc_station.c`
- [ ] Add to public header `hdlc.h`
- [ ] Write timer-specific tests

### Files Changed
`inc/hdlc_types.h`, `inc/hdlc.h`, `src/station/hdlc_station.c`, `src/station/hdlc_input.c`

---

## PHASE 5 — Remote Busy + Local Busy + RNR ⬜

**Goal:** Fully implement flow control. RNR sending and receiving.

**Status:** `PENDING`

### Context Fields (already added in Phase 1)
```c
atc_hdlc_bool remote_busy; /**< true when peer sent RNR and has not yet sent RR */
atc_hdlc_bool local_busy;  /**< true when local RX resources are exhausted */
```
Both flags are sub-conditions of CONNECTED; the station state remains
CONNECTED while either flag is set.

### New Public API
```c
/**
 * @brief Notify the station of a local busy condition.
 *
 * When @p busy is true, the station sets local_busy and responds to incoming
 * I-frames with RNR instead of RR. The payload is still delivered to on_data
 * but peer transmission is throttled.
 * When @p busy is false, local_busy is cleared and RR is sent to resume peer.
 * The station remains in CONNECTED throughout; only the flag changes.
 *
 * @param ctx  Initialised station context.
 * @param busy true to assert local busy, false to clear it.
 * @return ATC_HDLC_OK or ATC_HDLC_ERR_INVALID_STATE.
 */
atc_hdlc_error_t atc_hdlc_set_local_busy(atc_hdlc_context_t *ctx, atc_hdlc_bool busy);
```

### Behaviour Changes
- **S-frame handler — RNR received:** Set `remote_busy = true`, fire `EVENT_REMOTE_BUSY_ON` (state stays CONNECTED)
- **S-frame handler — RR received:** If `remote_busy`, clear it, fire `EVENT_REMOTE_BUSY_OFF` (state stays CONNECTED)
- **I-frame handler — `local_busy` active:** Send RNR response instead of RR; do NOT deliver payload via `on_data`
- **`atc_hdlc_output_frame_i()` — `remote_busy` active:** Return `ERR_REMOTE_BUSY` immediately
- **`atc_hdlc_set_local_busy(false)`:** Send `RR(F=0)` to resume peer

### Tasks
- [ ] Add `remote_busy`, `local_busy` to context
- [ ] RNR receive logic in `handle_s_frame()`
- [ ] RR receive logic (clear remote_busy) in `handle_s_frame()`
- [ ] Local busy RNR response in `handle_i_frame()`
- [ ] `atc_hdlc_set_local_busy()` implementation
- [ ] `remote_busy` guard in `atc_hdlc_output_frame_i()`
- [ ] `EVENT_REMOTE_BUSY_ON/OFF` notifications
- [ ] Tests

### Files Changed
`inc/hdlc_types.h`, `inc/hdlc.h`, `src/station/hdlc_frame_handlers.c`, `src/station/hdlc_output.c`

---

## PHASE 6 — FRMR Sending + Link Reset API ⬜

**Goal:** Send FRMR on protocol violations. Add user-accessible link reset.

**Status:** `PENDING`

### FRMR Sending Triggers
| Condition | FRMR reason bit |
|-----------|----------------|
| Invalid/unimplemented control field | W |
| Info field present on frame that disallows it | X |
| Info field exceeds `max_frame_size` | Y |
| Invalid N(R) (out of V(A)..V(S) window) | Z |

### New Internal Helper
```c
/* In hdlc_private.h */
void hdlc_send_frmr(atc_hdlc_context_t *ctx,
                    atc_hdlc_u8 rejected_ctrl,
                    atc_hdlc_bool w, atc_hdlc_bool x,
                    atc_hdlc_bool y, atc_hdlc_bool z);
```

### New Public API
```c
/**
 * @brief Reset the link and re-establish connection.
 *
 * Performs an internal state reset followed by a new SABM transmission.
 * This is the primary recovery mechanism after an FRMR condition.
 * Valid in any state; fires EVENT_RESET.
 *
 * @param ctx Initialised station context.
 * @return ATC_HDLC_OK always.
 */
atc_hdlc_error_t atc_hdlc_link_reset(atc_hdlc_context_t *ctx);
```

### Tasks
- [ ] Implement `hdlc_send_frmr()` internal helper (info field: rejected ctrl + V(S)/V(R) + reason bits)
- [ ] Call `hdlc_send_frmr()` in `hdlc_process_nr()` for invalid N(R) (currently: log + ignore)
- [ ] Call `hdlc_send_frmr()` for info field on non-data frames
- [ ] Call `hdlc_send_frmr()` for info field exceeding `max_frame_size`
- [ ] Implement `atc_hdlc_link_reset()`
- [ ] `EVENT_RESET` notification in `atc_hdlc_link_reset()`
- [ ] Ensure FRMR_ERROR state rejects all ops except reset/disconnect
- [ ] Tests

### Files Changed
`inc/hdlc.h`, `src/hdlc_private.h`, `src/station/hdlc_frame_handlers.c`, `src/station/hdlc_station.c`

---

## PHASE 7 — TEST Frame Full Lifecycle ⬜

**Goal:** Implement the complete TEST frame round-trip as defined in §7 of the architecture document.

**Status:** `PENDING`

### New Context Fields
```c
atc_hdlc_bool  test_pending;      /**< true while awaiting TEST(F=1) response */
const atc_hdlc_u8 *test_pattern;  /**< Pointer to the sent test pattern (user-owned) */
atc_hdlc_u16   test_pattern_len;  /**< Length of the test pattern */
```

### Lifecycle
1. **Sender:** `atc_hdlc_output_frame_test()` → stores pattern ptr+len, sets `test_pending`, starts T1
2. **Receiver:** TEST(P=1) → echoes same info field as TEST(F=1); no state change
3. **Response processing:** TEST(F=1) + `test_pending` → compare payload → populate `test_result` → fire `EVENT_TEST_RESULT` → cancel T1 → clear `test_pending`
4. **Timeout:** T1 expires + `test_pending` → populate `test_result` (timeout=true) → fire `EVENT_TEST_RESULT` → clear `test_pending`

### Behaviour Changes to `atc_hdlc_output_frame_test()`
- If `test_pending` → return `ERR_TEST_PENDING`
- Store pattern, set flag, start T1

### Tasks
- [ ] Add `test_pending`, `test_pattern`, `test_pattern_len` to context
- [ ] Update `atc_hdlc_output_frame_test()` with new logic
- [ ] Update TEST(F=1) handler in `handle_u_frame()` to compare and report
- [ ] Update T1 expiry in `atc_hdlc_tick()` to check `test_pending`
- [ ] Fire `EVENT_TEST_RESULT` with `test_result` populated
- [ ] Tests (success, mismatch, timeout, double-send guard)

### Files Changed
`inc/hdlc_types.h`, `src/station/hdlc_output.c`, `src/station/hdlc_frame_handlers.c`, `src/station/hdlc_station.c`

---

## PHASE 8 — Event System Expansion + Status Query Functions ⬜

**Goal:** Complete the event notification model. Add all §6.6 query functions.

**Status:** `PENDING`

### New Public API (Query Functions)
```c
/** @brief Returns true if an unacknowledged I-frame is pending (piggyback opportunity). */
atc_hdlc_bool    atc_hdlc_has_pending_ack(const atc_hdlc_context_t *ctx);

/** @brief Returns the number of free TX window slots (0 = window full). */
atc_hdlc_u8      atc_hdlc_get_window_available(const atc_hdlc_context_t *ctx);

/** @brief Returns the current state machine state. */
atc_hdlc_state_t atc_hdlc_get_state(const atc_hdlc_context_t *ctx);

/** @brief Copies the current statistics snapshot into @p out. */
void             atc_hdlc_get_stats(const atc_hdlc_context_t *ctx, atc_hdlc_stats_t *out);
```

### `EVENT_WINDOW_OPEN` Notification
Fire in `hdlc_process_nr()` when at least one TX slot is freed (V(A) advances from full-window condition).

### Tasks
- [ ] Implement all four query functions in `hdlc_station.c`
- [ ] Add to `hdlc.h`
- [ ] `EVENT_WINDOW_OPEN` in `hdlc_process_nr()`
- [ ] Ensure all previously added events (`REMOTE_BUSY_ON/OFF`, `RESET`, `TEST_RESULT`) are wired correctly
- [ ] Tests

### Files Changed
`inc/hdlc.h`, `src/station/hdlc_station.c`, `src/station/hdlc_frame_handlers.c`

---

## PHASE 9 — Statistics Expansion + Compile-Time Configuration ⬜

**Goal:** Complete statistics instrumentation. Add compile-time feature toggles.

**Status:** `PENDING`

### New Compile-Time Macros in `hdlc_config.h`

```c
/** Set to 1 to use a 256-entry lookup table for FCS-16 (faster, +512 B ROM).
 *  Set to 0 for bit-by-bit computation (slower, minimal ROM).  */
#ifndef ATC_HDLC_FCS_USE_TABLE
#define ATC_HDLC_FCS_USE_TABLE 1
#endif

/** Set to 1 to collect runtime statistics in atc_hdlc_stats_t.
 *  Set to 0 to compile out all stat increments (zero overhead). */
#ifndef ATC_HDLC_ENABLE_STATS
#define ATC_HDLC_ENABLE_STATS 1
#endif

/** Set to 1 to enable internal assertion checks (debug builds).
 *  Set to 0 for release builds. */
#ifndef ATC_HDLC_ENABLE_ASSERT
#define ATC_HDLC_ENABLE_ASSERT 0
#endif
```

### Internal Helpers
```c
/* Stat increment wrapper — no-op when ENABLE_STATS=0 */
#if ATC_HDLC_ENABLE_STATS
  #define HDLC_STAT_INC(ctx, field)  ((ctx)->stats.field++)
  #define HDLC_STAT_ADD(ctx, field, n) ((ctx)->stats.field += (n))
#else
  #define HDLC_STAT_INC(ctx, field)  ((void)0)
  #define HDLC_STAT_ADD(ctx, field, n) ((void)0)
#endif

#if ATC_HDLC_ENABLE_ASSERT
  #include <assert.h>
  #define HDLC_ASSERT(cond) assert(cond)
#else
  #define HDLC_ASSERT(cond) ((void)0)
#endif
```

### Instrumentation Points
- `HDLC_STAT_INC(ctx, tx_i_frames)` + `HDLC_STAT_ADD(ctx, tx_bytes, len)` in `hdlc_output.c`
- `HDLC_STAT_INC(ctx, rx_i_frames)` + `HDLC_STAT_ADD(ctx, rx_bytes, len)` in `hdlc_frame_handlers.c`
- `HDLC_STAT_INC(ctx, fcs_errors)` in `hdlc_input.c`
- `HDLC_STAT_INC(ctx, frmr_count)` in FRMR handler
- `HDLC_STAT_INC(ctx, timeout_count)` in T1 expiry handler
- `HDLC_STAT_INC(ctx, rej_sent/received)` in REJ send/receive paths
- `HDLC_STAT_INC(ctx, rnr_sent/received)` in RNR send/receive paths
- `HDLC_STAT_INC(ctx, local_busy_transitions)` in `set_local_busy(true)`
- `HDLC_STAT_INC(ctx, test_sent/success/failed)` in TEST paths

### CRC Bitwise Alternative in `hdlc_crc.c`
```c
#if ATC_HDLC_FCS_USE_TABLE
    /* existing LUT implementation */
#else
    /* bit-by-bit CRC-16-CCITT (polynomial 0x8408, reflected input) */
#endif
```

### Tasks
- [ ] Add macros to `hdlc_config.h`
- [ ] Add `HDLC_STAT_INC/ADD` and `HDLC_ASSERT` macros to `hdlc_private.h`
- [ ] Replace raw `ctx->stats_*` increments with `HDLC_STAT_INC()` everywhere
- [ ] Add missing stat instrumentation at all points listed above
- [ ] Add `#if ATC_HDLC_FCS_USE_TABLE` branching to `hdlc_crc.c`
- [ ] Tests (verify stat counts)

### Files Changed
`inc/hdlc_config.h`, `src/hdlc_private.h`, `src/frame/hdlc_crc.c`, all `src/station/*.c`

---

## PHASE 10 — I-Frame Reception Fixes + Connect/Disconnect Preconditions ⬜

**Goal:** Correct remaining behavioural gaps in §6.4 and add state guards to connection management.

**Status:** `PENDING`

### I-Frame Reception Fixes

1. **Duplicate REJ guard:** In `handle_i_frame()`, out-of-sequence I-frame with `rej_exception == true` → do NOT send another REJ; silently discard.
2. **P=1 response:** Use `hdlc_send_response_rr()` (address = own address) not `hdlc_send_rr()` (address = peer) when responding to P=1 in I-frame handler.
3. **Local busy gate:** When `local_busy == true`, incoming I-frames with correct N(S) must:
   - Advance V(R) (sequence accepted)
   - Start T2
   - BUT respond with RNR instead of RR
   - AND still deliver payload via `on_data` (local busy throttles peer, not discard)

   > Note: Phase 5 adds the RNR response; this phase audits and confirms correctness.

### Connect / Disconnect Preconditions

| Function | Allowed states | Error if not |
|----------|---------------|-------------|
| `atc_hdlc_link_setup()` | DISCONNECTED | `ERR_INVALID_STATE` |
| `atc_hdlc_disconnect()` | CONNECTED, FRMR_ERROR | `ERR_INVALID_STATE` |
| `atc_hdlc_output_frame_i()` | CONNECTED (remote_busy caught by `ERR_REMOTE_BUSY` first) | `ERR_INVALID_STATE` |
| `atc_hdlc_link_reset()` | any | always allowed |

### Tasks
- [ ] Duplicate REJ guard in `handle_i_frame()`
- [ ] Fix P=1 response address in `handle_i_frame()`
- [ ] Audit local-busy I-frame path (payload delivery + RNR response)
- [ ] Add state precondition checks to `link_setup()`, `disconnect()`, `output_frame_i()`
- [ ] Tests

### Files Changed
`src/station/hdlc_frame_handlers.c`, `src/station/hdlc_output.c`, `src/station/hdlc_station.c`

---

## PHASE 11 — Optional Buffer-Based Output + Zero-Copy RX ⬜

**Goal:** Add the optional buffer-based send path and the zero-copy RX swap mechanism.

**Status:** `PENDING`

### Buffer-Based Send (Optional Second Path)

Add optional `send_buffer` callback to `atc_hdlc_platform_t`:
```c
/** Optional bulk-send callback. If non-NULL, frames are encoded into an
 *  intermediate buffer first and transmitted in a single call.
 *  If NULL, the byte-by-byte @c send path is used.                     */
atc_hdlc_error_t (*send_buffer)(const atc_hdlc_u8 *data,
                                 atc_hdlc_u32 len,
                                 void *user_ctx);

/** Intermediate encode buffer for the buffer-based send path.
 *  Must be >= (max_frame_size * 2 + 6) octets to accommodate worst-case stuffing.
 *  Ignored if send_buffer is NULL. */
atc_hdlc_u8  *encode_buf;
atc_hdlc_u32  encode_buf_capacity;
```

Selection logic in `frame_pack_core()`: if `platform->send_buffer != NULL` → encode to `encode_buf` → call `send_buffer()` on frame end.

### Zero-Copy RX Swap
```c
/**
 * @brief Swap the active RX buffer (zero-copy delivery).
 *
 * The caller takes ownership of the current RX buffer (which contains
 * a freshly delivered payload) and supplies a fresh empty buffer.
 * Must be called from within the @c on_data callback.
 *
 * @param ctx      Initialised station context.
 * @param new_buf  New buffer to activate.
 * @param new_cap  Capacity of the new buffer in octets (must be >= max_frame_size).
 * @return Pointer to the old buffer (now owned by the caller).
 */
atc_hdlc_u8 *atc_hdlc_swap_rx_buffer(atc_hdlc_context_t *ctx,
                                       atc_hdlc_u8 *new_buf,
                                       atc_hdlc_u32 new_cap);
```

### Tasks
- [ ] Add `send_buffer`, `encode_buf`, `encode_buf_capacity` to `atc_hdlc_platform_t`
- [ ] Selection logic in encode path
- [ ] Implement `atc_hdlc_swap_rx_buffer()`
- [ ] Add to `hdlc.h`
- [ ] Tests

### Files Changed
`inc/hdlc_types.h`, `inc/hdlc.h`, `src/frame/hdlc_frame.c`, `src/station/hdlc_output.c`, `src/station/hdlc_station.c`

---

## Progress Tracker

| Phase | Description | Status | Build | Tests |
|-------|-------------|--------|-------|-------|
| 0 | Directory reorganisation | ✅ Complete | PASS | 3/3 PASS |
| 1 | Core type system | ✅ Complete | PASS | 3/3 PASS |
| 2 | Init / reset refactor | ✅ Complete | PASS | 3/3 PASS |
| 3a | Coding convention cleanup | ✅ Complete | PASS | 3/3 PASS |
| 3b | Timer architecture refactor | ✅ Complete | PASS | 4/4 PASS |
| 4 | State machine expansion | ⬜ Pending | — | — |
| 5 | T3 timer (now: platform-driven) | ⬜ Pending | — | — |
| 6 | Remote busy / local busy | ⬜ Pending | — | — |
| 7 | FRMR sending + link reset | ⬜ Pending | — | — |
| 8 | TEST frame lifecycle | ⬜ Pending | — | — |
| 9 | Events + query functions | ⬜ Pending | — | — |
| 10 | Stats + compile-time config | ⬜ Pending | — | — |
| 11 | I-frame fixes + preconditions | ⬜ Pending | — | — |
| 12 | Buffer-based output + zero-copy RX | ⬜ Pending | — | — |

---

## Change Log

| Date | Phase | Change |
|------|-------|--------|
| 2026-03-15 | — | Initial plan created |
| 2026-03-15 | 0 | Directory reorganisation complete: `src/frame/`, `src/station/`, `hdlc.c` → `hdlc_station.c`, all include paths updated, clean build + 3/3 tests pass |
| 2026-03-15 | 1 | Core type system: atc_hdlc_error_t, atc_hdlc_state_t (5-state), config/platform/buffer/stats/test_result structs added; state machine model corrected |
| 2026-03-15 | 2 | Init/reset refactor: new `atc_hdlc_init()` (5-param struct-based), all consistency checks, deprecated fields removed from context, `configure_station` removed, `link_setup(peer_addr)`, all output_frame_* return `atc_hdlc_error_t`, platform callbacks via `atc_hdlc_platform_t`, new link_reset/set_local_busy/query APIs, all test files migrated; clean build + 3/3 tests pass |
| 2026-03-15 | 3a | Convention cleanup: `hdlc_in.c`/`hdlc_out.c` rename, `data_in*`/`transmit_*` API rename, `.on_send`, `rx_*`/`tx_*` context fields, `t1_timer`/`t2_timer`, `HDLC_` internal macros, `hdlc_` prefix on internal functions; clean build + 3/3 tests pass |
| 2026-03-15 | 3b | Timer refactor: `atc_hdlc_tick()` removed, `t1/t2/t3_start/stop` platform callbacks added, `atc_hdlc_t1/t2/t3_expired()` public API, all internal timer logic migrated, test thread loops updated; clean build + 4/4 tests pass |
