# ATC HDLC — Improvement Plan

References: Linux Kernel `net/lapb/`, Wireshark HDLC dissector, ISO 13239.

Design principles: Linux Kernel LAPB style — minimal, direct bit manipulation, no over-abstraction, Linus Torvalds coding style.

---

## Bug Fixes

**B1 — `tx_i_frames` triple-count (stat corruption)**
- `HDLC_STAT_INC(ctx, tx_i_frames)` fires in three places:
  1. `hdlc_transmit_frame()` — dead code, never called in the streaming path
  2. `atc_hdlc_transmit_end()` — fires for every frame type (UA, RR, FRMR…), not just I-frames
  3. `atc_hdlc_transmit_i()` — the only correct location
- Fix: Remove increments from (1) and (2). Remove `hdlc_transmit_frame()` entirely (dead code).
- Add `HDLC_STAT_INC(ctx, tx_i_frames)` in `hdlc_retransmit_go_back_n()` loop (retransmits are also I-frames).
- Files: `src/station/hdlc_out.c`, `src/hdlc_private.h`

**B2 — `atc_hdlc_link_reset()` bypasses state callback**
- Sets `ctx->current_state = ATC_HDLC_STATE_CONNECTING` directly, skipping `hdlc_set_protocol_state()`. The `on_event` callback is not fired for the transition, and T3 stop logic does not run.
- Fix: Replace direct assignment with `hdlc_set_protocol_state(ctx, ATC_HDLC_STATE_CONNECTING, ATC_HDLC_EVENT_RESET)`.
- Files: `src/station/hdlc_station.c`

**B3 — FRMR info byte 1: C/R bit never set**
- `HDLC_FRMR_CR_BIT` (0x10) is defined in `src/hdlc_private.h` but hardcoded to 0 in `hdlc_send_frmr()`. ISO 13239 Table 10 requires this bit to reflect the C/R state of the rejected frame.
- Fix: Derive `cr` from `rejected_addr == ctx->my_address`. Set bit 4 of `info[1]` accordingly.
- Files: `src/station/hdlc_frame_handlers.c`

**B4 — FRMR state: no periodic resend (spec violation)**
- ISO 13239 §5.5.3 requires repeatedly retransmitting FRMR under T1 until a SABM or DISC is received. Currently `atc_hdlc_t1_expired()` has no `case ATC_HDLC_STATE_FRMR_ERROR:` — the station goes silent.
- Fix:
  1. Add `atc_hdlc_u8 frmr_info[3]` to `atc_hdlc_context_t` (cache the FRMR payload).
  2. In `hdlc_send_frmr()`: populate `ctx->frmr_info`, then call `hdlc_t1_start()` instead of `hdlc_t1_stop()`.
  3. In `atc_hdlc_t1_expired()`: add the FRMR_ERROR case — retransmit `ctx->frmr_info`, restart T1.
- Files: `inc/hdlc_types.h`, `src/station/hdlc_station.c`, `src/station/hdlc_frame_handlers.c`
- Dependency: do B3 first so the cached bytes are correct.

**B5 — `on_send` return value silently discarded**
- `hdlc_write_byte()` calls `on_send()` and ignores the return value. If the UART transmit buffer is full, the library silently drops bytes mid-frame, producing a frame with invalid FCS.
- Fix:
  1. In `hdlc_write_byte()`: if `on_send()` returns non-zero, set `enc_ctx->ctx->tx_error = true`.
  2. Add `bool tx_error` to `atc_hdlc_context_t`.
  3. `atc_hdlc_transmit_start()` clears `ctx->tx_error`.
  4. `atc_hdlc_transmit_i/ui/test()` check `ctx->tx_error` after `transmit_end()` and return `ATC_HDLC_ERR_SEND_FAILED = -17`.
- Files: `inc/hdlc_types.h`, `src/frame/hdlc_frame.c`, `src/station/hdlc_out.c`

---

## Architecture

**A1 — Eliminate `atc_hdlc_frame_t.type` field**
- `frame->type` (4-byte enum) is computed redundantly from `frame->control` and stored in the struct.
- Fix: Remove the `type` field from `atc_hdlc_frame_t`. State handlers dispatch directly on `ctrl` bits.
- Public-API functions `atc_hdlc_get_s_frame_sub_type()` and `atc_hdlc_get_u_frame_sub_type()` remain as thin wrappers on the control byte.
- Embedded gain: saves 4 bytes per frame struct on the stack; eliminates one function call per received frame.
- Files: `inc/hdlc_types.h`, `src/hdlc_private.h`, `src/frame/hdlc_frame.c`, `src/station/hdlc_frame_handlers.c`

**A2 — U-frame dispatch: direct hex switch**
- Replace `atc_hdlc_get_u_frame_sub_type()` (sequential comparisons) with `switch(ctrl & 0xEF)`:
  ```c
  #define HDLC_U_SABM   0x2F
  #define HDLC_U_DISC   0x43
  #define HDLC_U_UA     0x63
  #define HDLC_U_DM     0x0F
  #define HDLC_U_FRMR   0x87
  #define HDLC_U_UI     0x03
  #define HDLC_U_TEST   0xE3
  #define HDLC_U_SNRM   0x83
  #define HDLC_U_SABME  0x6F
  #define HDLC_PF_BIT   0x10
  ```
- Embedded gain: on Cortex-M0, a jump table replaces ~10 compare-and-branch sequences.
- Files: `src/hdlc_private.h`, `src/station/hdlc_frame_handlers.c`, `src/frame/hdlc_frame.c`
- Dependency: do together with A1.

**A3 — Per-state compilation units**
- Split `src/station/hdlc_frame_handlers.c` into:
  - `src/station/hdlc_state_disconnected.c`
  - `src/station/hdlc_state_connecting.c`
  - `src/station/hdlc_state_disconnecting.c`
  - `src/station/hdlc_state_connected.c` — includes `hdlc_process_nr()` + `hdlc_retransmit_go_back_n()`
  - `src/station/hdlc_state_frmr.c`
  - `src/station/hdlc_frame_handlers.c` — keep only: `hdlc_process_complete_frame()`, `hdlc_reset_connection_state()`, `hdlc_send_frmr()`
  - Add `src/station/hdlc_states.h` with the 5 internal function prototypes.
  - Update `src/CMakeLists.txt`.
- Embedded gain: linker can dead-strip whole compilation units.
- Dependency: do after A1 + A2.

**A4 — Complete the C/R role model**
- `atc_hdlc_station_role_t` enum exists in `hdlc_types.h` but no `role` field in `atc_hdlc_context_t`. State handlers use raw `frame->address == ctx->my_address` ad-hoc.
- Fix:
  1. Add `atc_hdlc_station_role_t role` to `atc_hdlc_context_t`.
  2. `atc_hdlc_init()` sets `ctx->role = ATC_HDLC_ROLE_COMBINED`.
  3. Add `hdlc_is_cmd()` inline to `src/hdlc_private.h` using the role field.
  4. Replace all ad-hoc address comparisons with `hdlc_is_cmd(ctx, frame)`.
- Files: `inc/hdlc_types.h`, `src/hdlc_private.h`, `src/station/hdlc_frame_handlers.c`, `src/station/hdlc_station.c`

---

## Embedded Robustness

**E1 — Configurable log sink (remove direct `printf`)**
- `ATC_HDLC_LOG_*` macros call `printf` directly, linking stdio (~10-40KB on Cortex-M0).
- Fix: Add to `inc/hdlc_config.h`:
  ```c
  #ifndef ATC_HDLC_LOG_IMPL
  #  define ATC_HDLC_LOG_IMPL(level, fmt, ...) printf("[HDLC %s] " fmt "\n", level, ##__VA_ARGS__)
  #endif
  ```
  Rewrite the macros in `src/hdlc_private.h` to delegate to `ATC_HDLC_LOG_IMPL`.
- Files: `inc/hdlc_config.h`, `src/hdlc_private.h`

**E2 — Compiler warning flags**
- Add to `src/CMakeLists.txt`:
  ```cmake
  target_compile_options(atc_hdlc PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wpedantic)
  ```
  Fix surfaced warnings (primarily signed/unsigned in modulo arithmetic and FCS byte splits).
- Files: `src/CMakeLists.txt`, several `.c` files for cast fixes.

**E3 — ISR/thread safety contract**
- Add a documentation block at the top of `inc/hdlc.h` covering:
  - `atc_hdlc_data_in()` — NOT ISR-safe
  - `atc_hdlc_t[123]_expired()` — NOT ISR-safe
  - `atc_hdlc_transmit_*()` — NOT ISR-safe
  - `atc_hdlc_get_state/stats()` — read-only, safe if not concurrently written
- Files: `inc/hdlc.h`

**E4 — `atc_hdlc_abort()` API**
- No API exists for immediate abort. Needed when UART hardware detects a line break or framing error.
- Fix: Add `void atc_hdlc_abort(atc_hdlc_context_t *ctx)`:
  sends `0x7E 0x7E` to force peer into HUNT state; stops all timers; calls `hdlc_reset_connection_state()`;
  sets `ctx->rx_state = HDLC_RX_STATE_HUNT`; transitions to DISCONNECTED without sending DISC.
- Files: `inc/hdlc.h`, `src/station/hdlc_station.c`

**E5 — Replace `atc_hdlc_bool` with `bool` in `src/`**
- All `src/` files use `bool`, `true`, `false` directly. Leave `inc/hdlc_types.h` typedef unchanged.
- Files: `src/hdlc_private.h` and all `.c` files under `src/`.

---

## Features

**F1 — Enhanced statistics**
- Add to `atc_hdlc_stats_t` in `inc/hdlc_types.h`:
  ```c
  atc_hdlc_u32 t1_expiry_count;
  atc_hdlc_u32 retry_exhaustion_count;
  atc_hdlc_u32 sabm_collision_count;
  atc_hdlc_u32 window_full_count;
  atc_hdlc_u32 tx_s_frames;
  atc_hdlc_u32 tx_u_frames;
  atc_hdlc_u32 rx_s_frames;
  atc_hdlc_u32 rx_u_frames;
  atc_hdlc_u32 rx_out_of_sequence;
  ```
- Files: `inc/hdlc_types.h`, `src/station/hdlc_frame_handlers.c`, `src/station/hdlc_station.c`

**F4 — Promiscuous / monitor mode**
- Gated behind `#define ATC_HDLC_ENABLE_PROMISCUOUS 0` in `inc/hdlc_config.h`.
- Add `bool promiscuous` to context and `atc_hdlc_set_promiscuous(ctx, enable)` to public API.
- Add `atc_hdlc_on_sniff_fn on_sniff` callback to `atc_hdlc_platform_t`.
- In `hdlc_in.c` address filter: skip rejection when `promiscuous` is set.
- Files: `inc/hdlc_config.h`, `inc/hdlc_types.h`, `inc/hdlc.h`, `src/station/hdlc_in.c`

---

## Out of Scope

- **F2 — Extended frame format (mod-128)**
- **F3 — SREJ (Selective Reject)**

---

## Execution Order

```
Step 1 (Bugs):        B3 → B4 → B1 → B2 → B5
Step 2 (Architecture): A1 + A2 (together) → A3 → A4
Step 3 (Robustness):  E1 → E2 → E3 → E4 → E5
Step 4 (Features):    F1 → F4
```

---

## Critical Files

| File | Touches |
|------|---------|
| `inc/hdlc_types.h` | B4 (frmr_info), B5 (tx_error, error code), A1 (remove frame.type), A4 (role field), F1 (stats fields) |
| `src/station/hdlc_frame_handlers.c` | B3, B4, A1, A2, A3, A4, F1 |
| `src/station/hdlc_station.c` | B2, B4, A4, E4 |
| `src/station/hdlc_out.c` | B1, B5 |
| `src/hdlc_private.h` | A1, A2, A4, E1, E5 |
| `src/frame/hdlc_frame.c` | A1, B5 |
| `inc/hdlc_config.h` | E1, F4 |
| `inc/hdlc.h` | E3, E4, F4 |
