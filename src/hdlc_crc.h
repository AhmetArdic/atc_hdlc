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
 * @file hdlc_crc.h
 * @author ahmettardic - Ahmet Talha ARDIC
 * @date 02.02.2026
 * @brief Internal CRC-16 API.
 *
 * Declares the CRC update function used by the HDLC engine.
 */

#ifndef ATC_HDLC_CRC_H
#define ATC_HDLC_CRC_H

#include "../inc/hdlc_types.h"

 /**
 * @brief FCS initialization value.
 *
 * Default: 0xFFFF
 */
#define ATC_HDLC_FCS_INIT_VALUE     (0xFFFF)

/**
 * @brief Update the running CRC-16-CCITT value.
 *
 * @param fcs  Current accumulated CRC value.
 * @param data New byte to include in calculation.
 * @return atc_hdlc_u16 Updated CRC value.
 */
atc_hdlc_u16 atc_hdlc_crc_ccitt_update(atc_hdlc_u16 fcs, atc_hdlc_u8 data);

#endif // ATC_HDLC_CRC_H
