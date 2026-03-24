
---

## Bug Fixes

**B3 — FRMR info byte 1: C/R bit never set**
- `HDLC_FRMR_CR_BIT` (0x10) is defined in `src/hdlc_private.h` but hardcoded to 0 in
  `hdlc_send_frmr()`. ISO 13239 Table 10 requires this bit to reflect the C/R state of the
  rejected frame.
- Fix: Derive `cr` from `ctx->rx_frame.address == ctx->my_address` inside `hdlc_send_frmr()`
  (no new parameter needed — the function is only ever called while processing a received frame,
  so `ctx->rx_frame` is always valid). Use existing constants `HDLC_FRMR_CR_BIT`,
  `HDLC_FRMR_VR_SHIFT`, `HDLC_FRMR_VS_SHIFT` from `src/hdlc_private.h`.
- Files: `src/station/hdlc_in.c`

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

---
