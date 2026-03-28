/*
 * Copyright (C) 2026 Ahmet Talha ARDIC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

/**
 * @file hdlc_config.h
 * @brief HDLC configuration options and default values.
 */

#ifndef ATC_HDLC_CONFIG_H
#define ATC_HDLC_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Default T1 retransmit timeout (ms). */
#ifndef ATC_HDLC_DEFAULT_T1_TIMEOUT
#define ATC_HDLC_DEFAULT_T1_TIMEOUT (1000)
#endif

/** @brief Default T2 ack delay timeout (ms). */
#ifndef ATC_HDLC_DEFAULT_T2_TIMEOUT
#define ATC_HDLC_DEFAULT_T2_TIMEOUT (10)
#endif

/** @brief Default max retry count (N2). */
#ifndef ATC_HDLC_DEFAULT_N2_RETRY_COUNT
#define ATC_HDLC_DEFAULT_N2_RETRY_COUNT (3)
#endif

/**
 * @brief Log verbosity levels (used with ATC_HDLC_LOG_LEVEL).
 *   OFF  — all logs compiled out
 *   ERR  — connection-breaking errors (FRMR, link failure, CRC)
 *   WRN  — protocol warnings (bad N(R), out-of-sequence, retransmit)
 *   INFO — state transitions and connection events
 *   DBG  — per-frame detail and flow-control tracking
 */
#define ATC_HDLC_LOG_LEVEL_OFF  (-1)
#define ATC_HDLC_LOG_LEVEL_ERR  (0)
#define ATC_HDLC_LOG_LEVEL_WRN  (1)
#define ATC_HDLC_LOG_LEVEL_INFO (2)
#define ATC_HDLC_LOG_LEVEL_DBG  (3)

#ifndef ATC_HDLC_LOG_LEVEL
#define ATC_HDLC_LOG_LEVEL ATC_HDLC_LOG_LEVEL_OFF 
#endif

/** @brief Log sink. Override before including this header to redirect logs
 *  without pulling in stdio.h (e.g. on bare-metal targets). */
#if ATC_HDLC_LOG_LEVEL >= ATC_HDLC_LOG_LEVEL_ERR
#ifndef ATC_HDLC_LOG_IMPL
#include <stdio.h>
#define ATC_HDLC_LOG_IMPL(level, fmt, ...) printf("[HDLC %-4s] " fmt "\n", level, ##__VA_ARGS__)
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* ATC_HDLC_CONFIG_H */
