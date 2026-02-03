#ifndef HDLC_CONFIG_H
#define HDLC_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HDLC Configuration
 * 
 * Adjust these macros to fit the target embedded system's constraints.
 */

/**
 * @brief Maximum Transmission Unit (Payload Size)
 * 
 * Defines the maximum size of the Information field in an I-Frame or UI-Frame.
 * Does not include header (Addr+Ctrl) or trailer (FCS).
 */
#ifndef HDLC_MAX_MTU
#define HDLC_MAX_MTU    256
#endif

/**
 * @brief Address Field Length
 * 
 * Standard HDLC uses 1 byte. Extended addressing is not supported in this
 * classic implementation.
 */
#define HDLC_ADDR_LEN   1

/**
 * @brief Control Field Length
 * 
 * Classic HDLC uses 1 byte (8-bit) control field.
 * Extended (16-bit) is not supported.
 */
#define HDLC_CTRL_LEN   1

/**
 * @brief FCS (CRC) Size
 * 
 * 2 bytes for CRC-16-CCITT.
 */
#define HDLC_FCS_LEN    2

/**
 * @brief Receive Window Size (1-7)
 * 
 * Determines how many I-frames can be unacknowledged.
 * Standard mode allows up to 7.
 */
#ifndef HDLC_WINDOW_SIZE
#define HDLC_WINDOW_SIZE 7
#endif

/**
 * @brief Context User Data
 * 
 * Enable a void* user_data pointer in the context to pass to callbacks.
 */
#define HDLC_USE_USER_DATA 1

#ifdef __cplusplus
}
#endif

#endif // HDLC_CONFIG_H
