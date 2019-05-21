/*
 * Marvell 88E6xxx System Management Interface (RMU) support
 *
 * Copyright (c) 2019 Vivien Didelot <vivien.didelot@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _MV88E6XXX_RMU_H
#define _MV88E6XXX_RMU_H

#define MV88E6XXX_RMU_REQUEST_FORMAT_SOHO	0x0001

#define MV88E6XXX_RMU_REQUEST_CODE_GET_ID	0x0000
#define MV88E6XXX_RMU_REQUEST_CODE_DUMP_ATU	0x1000
#define MV88E6XXX_RMU_REQUEST_CODE_DUMP_MIB	0x1020
#define MV88E6XXX_RMU_REQUEST_CODE_READ_WRITE	0x2000

#define MV88E6XXX_RMU_REQUEST_DATA_DUMP_MIB_CLEAR	0x8000

#define MV88E6XXX_RMU_RESPONSE_CODE_GET_ID	MV88E6XXX_RMU_REQUEST_CODE_GET_ID
#define MV88E6XXX_RMU_RESPONSE_CODE_DUMP_ATU	MV88E6XXX_RMU_REQUEST_CODE_DUMP_ATU
#define MV88E6XXX_RMU_RESPONSE_CODE_DUMP_MIB	MV88E6XXX_RMU_REQUEST_CODE_DUMP_MIB
#define MV88E6XXX_RMU_RESPONSE_CODE_READ_WRITE	MV88E6XXX_RMU_REQUEST_CODE_READ_WRITE

#include "chip.h"

int mv88e6xxx_rmu_setup(struct mv88e6xxx_chip *chip);
int mv88e6xxx_rmu_response(struct mv88e6xxx_chip *chip, struct sk_buff *skb);

int mv88e6xxx_rmu_dump_atu(struct mv88e6xxx_chip *chip, u16 *continue_code,
			   struct mv88e6xxx_atu_entry *entries);

#endif /* _MV88E6XXX_RMU_H */
