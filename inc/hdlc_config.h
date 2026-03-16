/*
 * Copyright (C) 2026 Ahmet Talha ARDIC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file hdlc_config.h
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief Configuration parameters for the HDLC Library.
 *
 * This file contains compile-time configuration macros to tailor the HDLC
 * library to specific embedded system constraints.
 */

#ifndef ATC_HDLC_CONFIG_H
#define ATC_HDLC_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * --------------------------------------------------------------------------
 * TIMER DEFAULTS
 * --------------------------------------------------------------------------
 */

/**
 * @brief Default retransmission (T1) timeout in ticks.
 *
 * Used as the default value for the retransmit_timeout parameter
 * in atc_hdlc_init(). Can be overridden at init time.
 * The actual time depends on how frequently atc_hdlc_tick() is called.
 */
#ifndef ATC_HDLC_DEFAULT_RETRANSMIT_TIMEOUT
#define ATC_HDLC_DEFAULT_RETRANSMIT_TIMEOUT  1000
#endif

/**
 * @brief Default ACK delay (T2) timeout in ticks.
 *
 * Used as the default value for the ack_delay_timeout parameter
 * in atc_hdlc_init(). Prevents immediate RR transmission, allowing
 * piggybacking on outgoing I-frames.
 */
#ifndef ATC_HDLC_DEFAULT_ACK_DELAY_TIMEOUT
#define ATC_HDLC_DEFAULT_ACK_DELAY_TIMEOUT  10
#endif

/**
 * @brief Default Contention delay timeout in ticks.
 *
 * Used for backoff when two peers send SABM concurrently to prevent deadlocks.
 */
#ifndef ATC_HDLC_DEFAULT_CONTENTION_DELAY_TIMEOUT
#define ATC_HDLC_DEFAULT_CONTENTION_DELAY_TIMEOUT  100
#endif

/*
 * --------------------------------------------------------------------------
 * RETRY COUNT DEFAULTS
 * --------------------------------------------------------------------------
 */

/**
 * @brief Default maximum retry count (N2) before link failure.
 *
 * Used as the default value for the max_retry_count parameter
 * in atc_hdlc_init(). Can be overridden at init time.
 */
#ifndef ATC_HDLC_DEFAULT_MAX_RETRY_COUNT
#define ATC_HDLC_DEFAULT_MAX_RETRY_COUNT  3
#endif

/*
 * --------------------------------------------------------------------------
 * WINDOW SIZE DEFAULTS
 * --------------------------------------------------------------------------
 */

/**
 * @brief Default transmit window size for Go-Back-N.
 *
 * Valid range: 1..7. Window=1 is equivalent to Stop-and-Wait.
 * Can be overridden at compile time or at init time.
 */
#ifndef ATC_HDLC_DEFAULT_WINDOW_SIZE
#define ATC_HDLC_DEFAULT_WINDOW_SIZE  1
#endif

/*
 * --------------------------------------------------------------------------
 * STATISTICS INSTRUMENTATION
 * --------------------------------------------------------------------------
 */

/**
 * @brief Enable runtime statistics collection in atc_hdlc_stats_t.
 *
 * Set to 0 to compile out all stat increments (zero overhead on
 * resource-constrained targets). Default: enabled.
 */
#ifndef ATC_HDLC_ENABLE_STATS
#define ATC_HDLC_ENABLE_STATS 1
#endif

/**
 * @brief Enable internal assertion checks.
 *
 * Set to 1 for debug builds. Requires <assert.h>. Default: disabled.
 */
#ifndef ATC_HDLC_ENABLE_ASSERT
#define ATC_HDLC_ENABLE_ASSERT 0
#endif

/*
 * --------------------------------------------------------------------------
 * CRC COMPUTATION
 * --------------------------------------------------------------------------
 */

/**
 * @brief Use a 256-entry lookup table for FCS-16 computation.
 *
 * Set to 1 for maximum speed (+512 B ROM).
 * Set to 0 for bit-by-bit computation (minimal ROM, ~8× slower).
 * Default: table enabled.
 */
#ifndef ATC_HDLC_FCS_USE_TABLE
#define ATC_HDLC_FCS_USE_TABLE 1
#endif

/*
 * --------------------------------------------------------------------------
 * DEBUG LOGGING
 * --------------------------------------------------------------------------
 */

/**
 * @brief Enable debug logging macros
 * 
 * Set to 1 to enable HDLC debug printouts. Requires <stdio.h>.
 */
#ifndef ATC_HDLC_ENABLE_DEBUG_LOGS
#define ATC_HDLC_ENABLE_DEBUG_LOGS 0
#endif

#if ATC_HDLC_ENABLE_DEBUG_LOGS
#define ATC_HDLC_LOG_DEBUG(fmt, ...) printf("[HDLC DEBUG] " fmt "\n", ##__VA_ARGS__)
#define ATC_HDLC_LOG_WARN(fmt, ...)  printf("[HDLC WARN]  " fmt "\n", ##__VA_ARGS__)
#define ATC_HDLC_LOG_ERROR(fmt, ...) printf("[HDLC ERROR] " fmt "\n", ##__VA_ARGS__)
#else
#define ATC_HDLC_LOG_DEBUG(fmt, ...)
#define ATC_HDLC_LOG_WARN(fmt, ...)
#define ATC_HDLC_LOG_ERROR(fmt, ...)
#endif

#ifdef __cplusplus
}
#endif

#endif // ATC_HDLC_CONFIG_H
