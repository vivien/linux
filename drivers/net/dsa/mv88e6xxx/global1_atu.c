/*
 * Marvell 88E6xxx Address Translation Unit (ATU) support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 * Copyright (c) 2017 Savoir-faire Linux, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mv88e6xxx.h"
#include "global1.h"

/* Offset 0x01: ATU FID Register
 * Offset 0x0A: ATU Control Register
 * Offset 0x0B: ATU Operation Register
 */

static int mv88e6xxx_g1_atu_wait(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g1_wait(chip, GLOBAL_ATU_OP, GLOBAL_ATU_OP_BUSY);
}

static int mv88e6xxx_g1_atu_op(struct mv88e6xxx_chip *chip, u16 fid, u16 op)
{
	u16 val;
	int err;

	/* FID bits are dispatched all around gradually as more are supported */
	if (mv88e6xxx_num_databases(chip) > 256) {
		err = mv88e6xxx_g1_write(chip, GLOBAL_ATU_FID, fid);
		if (err)
			return err;
	} else {
		if (mv88e6xxx_num_databases(chip) > 16) {
			/* ATU DBNum[7:4] are located in ATU Control 15:12 */
			err = mv88e6xxx_g1_read(chip, GLOBAL_ATU_CONTROL, &val);
			if (err)
				return err;

			val = (val & 0x0fff) | ((fid << 8) & 0xf000);
			err = mv88e6xxx_g1_write(chip, GLOBAL_ATU_CONTROL, val);
			if (err)
				return err;
		}

		/* ATU DBNum[3:0] are located in ATU Operation 3:0 */
		op |= fid & 0xf;
	}

	err = mv88e6xxx_g1_write(chip, GLOBAL_ATU_OP, op);
	if (err)
		return err;

	return mv88e6xxx_g1_atu_wait(chip);
}

/* Offset 0x0C: ATU Data Register */

static int mv88e6xxx_g1_atu_data_read(struct mv88e6xxx_chip *chip,
				      struct mv88e6xxx_atu_entry *entry)
{
	u16 val;
	int err;

	err = mv88e6xxx_g1_read(chip, GLOBAL_ATU_DATA, &val);
	if (err)
		return err;

	entry->state = val & 0xf;
	if (entry->state != GLOBAL_ATU_DATA_STATE_UNUSED) {
		if (val & GLOBAL_ATU_DATA_TRUNK)
			entry->trunk = true;

		entry->portvec = (val >> 4) & mv88e6xxx_port_mask(chip);
	}

	return 0;
}

static int mv88e6xxx_g1_atu_data_write(struct mv88e6xxx_chip *chip,
				       struct mv88e6xxx_atu_entry *entry)
{
	u16 data = entry->state & 0xf;

	if (entry->state != GLOBAL_ATU_DATA_STATE_UNUSED) {
		if (entry->trunk)
			data |= GLOBAL_ATU_DATA_TRUNK;

		data |= (entry->portvec & mv88e6xxx_port_mask(chip)) << 4;
	}

	return mv88e6xxx_g1_write(chip, GLOBAL_ATU_DATA, data);
}

/* Offset 0x0D: ATU MAC Address Register Bytes 0 & 1
 * Offset 0x0E: ATU MAC Address Register Bytes 2 & 3
 * Offset 0x0F: ATU MAC Address Register Bytes 4 & 5
 */

static int mv88e6xxx_g1_atu_mac_read(struct mv88e6xxx_chip *chip,
				     struct mv88e6xxx_atu_entry *entry)
{
	u16 val;
	int i, err;

	for (i = 0; i < 3; i++) {
		err = mv88e6xxx_g1_read(chip, GLOBAL_ATU_MAC_01 + i, &val);
		if (err)
			return err;

		entry->mac[i * 2] = val >> 8;
		entry->mac[i * 2 + 1] = val & 0xff;
	}

	return 0;
}

static int mv88e6xxx_g1_atu_mac_write(struct mv88e6xxx_chip *chip,
				      struct mv88e6xxx_atu_entry *entry)
{
	u16 val;
	int i, err;

	for (i = 0; i < 3; i++) {
		val = (entry->mac[i * 2] << 8) | entry->mac[i * 2 + 1];
		err = mv88e6xxx_g1_write(chip, GLOBAL_ATU_MAC_01 + i, val);
		if (err)
			return err;
	}

	return 0;
}

/* Address Translation Unit operations */

int mv88e6xxx_g1_atu_getnext(struct mv88e6xxx_chip *chip,
			     struct mv88e6xxx_atu_entry *entry)
{
	int err;

	err = mv88e6xxx_g1_atu_wait(chip);
	if (err)
		return err;

	/* Write the MAC address to iterate from only once */
	if (entry->state == GLOBAL_ATU_DATA_STATE_UNUSED) {
		err = mv88e6xxx_g1_atu_mac_write(chip, entry);
		if (err)
			return err;
	}

	err = mv88e6xxx_g1_atu_op(chip, entry->fid, GLOBAL_ATU_OP_GET_NEXT_DB);
	if (err)
		return err;

	err = mv88e6xxx_g1_atu_data_read(chip, entry);
	if (err)
		return err;

	return mv88e6xxx_g1_atu_mac_read(chip, entry);
}

int mv88e6xxx_g1_atu_loadpurge(struct mv88e6xxx_chip *chip,
			       struct mv88e6xxx_atu_entry *entry)
{
	int err;

	err = mv88e6xxx_g1_atu_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g1_atu_mac_write(chip, entry);
	if (err)
		return err;

	err = mv88e6xxx_g1_atu_data_write(chip, entry);
	if (err)
		return err;

	return mv88e6xxx_g1_atu_op(chip, entry->fid, GLOBAL_ATU_OP_LOAD_DB);
}

static int mv88e6xxx_g1_atu_flushmove(struct mv88e6xxx_chip *chip,
				      struct mv88e6xxx_atu_entry *entry,
				      bool static_too)
{
	int op;
	int err;

	err = mv88e6xxx_g1_atu_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g1_atu_data_write(chip, entry);
	if (err)
		return err;

	if (entry->fid) {
		op = static_too ? GLOBAL_ATU_OP_FLUSH_MOVE_ALL_DB :
			GLOBAL_ATU_OP_FLUSH_MOVE_NON_STATIC_DB;
	} else {
		op = static_too ? GLOBAL_ATU_OP_FLUSH_MOVE_ALL :
			GLOBAL_ATU_OP_FLUSH_MOVE_NON_STATIC;
	}

	return mv88e6xxx_g1_atu_op(chip, entry->fid, op);
}

int mv88e6xxx_g1_atu_flush(struct mv88e6xxx_chip *chip, u16 fid,
			   bool static_too)
{
	struct mv88e6xxx_atu_entry entry = {
		.fid = fid,
		.state = 0, /* Null EntryState means Flush */
	};

	return mv88e6xxx_g1_atu_flushmove(chip, &entry, static_too);
}

static int mv88e6xxx_g1_atu_move(struct mv88e6xxx_chip *chip, u16 fid,
				 int from_port, int to_port, bool static_too)
{
	struct mv88e6xxx_atu_entry entry = { 0 };
	unsigned long mask = 0xf;
	int shift = bitmap_weight(&mask, 16);

	entry.fid = fid,
	entry.state = 0xf, /* Full EntryState means Move */
	entry.portvec = from_port & mask;
	entry.portvec |= (to_port & mask) << shift;

	return mv88e6xxx_g1_atu_flushmove(chip, &entry, static_too);
}

int mv88e6xxx_g1_atu_remove(struct mv88e6xxx_chip *chip, u16 fid, int port,
			    bool static_too)
{
	int from_port = port;
	int to_port = 0xf;

	return mv88e6xxx_g1_atu_move(chip, fid, from_port, to_port, static_too);
}
