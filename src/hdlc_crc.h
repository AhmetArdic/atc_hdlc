#ifndef HDLC_CRC_H
#define HDLC_CRC_H

#include "hdlc_types.h"

hdlc_u16 hdlc_crc_ccitt_update(hdlc_u16 fcs, hdlc_u8 data);

#endif
