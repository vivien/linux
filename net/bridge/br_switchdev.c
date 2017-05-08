#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <net/switchdev.h>

#include "br_private.h"

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
