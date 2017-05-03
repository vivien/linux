#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <net/switchdev.h>

#include "br_private.h"

static int nbp_switchdev_fdb_add_event(struct net_bridge_port *p,
				       struct switchdev_notifier_fdb_info *info)
{
	return br_fdb_external_learn_add(p->br, p, info->addr, info->vid);
}

static int nbp_switchdev_fdb_del_event(struct net_bridge_port *p,
				       struct switchdev_notifier_fdb_info *info)
{
	return br_fdb_external_learn_del(p->br, p, info->addr, info->vid);
}

static int br_switchdev_event(struct notifier_block *unused,
			      unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	struct net_bridge_port *p;
	int err = NOTIFY_DONE;

	p = br_port_get_rtnl(dev);
	if (!p)
		goto out;

	switch (event) {
	case SWITCHDEV_FDB_ADD:
		err = nbp_switchdev_fdb_add_event(p, ptr);
		err = notifier_from_errno(err);
		break;
	case SWITCHDEV_FDB_DEL:
		err = nbp_switchdev_fdb_del_event(p, ptr);
		err = notifier_from_errno(err);
		break;
	}

out:
	return err;
}

static struct notifier_block br_switchdev_notifier = {
	.notifier_call = br_switchdev_event,
};

int br_switchdev_notifier_register(void)
{
	return register_switchdev_notifier(&br_switchdev_notifier);
}

void br_switchdev_notifier_unregister(void)
{
	int err;

	err = unregister_switchdev_notifier(&br_switchdev_notifier);
	if (err)
		pr_err("failed to unregister bridge notifier (%d)\n", err);
}

static int br_switchdev_mark_get(struct net_bridge *br, struct net_device *dev)
{
	struct net_bridge_port *p;

	/* dev is yet to be added to the port list. */
	list_for_each_entry(p, &br->port_list, list) {
		if (switchdev_port_same_parent_id(dev, p->dev))
			return p->offload_fwd_mark;
	}

	return ++br->offload_fwd_mark;
}

int nbp_switchdev_mark_set(struct net_bridge_port *p)
{
	struct switchdev_attr attr = {
		.orig_dev = p->dev,
		.id = SWITCHDEV_ATTR_ID_PORT_PARENT_ID,
	};
	int err;

	ASSERT_RTNL();

	err = switchdev_port_attr_get(p->dev, &attr);
	if (err) {
		if (err == -EOPNOTSUPP)
			return 0;
		return err;
	}

	p->offload_fwd_mark = br_switchdev_mark_get(p->br, p->dev);

	return 0;
}

void nbp_switchdev_frame_mark(const struct net_bridge_port *p,
			      struct sk_buff *skb)
{
	if (skb->offload_fwd_mark && !WARN_ON_ONCE(!p->offload_fwd_mark))
		BR_INPUT_SKB_CB(skb)->offload_fwd_mark = p->offload_fwd_mark;
}

bool nbp_switchdev_allowed_egress(const struct net_bridge_port *p,
				  const struct sk_buff *skb)
{
	return !skb->offload_fwd_mark ||
	       BR_INPUT_SKB_CB(skb)->offload_fwd_mark != p->offload_fwd_mark;
}

int nbp_switchdev_fdb_del(const struct net_bridge_port *p,
			  const unsigned char *addr, u16 vid)
{
	struct switchdev_obj_port_fdb fdb = {
		.obj.orig_dev = p->dev,
		.obj.id = SWITCHDEV_OBJ_ID_PORT_FDB,
		.obj.flags = SWITCHDEV_F_DEFER,
		.vid = vid,
	};

	ether_addr_copy(fdb.addr, addr);

	return switchdev_port_obj_del(p->dev, &fdb.obj);
}

int nbp_switchdev_mdb_add(const struct net_bridge_port *p,
			  const unsigned char *addr, u16 vid, void *priv)
{
	struct switchdev_obj_port_mdb mdb = {
		.obj.orig_dev = p->dev,
		.obj.id = SWITCHDEV_OBJ_ID_PORT_MDB,
		.obj.flags = SWITCHDEV_F_DEFER,
		.obj.complete = br_mdb_complete,
		.obj.complete_priv = priv,
		.vid = vid,
	};

	ether_addr_copy(mdb.addr, addr);

	return switchdev_port_obj_add(p->dev, &mdb.obj);
}

int nbp_switchdev_mdb_del(const struct net_bridge_port *p,
			  const unsigned char *addr, u16 vid)
{
	struct switchdev_obj_port_mdb mdb = {
		.obj.orig_dev = p->dev,
		.obj.id = SWITCHDEV_OBJ_ID_PORT_MDB,
		.obj.flags = SWITCHDEV_F_DEFER,
		.vid = vid,
	};

	ether_addr_copy(mdb.addr, addr);

	return switchdev_port_obj_del(p->dev, &mdb.obj);
}

int nbp_switchdev_vlan_add(const struct net_bridge_port *p, u16 vid, u16 flags)
{
	struct switchdev_obj_port_vlan v = {
		.obj.orig_dev = p->dev,
		.obj.id = SWITCHDEV_OBJ_ID_PORT_VLAN,
		.flags = flags,
		.vid_begin = vid,
		.vid_end = vid,
	};

	return switchdev_port_obj_add(p->dev, &v.obj);
}

int nbp_switchdev_vlan_del(const struct net_bridge_port *p, u16 vid)
{
	struct switchdev_obj_port_vlan v = {
		.obj.orig_dev = p->dev,
		.obj.id = SWITCHDEV_OBJ_ID_PORT_VLAN,
		.vid_begin = vid,
		.vid_end = vid,
	};

	return switchdev_port_obj_del(p->dev, &v.obj);
}

int br_switchdev_vlan_filtering(const struct net_bridge *br, bool val)
{
	struct switchdev_attr attr = {
		.orig_dev = br->dev,
		.id = SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING,
		.flags = SWITCHDEV_F_SKIP_EOPNOTSUPP,
		.u.vlan_filtering = val,
	};
	int err;

	err = switchdev_port_attr_set(br->dev, &attr);
	if (err && err != -EOPNOTSUPP)
		return err;

	return 0;
}

int nbp_switchdev_vlan_filtering(const struct net_bridge_port *p)
{
	struct switchdev_attr attr = {
		.orig_dev = p->br->dev,
		.id = SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING,
		.flags = SWITCHDEV_F_SKIP_EOPNOTSUPP,
		.u.vlan_filtering = p->br->vlan_enabled,
	};
	int err;

	err = switchdev_port_attr_set(p->dev, &attr);
	if (err && err != -EOPNOTSUPP)
		return err;

	return 0;
}

int nbp_switchdev_stp_state(const struct net_bridge_port *p)
{
	struct switchdev_attr attr = {
		.orig_dev = p->dev,
		.id = SWITCHDEV_ATTR_ID_PORT_STP_STATE,
		.flags = SWITCHDEV_F_DEFER,
		.u.stp_state = p->state,
	};
	int err;

	err = switchdev_port_attr_set(p->dev, &attr);
	if (err && err != -EOPNOTSUPP)
		return err;

	return 0;
}
