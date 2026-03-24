# ATC HDLC — Improvement Plan

References: Linux Kernel `net/lapb/`, Wireshark HDLC dissector, ISO 13239.

Design principles: Linux Kernel LAPB style — minimal, direct bit manipulation, no over-abstraction, Linus Torvalds coding style.

---

## Bug Fixes

**B2 — `atc_hdlc_link_reset()` bypasses state callback**
- `hdlc_station.c` has two problems: a standalone `hdlc_fire_event(ctx, ATC_HDLC_EVENT_RESET)`
  followed later by a direct `ctx->current_state = ATC_HDLC_STATE_CONNECTING` assignment.
  Together these fire the event without transitioning state properly, and the direct assignment
  skips T1/T2 stop logic in `hdlc_set_protocol_state()`.
- Fix: Remove both the standalone `hdlc_fire_event()` call and the direct state assignment.
  Insert one `hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_CONNECTING, ATC_HDLC_EVENT_RESET)`
  after `hdlc_t1_start(ctx)`.
- Files: `src/station/hdlc_station.c`

**B3 — FRMR info byte 1: C/R bit never set**
- `HDLC_FRMR_CR_BIT` (0x10) is defined in `src/hdlc_private.h` but hardcoded to 0 in
  `hdlc_send_frmr()`. ISO 13239 Table 10 requires this bit to reflect the C/R state of the
  rejected frame.
- Fix: Derive `cr` from `ctx->rx_frame.address == ctx->my_address` inside `hdlc_send_frmr()`
  (no new parameter needed — the function is only ever called while processing a received frame,
  so `ctx->rx_frame` is always valid). Use existing constants `HDLC_FRMR_CR_BIT`,
  `HDLC_FRMR_VR_SHIFT`, `HDLC_FRMR_VS_SHIFT` from `src/hdlc_private.h`.
- Files: `src/station/hdlc_in.c`

**B4 — FRMR state: no periodic resend (spec violation)**
- ISO 13239 §5.5.3 requires repeatedly retransmitting FRMR under T1 until a SABM or DISC is
  received. Currently `atc_hdlc_t1_expired()` has no `case ATC_HDLC_STATE_FRMR_ERROR:` — the
  station goes silent.
- Fix:
  1. Add `atc_hdlc_u8 frmr_info[3]` to `atc_hdlc_context_t` (cache the FRMR payload).
  2. In `hdlc_send_frmr()`: populate `ctx->frmr_info`, then replace `hdlc_t1_stop()` with
     `hdlc_t1_start()` so T1 begins ticking for periodic retransmit. Keep `hdlc_t2_stop()`.
  3. In `atc_hdlc_t1_expired()`: add `case ATC_HDLC_STATE_FRMR_ERROR:` — retransmit
     `ctx->frmr_info`, restart T1. The existing N2 check applies (acceptable simplification).
- Files: `inc/hdlc_types.h`, `src/station/hdlc_station.c`, `src/station/hdlc_in.c`
- Dependency: do B3 first so the cached bytes are correct.

**B5 — `on_send` return value silently discarded**
- `hdlc_write_byte()` calls `on_send()` and ignores the return value. If the UART transmit
  buffer is full, the library silently drops bytes mid-frame, producing a frame with invalid FCS.
- Fix:
  1. In `hdlc_write_byte()`: if `on_send()` returns non-zero, set `enc_ctx->ctx->tx_error = true`.
  2. Add `bool tx_error` to `atc_hdlc_context_t`.
  3. `atc_hdlc_transmit_start()` clears `ctx->tx_error`.
  4. `atc_hdlc_transmit_i/ui/test()` check `ctx->tx_error` after `transmit_end()` and return
     `ATC_HDLC_ERR_SEND_FAILED = -17`.
- Files: `inc/hdlc_types.h`, `src/frame/hdlc_frame.c`, `src/station/hdlc_out.c`

---

## Architecture

**A3 — Per-state compilation units**
- The current `src/station/hdlc_in.c` is monolithic (~600 lines): it contains both the byte
  parser and all 5 state handlers. Split as follows:
  - `src/station/hdlc_in.c` — keep only the byte-parser (`hdlc_data_in`, `atc_hdlc_data_in`)
  - `src/station/hdlc_in.c` — keep only: `hdlc_process_complete_frame()`,
    `hdlc_reset_connection_state()`, `hdlc_send_frmr()`
  - `src/station/hdlc_state_disconnected.c`
  - `src/station/hdlc_state_connecting.c`
  - `src/station/hdlc_state_disconnecting.c`
  - `src/station/hdlc_state_connected.c` — includes `hdlc_process_nr()` + `hdlc_retransmit_go_back_n()`
  - `src/station/hdlc_state_frmr.c`
  - Add `src/station/hdlc_states.h` with the 5 internal function prototypes plus shared
    static inline helpers (`frame_u_type`, `hdlc_enter_frmr_state`).
  - Update `src/CMakeLists.txt`.
- Embedded gain: linker can dead-strip whole compilation units.
- Dependency: do after A1 + A2.

**A4 — Complete the C/R role model**
- `atc_hdlc_station_role_t` enum exists in `hdlc_types.h` but no `role` field in
  `atc_hdlc_context_t`. State handlers use raw `frame->address == ctx->my_address` ad-hoc.
- Fix:
  1. Add `atc_hdlc_station_role_t role` to `atc_hdlc_context_t`.
  2. `atc_hdlc_init()` sets `ctx->role = ATC_HDLC_ROLE_COMBINED`.
  3. Add `hdlc_is_cmd()` static inline to `src/hdlc_private.h`:
     `return frame->address == ctx->my_address;`
  4. Replace all ad-hoc address comparisons with `hdlc_is_cmd(ctx, frame)`.
- Files: `inc/hdlc_types.h`, `src/hdlc_private.h`, `src/station/hdlc_in.c`,
  `src/station/hdlc_station.c`

---

## Embedded Robustness

**E1 — Configurable log sink (remove direct `printf`)**
- `ATC_HDLC_LOG_*` macros call `printf` directly, linking stdio (~10-40KB on Cortex-M0).
- Fix: Add to `inc/hdlc_config.h` inside the `#ifndef` guard so user-supplied overrides skip
  the `<stdio.h>` include entirely:
  ```c
  #ifndef ATC_HDLC_LOG_IMPL
  #  include <stdio.h>
  #  define ATC_HDLC_LOG_IMPL(level, fmt, ...) printf("[HDLC %s] " fmt "\n", level, ##__VA_ARGS__)
  #endif
  ```
  Rewrite the macros in `src/hdlc_private.h` to delegate to `ATC_HDLC_LOG_IMPL`.
- Files: `inc/hdlc_config.h`, `src/hdlc_private.h`

**E2 — Compiler warning flags**
- Add to `src/CMakeLists.txt`:
  ```cmake
  target_compile_options(atc_hdlc PRIVATE -Wall -Wextra -Wshadow -Wconversion)
  ```
  Note: `-Wpedantic` is intentionally omitted — the log macros use `##__VA_ARGS__` which is
  a GCC/Clang extension flagged by strict pedantic mode.
  Fix surfaced warnings (primarily signed/unsigned in modulo arithmetic and FCS byte splits)
  by adding explicit `(atc_hdlc_u8)` casts.
- Files: `src/CMakeLists.txt`, several `.c` files for cast fixes.

**E3 — ISR/thread safety contract**
- Add a documentation block at the top of `inc/hdlc.h` covering:
  - `atc_hdlc_data_in()` — NOT ISR-safe
  - `atc_hdlc_t1_expired()`, `atc_hdlc_t2_expired()` — NOT ISR-safe
  - `atc_hdlc_transmit_*()` — NOT ISR-safe
  - `atc_hdlc_get_state/stats()` — read-only, safe if not concurrently written
- Files: `inc/hdlc.h`

**E4 — `atc_hdlc_abort()` API**
- No API exists for immediate abort. Needed when UART hardware detects a line break or framing
  error.
- Fix: Add `void atc_hdlc_abort(atc_hdlc_context_t *ctx)`:
  sends `0x7E 0x7E` to force peer into HUNT state; stops all timers; calls
  `hdlc_reset_connection_state()`; sets `ctx->rx_state = HDLC_RX_STATE_HUNT`; sets
  `ctx->current_state = ATC_HDLC_STATE_DISCONNECTED` directly (no event — intentional,
  abort is unconditional and unilateral).
- Files: `inc/hdlc.h`, `src/station/hdlc_station.c`

**E5 — Replace `atc_hdlc_bool` with `bool` in `src/`**
- All `src/` files use `bool`, `true`, `false` directly. Leave `inc/hdlc_types.h` typedef
  unchanged (`typedef bool atc_hdlc_bool` remains for public API compatibility).
- Files: `src/hdlc_private.h` and all `.c` files under `src/`.

---

## Features

**F1 — Enhanced statistics**
- Add to `atc_hdlc_stats_t` in `inc/hdlc_types.h`:
  ```c
  atc_hdlc_u32 t1_expiry_count;        /* increment in atc_hdlc_t1_expired() */
  atc_hdlc_u32 retry_exhaustion_count; /* increment when N2 exceeded */
  atc_hdlc_u32 sabm_collision_count;   /* increment in hdlc_state_connecting on SABM rx */
  atc_hdlc_u32 window_full_count;      /* increment in atc_hdlc_transmit_i() */
  atc_hdlc_u32 tx_s_frames;            /* increment in hdlc_send_s_frame() inline */
  atc_hdlc_u32 tx_u_frames;            /* increment in hdlc_send_u_frame() inline + hdlc_send_frmr() */
  atc_hdlc_u32 rx_s_frames;            /* increment in hdlc_process_complete_frame() */
  atc_hdlc_u32 rx_u_frames;            /* increment in hdlc_process_complete_frame() */
  atc_hdlc_u32 rx_out_of_sequence;     /* increment in hdlc_state_connected on OOS I-frame */
  ```
- Files: `inc/hdlc_types.h`, `src/hdlc_private.h`, `src/station/hdlc_in.c`,
  `src/station/hdlc_station.c`, `src/station/hdlc_out.c`

**F4 — Promiscuous / monitor mode**
- Gated behind `#define ATC_HDLC_ENABLE_PROMISCUOUS 0` in `inc/hdlc_config.h`.
- Add `bool promiscuous` to `atc_hdlc_context_t`.
- Add `atc_hdlc_on_sniff_fn on_sniff` callback to `atc_hdlc_platform_t` (after `on_event`).
- Add `atc_hdlc_set_promiscuous(ctx, enable)` to public API, guarded with
  `#if ATC_HDLC_ENABLE_PROMISCUOUS`.
- In `hdlc_in.c` address filter: skip rejection when `ctx->promiscuous` is set.
- Call `on_sniff` for every complete valid frame when promiscuous is enabled.
- Files: `inc/hdlc_config.h`, `inc/hdlc_types.h`, `inc/hdlc.h`, `src/station/hdlc_in.c`,
  `src/station/hdlc_station.c`

---

## Out of Scope

- **F2 — Extended frame format (mod-128)**
- **F3 — SREJ (Selective Reject)**

---

## Execution Order

```
Step 1 (Bugs):        B3 → B4 → B2 → B5      (B1 already done)
Step 2 (Architecture): A1 + A2 (together) → A3 → A4
Step 3 (Robustness):  E1 → E2 → E3 → E4 → E5
Step 4 (Features):    F1 → F4
```

---

## Critical Files

| File | Touches |
|------|---------|
| `inc/hdlc_types.h` | B4 (frmr_info), B5 (tx_error, error code), A1 (remove frame.type), A4 (role field), F1 (stats fields), F4 (on_sniff_fn, promiscuous) |
| `inc/hdlc_config.h` | E1 (ATC_HDLC_LOG_IMPL + stdio.h), F4 (ATC_HDLC_ENABLE_PROMISCUOUS) |
| `inc/hdlc.h` | E3, E4, F4 |
| `src/station/hdlc_in.c` | A1, A3 (byte-parser only after split), A4, F4 |
| `src/station/hdlc_in.c` | B3, B4, A1, A2, A3, A4, F1 |
| `src/station/hdlc_station.c` | B2, B4, A4, E4, F4 |
| `src/station/hdlc_out.c` | B5, F1 |
| `src/hdlc_private.h` | A1, A2, A4, E1, E5, F1 |
| `src/frame/hdlc_frame.c` | A1, A2, B5 |
| `src/CMakeLists.txt` | A3 (new files), E2 (warning flags) |
| `test/test_hdlc.c` | A1 (remove frame.type usage) |
| `test/test_reliable_transmission.c` | A1 (remove frame.type usage) |
| `test/test_connection_management.c` | A1 (remove frame.type usage) |
