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
> - **NO CODE BLOAT** — minimal, goal-oriented implementation
> - **NO COMMENT BLOAT** — only add doxygen-style comments to public API headers if changes affect the user
> - **FUTURE-PROOF CODE** — future features must not break the existing API
> - **FOLLOW LINUX KERNEL LAPB** — without violating the above rules
> - **THINK LIKE LINUS TORVALDS** when implementing or planning

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

## PHASE 5 — T3 Timer + T1 Connecting/Disconnecting Retry ⬜

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

## PHASE 6 — Remote Busy + Local Busy + RNR ⬜

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

## PHASE 7 — FRMR Sending + Link Reset API ⬜

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

## PHASE 8 — TEST Frame Full Lifecycle ⬜

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

## PHASE 9 — Event System Expansion + Status Query Functions ⬜

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

## PHASE 10 — Statistics Expansion + Compile-Time Configuration ⬜

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

## PHASE 11 — I-Frame Reception Fixes + Connect/Disconnect Preconditions ⬜

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

## PHASE 12 — Optional Buffer-Based Output + Zero-Copy RX ⬜

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
| 5 | T3 timer (now: platform-driven) | ⬜ Pending | — | — |
| 6 | Remote busy / local busy | ⬜ Pending | — | — |
| 7 | FRMR sending + link reset | ⬜ Pending | — | — |
| 8 | TEST frame lifecycle | ⬜ Pending | — | — |
| 9 | Events + query functions | ⬜ Pending | — | — |
| 10 | Stats + compile-time config | ⬜ Pending | — | — |
| 11 | I-frame fixes + preconditions | ⬜ Pending | — | — |
| 12 | Buffer-based output + zero-copy RX | ⬜ Pending | — | — |
