/*
 * net/dsa/tree.c - DSA switch tree handling
 * Copyright (c) 2016 Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/if_bridge.h>
#include <linux/list.h>
#include <linux/netdevice.h>

#include "dsa_priv.h"

int dsa_tree_bridge_port_join(struct dsa_switch_tree *dst, struct dsa_port *dp,
			      struct net_device *br)
{
	struct dsa_switch *ds;
	int err = 0;

	/* on NETDEV_CHANGEUPPER, the port is already bridged */
	dp->br = br;

	dsa_tree_for_each_switch(dst, ds) {
		if (ds->drv->port_bridge_join) {
			err = ds->drv->port_bridge_join(ds, dp, br);
			if (err) {
				if (err != -EOPNOTSUPP)
					break;
				err = 0;
			}
		}
	}

	/* if an error is reported, bridge rolls back the operation */
	if (err)
		dp->br = NULL;

	return err;
}

void dsa_tree_bridge_port_leave(struct dsa_switch_tree *dst,
				struct dsa_port *dp, struct net_device *br)
{
	struct dsa_switch *ds;

	/* on NETDEV_CHANGEUPPER, the port is already unbridged */
	dp->br = NULL;

	dsa_tree_for_each_switch(dst, ds) {
		if (ds->drv->port_bridge_leave)
			ds->drv->port_bridge_leave(ds, dp, br);

		if (dsa_port_is_external(dp, ds))
			continue;

		/* The bridge layer put the port in BR_STATE_DISABLED,
		 * restore BR_STATE_FORWARDING to keep it functional.
		 */
		if (ds->drv->port_stp_state_set)
			ds->drv->port_stp_state_set(ds, dp->port,
						    BR_STATE_FORWARDING);
	}
}
