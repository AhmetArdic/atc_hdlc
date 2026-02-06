/**
 * @file hdlc_crc.h
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief Internal CRC-16 API.
 *
 * Declares the CRC update function used by the HDLC engine.
 */

#ifndef HDLC_CRC_H
#define HDLC_CRC_H

#include "../inc/hdlc_types.h"

 /**
 * @brief FCS initialization value.
 *
 * Default: 0xFFFF
 */
#define HDLC_FCS_INIT_VALUE     (0xFFFF)

/**
 * @brief Update the running CRC-16-CCITT value.
 *
 * @param fcs  Current accumulated CRC value.
 * @param data New byte to include in calculation.
 * @return hdlc_u16 Updated CRC value.
 */
hdlc_u16 hdlc_crc_ccitt_update(hdlc_u16 fcs, hdlc_u8 data);

#endif // HDLC_CRC_H
