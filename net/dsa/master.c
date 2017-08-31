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

static void dsa_master_get_ethtool_stats(struct net_device *dev,
					 struct ethtool_stats *stats,
					 uint64_t *data)
{
	struct dsa_master *master = dev->dsa_ptr;
	struct dsa_port *port = master->port;
	struct dsa_switch *ds = port->ds;
	const struct ethtool_ops *ops = master->orig_ethtool_ops;
	int count = 0;

	if (ops && ops->get_sset_count && ops->get_ethtool_stats) {
		count = ops->get_sset_count(dev, ETH_SS_STATS);
		ops->get_ethtool_stats(dev, stats, data);
	}

	if (ds->ops->get_ethtool_stats)
		ds->ops->get_ethtool_stats(ds, port->index, data + count);
}

static int dsa_master_get_sset_count(struct net_device *dev, int sset)
{
	struct dsa_master *master = dev->dsa_ptr;
	struct dsa_port *port = master->port;
	struct dsa_switch *ds = port->ds;
	const struct ethtool_ops *ops = master->orig_ethtool_ops;
	int count = 0;

	if (ops && ops->get_sset_count)
		count += ops->get_sset_count(dev, sset);

	if (sset == ETH_SS_STATS && ds->ops->get_sset_count)
		count += ds->ops->get_sset_count(ds);

	return count;
}

static void dsa_master_get_strings(struct net_device *dev, uint32_t stringset,
				   uint8_t *data)
{
	struct dsa_master *master = dev->dsa_ptr;
	struct dsa_port *port = master->port;
	struct dsa_switch *ds = port->ds;
	const struct ethtool_ops *ops = master->orig_ethtool_ops;
	int len = ETH_GSTRING_LEN;
	int mcount = 0, count;
	unsigned int i;
	uint8_t pfx[4];
	uint8_t *ndata;

	snprintf(pfx, sizeof(pfx), "p%.2d", port->index);
	/* We do not want to be NULL-terminated, since this is a prefix */
	pfx[sizeof(pfx) - 1] = '_';

	if (ops && ops->get_sset_count && ops->get_strings) {
		mcount = ops->get_sset_count(dev, ETH_SS_STATS);
		ops->get_strings(dev, stringset, data);
	}

	if (stringset == ETH_SS_STATS && ds->ops->get_strings) {
		ndata = data + mcount * len;
		/* This function copies ETH_GSTRINGS_LEN bytes, we will mangle
		 * the output after to prepend our CPU port prefix we
		 * constructed earlier
		 */
		ds->ops->get_strings(ds, port->index, ndata);
		count = ds->ops->get_sset_count(ds);
		for (i = 0; i < count; i++) {
			memmove(ndata + (i * len + sizeof(pfx)),
				ndata + i * len, len - sizeof(pfx));
			memcpy(ndata + i * len, pfx, sizeof(pfx));
		}
	}
}

static int dsa_master_ethtool_setup(struct dsa_master *master)
{
	struct device *dev = master->port->ds->dev;
	struct ethtool_ops *ops;

	ops = devm_kzalloc(dev, sizeof(*ops), GFP_KERNEL);
	if (!ops)
		return -ENOMEM;

	master->orig_ethtool_ops = master->netdev->ethtool_ops;
	if (master->orig_ethtool_ops)
		memcpy(ops, master->orig_ethtool_ops, sizeof(*ops));

	ops->get_sset_count = dsa_master_get_sset_count;
	ops->get_ethtool_stats = dsa_master_get_ethtool_stats;
	ops->get_strings = dsa_master_get_strings;

	master->netdev->ethtool_ops = ops;

	return 0;
}

static void dsa_master_ethtool_restore(struct dsa_master *master)
{
	master->netdev->ethtool_ops = master->orig_ethtool_ops;
	master->orig_ethtool_ops = NULL;
}

static int dsa_master_obj_add(struct net_device *dev,
			      const struct switchdev_obj *obj,
			      struct switchdev_trans *trans)
{
	struct dsa_master *master = dev->dsa_ptr;
	const struct switchdev_ops *ops = master->orig_switchdev_ops;
	int err;

	if (ops && ops->switchdev_port_obj_add) {
		err = ops->switchdev_port_obj_add(dev, obj, trans);
		if (err)
			return err;
	}

	return dsa_port_obj_add(master->port, obj, trans);
}

static int dsa_master_obj_del(struct net_device *dev,
			      const struct switchdev_obj *obj)
{
	struct dsa_master *master = dev->dsa_ptr;
	const struct switchdev_ops *ops = master->orig_switchdev_ops;
	int err;

	if (ops && ops->switchdev_port_obj_del) {
		err = ops->switchdev_port_obj_del(dev, obj);
		if (err)
			return err;
	}

	return dsa_port_obj_del(master->port, obj);
}

static int dsa_master_switchdev_setup(struct dsa_master *master)
{
	struct device *dev = master->port->ds->dev;
	struct switchdev_ops *ops;

	ops = devm_kzalloc(dev, sizeof(*ops), GFP_KERNEL);
	if (!ops)
		return -ENOMEM;

	master->orig_switchdev_ops = master->netdev->switchdev_ops;
	if (master->orig_switchdev_ops)
		memcpy(ops, master->orig_switchdev_ops, sizeof(*ops));

	ops->switchdev_port_obj_add = dsa_master_obj_add;
	ops->switchdev_port_obj_del = dsa_master_obj_del;

	master->netdev->switchdev_ops = ops;

	return 0;
}

static void dsa_master_switchdev_restore(struct dsa_master *master)
{
	master->netdev->switchdev_ops = master->orig_switchdev_ops;
	master->orig_switchdev_ops = NULL;
}

int dsa_master_tag_protocol(struct dsa_master *master)
{
	struct dsa_switch *ds = master->port->ds;
	enum dsa_tag_protocol proto;

	if (!ds->ops->get_tag_protocol)
		return -EOPNOTSUPP;

	proto = ds->ops->get_tag_protocol(ds);

	master->tag_ops = dsa_resolve_tag_protocol(proto);
	if (IS_ERR(master->tag_ops))
		return PTR_ERR(master->tag_ops);

	master->rcv = master->tag_ops->rcv;

	return 0;
}

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

	return master;
}

int dsa_master_setup(struct dsa_master *master)
{
	int err;

	err = dsa_master_ethtool_setup(master);
	if (err)
		return err;

	err = dsa_master_switchdev_setup(master);
	if (err)
		return err;

	return 0;
}

void dsa_master_restore(struct dsa_master *master)
{
	dsa_master_switchdev_restore(master);
	dsa_master_ethtool_restore(master);
}
