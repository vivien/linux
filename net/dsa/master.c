/*
 * Handling of a master port, receiving/sending frames from/to slave ports
 *
 * Copyright (c) 2017 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "dsa_priv.h"

struct dsa_master *dsa_master_create(struct dsa_port *port,
				     struct net_device *netdev)
{
	struct device *dev = port->ds->dev;
	struct dsa_master *master;

	master = devm_kzalloc(dev, sizeof(struct dsa_master), GFP_KERNEL);
	if (!master)
		return NULL;

	master->port = port;
	master->netdev = netdev;
	master->port->netdev = netdev;

	return master;
}
