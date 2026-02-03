/**
 * @file hdlc_config.h
 * @brief Configuration parameters for the HDLC Library.
 *
 * This file contains compile-time configuration macros to tailor the HDLC
 * library to specific embedded system constraints, such as memory usage (MTU),
 * window sizes, and internal buffer limits.
 *
 * @note Adjust these values based on your target architecture and application
 * requirements.
 */

#ifndef HDLC_CONFIG_H
#define HDLC_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * --------------------------------------------------------------------------
 * MEMORY & BUFFER CONFIGURATION
 * --------------------------------------------------------------------------
 */

/**
 * @brief Maximum Transmission Unit (MTU) for the Payload.
 *
 * Defines the maximum size (in bytes) of the Information field (Payload)
 * within an HDLC frame. This value does NOT include the framing overhead
 * (Flag, Address, Control, FCS).
 *
 * @note Increasing this value increases the static RAM usage for the Rx buffer.
 *       Ensure your target has sufficient RAM.
 *
 * Default: 256 bytes.
 */
#define HDLC_MAX_MTU    (256)

#ifdef __cplusplus
}
#endif

#endif // HDLC_CONFIG_H
