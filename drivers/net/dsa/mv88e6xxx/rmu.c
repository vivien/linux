/*
 * Marvell 88E6xxx Remote Management Unit (RMU) support
 *
 * Copyright (c) 2019 Vivien Didelot <vivien.didelot@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "chip.h"
#include "rmu.h"

static int mv88e6xxx_rmu_setup_port(struct mv88e6xxx_chip *chip, int port)
{
	int err;

	/* First disable the RMU */
	if (chip->info->ops->rmu_disable) {
		err = chip->info->ops->rmu_disable(chip);
		if (err)
			return err;
	}

	/* Then enable the RMU on this dedicated port */
	if (chip->info->ops->rmu_enable) {
		err = chip->info->ops->rmu_enable(chip, port, false);
		if (err)
			return err;

		dev_info(chip->dev, "RMU enabled on port %d\n", port);

		return 0;
	}

	return -EOPNOTSUPP;
}

int mv88e6xxx_rmu_setup(struct mv88e6xxx_chip *chip)
{
	struct dsa_switch *ds = chip->ds;
	int port;
	int err;

	/* Find a local port (in)directly connected to the CPU to enable RMU on */
	for (port = 0; port < mv88e6xxx_num_ports(chip); port++) {
		if (dsa_is_upstream_port(ds, port)) {
			err = mv88e6xxx_rmu_setup_port(chip, port);
			if (err)
				continue;

			break;
		}
	}

	return 0;
}
