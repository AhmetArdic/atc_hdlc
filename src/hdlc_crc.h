/*
 * Copyright (C) 2026 Ahmet Talha ARDIC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef ATC_HDLC_CRC_H
#define ATC_HDLC_CRC_H

#include "../inc/hdlc_types.h"

#define ATC_HDLC_FCS_INIT_VALUE (0xFFFF)

atc_hdlc_u16 atc_hdlc_crc_ccitt_update(atc_hdlc_u16 fcs, atc_hdlc_u8 data);

#endif /* ATC_HDLC_CRC_H */
