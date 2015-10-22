#include <linux/debugfs.h>
#include <linux/seq_file.h>

#define GLOBAL2_PVT_ADDR	0x0b
#define GLOBAL2_PVT_ADDR_BUSY	BIT(15)
#define GLOBAL2_PVT_ADDR_OP_INIT_ONES	((0x01 << 12) | GLOBAL2_PVT_ADDR_BUSY)
#define GLOBAL2_PVT_ADDR_OP_WRITE_PVLAN	((0x03 << 12) | GLOBAL2_PVT_ADDR_BUSY)
#define GLOBAL2_PVT_ADDR_OP_READ	((0x04 << 12) | GLOBAL2_PVT_ADDR_BUSY)
#define GLOBAL2_PVT_DATA	0x0c

static int _mv88e6xxx_pvt_wait(struct mv88e6xxx_priv_state *ps)
{
	return _mv88e6xxx_wait(ps, REG_GLOBAL2, GLOBAL2_PVT_ADDR,
			       GLOBAL2_PVT_ADDR_BUSY);
}

static int _mv88e6xxx_pvt_cmd(struct mv88e6xxx_priv_state *ps, int src_dev,
			      int src_port, u16 op)
{
	u16 reg = op;
	int err;

	/* 9-bit Cross-chip PVT pointer: with GLOBAL2_MISC_5_BIT_PORT cleared,
	 * source device is 5-bit, source port is 4-bit.
	 */
	reg |= (src_dev & 0x1f) << 4;
	reg |= (src_port & 0xf);

	err = _mv88e6xxx_reg_write(ps, REG_GLOBAL2, GLOBAL2_PVT_ADDR, reg);
	if (err)
		return err;

	return _mv88e6xxx_pvt_wait(ps);
}

static int _mv88e6xxx_pvt_read(struct mv88e6xxx_priv_state *ps, int src_dev,
			       int src_port, u16 *data)
{
	int ret;

	ret = _mv88e6xxx_pvt_wait(ps);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_pvt_cmd(ps, src_dev, src_port,
				GLOBAL2_PVT_ADDR_OP_READ);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_reg_read(ps, REG_GLOBAL2, GLOBAL2_PVT_DATA);
	if (ret < 0)
		return ret;

	*data = ret;

	return 0;
}

static int _mv88e6xxx_pvt_write(struct mv88e6xxx_priv_state *ps, int src_dev,
				int src_port, u16 data)
{
	int err;

	err = _mv88e6xxx_pvt_wait(ps);
	if (err)
		return err;

	err = _mv88e6xxx_reg_write(ps, REG_GLOBAL2, GLOBAL2_PVT_DATA, data);
	if (err)
		return err;

        return _mv88e6xxx_pvt_cmd(ps, src_dev, src_port,
				  GLOBAL2_PVT_ADDR_OP_WRITE_PVLAN);
}

static int mv88e6xxx_regs_show(struct seq_file *s, void *p)
{
	struct mv88e6xxx_priv_state *ps = s->private;
	int port, reg, ret;

	seq_puts(s, "    GLOBAL GLOBAL2 SERDES   ");
	for (port = 0; port < ps->info->num_ports; port++)
		seq_printf(s, " %2d  ", port);
	seq_puts(s, "\n");

	mutex_lock(&ps->smi_mutex);

	for (reg = 0; reg < 32; reg++) {
		seq_printf(s, "%2x:", reg);

		ret = _mv88e6xxx_reg_read(ps, REG_GLOBAL, reg);
		if (ret < 0)
			goto unlock;
		seq_printf(s, "  %4x  ", ret);

		ret = _mv88e6xxx_reg_read(ps, REG_GLOBAL2, reg);
		if (ret < 0)
			goto unlock;
		seq_printf(s, "  %4x  ", ret);

		ret = _mv88e6xxx_mdio_page_read(ps, REG_FIBER_SERDES,
						PAGE_FIBER_SERDES, reg);
		if (ret < 0)
			goto unlock;
		seq_printf(s, "  %4x  ", ret);

		/* Port regs 0x1a-0x1f are reserved in 6185 family */
		if (mv88e6xxx_6185_family(ps) && reg > 25) {
			for (port = 0; port < ps->info->num_ports; ++port)
				seq_printf(s, "%4c ", '-');
			seq_puts(s, "\n");
			continue;
		}

		for (port = 0; port < ps->info->num_ports; ++port) {
			ret = _mv88e6xxx_reg_read(ps, REG_PORT(port), reg);
			if (ret < 0)
				goto unlock;

			seq_printf(s, "%4x ", ret);
		}

		seq_puts(s, "\n");
	}

	ret = 0;
unlock:
	mutex_unlock(&ps->smi_mutex);

	return ret;
}

static ssize_t mv88e6xxx_regs_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct mv88e6xxx_priv_state *ps = s->private;
	char cmd[32], name[32] = { 0 };
	unsigned int port, reg, val;
	int ret;

	if (count > sizeof(name) - 1)
		return -EINVAL;

	if (copy_from_user(cmd, buf, sizeof(cmd)))
		return -EFAULT;

	ret = sscanf(cmd, "%s %x %x", name, &reg, &val);
	if (ret != 3)
		return -EINVAL;

	if (reg > 0x1f || val > 0xffff)
		return -ERANGE;

	mutex_lock(&ps->smi_mutex);

	if (strcasecmp(name, "GLOBAL") == 0)
		ret = _mv88e6xxx_reg_write(ps, REG_GLOBAL, reg, val);
	else if (strcasecmp(name, "GLOBAL2") == 0)
		ret = _mv88e6xxx_reg_write(ps, REG_GLOBAL2, reg, val);
	else if (strcasecmp(name, "SERDES") == 0)
		ret = _mv88e6xxx_mdio_page_write(ps, REG_FIBER_SERDES,
						 PAGE_FIBER_SERDES, reg, val);
	else if (kstrtouint(name, 10, &port) == 0 && port < ps->info->num_ports)
		ret = _mv88e6xxx_reg_write(ps, REG_PORT(port), reg, val);
	else
		ret = -EINVAL;

	mutex_unlock(&ps->smi_mutex);

	return ret < 0 ? ret : count;
}

static int mv88e6xxx_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, mv88e6xxx_regs_show, inode->i_private);
}

static const struct file_operations mv88e6xxx_regs_fops = {
	.open   = mv88e6xxx_regs_open,
	.read   = seq_read,
	.write  = mv88e6xxx_regs_write,
	.llseek = no_llseek,
	.release = single_release,
	.owner  = THIS_MODULE,
};

static int mv88e6xxx_atu_show(struct seq_file *s, void *p)
{
	struct mv88e6xxx_priv_state *ps = s->private;
	struct mv88e6xxx_atu_entry addr = {
		.mac = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
	};
	const char *state;
	int fid, i, err;

	seq_puts(s, " FID  MAC Addr                  State         Trunk?  DPV/Trunk ID\n");

	mutex_lock(&ps->smi_mutex);

	err = _mv88e6xxx_atu_mac_write(ps, addr.mac);
	if (err)
		goto unlock;

	for (fid = 0; fid < mv88e6xxx_num_databases(ps); ++fid) {
		do {
			err = _mv88e6xxx_atu_getnext(ps, fid, &addr);
			if (err)
				goto unlock;

			if (addr.state == GLOBAL_ATU_DATA_STATE_UNUSED)
				break;

			/* print ATU entry */
			seq_printf(s, "%4d", addr.fid);

			seq_printf(s, "  %.2x", addr.mac[0]);
			for (i = 1; i < ETH_ALEN; ++i)
				seq_printf(s, ":%.2x", addr.mac[i]);

			if (is_multicast_ether_addr(addr.mac)) {
				switch (addr.state) {
				case 0xf: /* GLOBAL_ATU_DATA_STATE_MC_PRIO_OVER */
					state = "MC_STATIC_PO";
					break;
				case 0xe: /* GLOBAL_ATU_DATA_STATE_MC_MGMT */
					state = "MC_STATIC_MGMT_PO";
					break;
				case 0xd:
					state = "MC_STATIC_NRL_PO";
					break;
				case 0xc:
					state = "MC_STATIC_POLICY_PO";
					break;
				case 0x7: /* GLOBAL_ATU_DATA_STATE_MC_STATIC */
					state = "MC_STATIC";
					break;
				case 0x6:
					state = "MC_STATIC_MGMT";
					break;
				case 0x5: /* GLOBAL_ATU_DATA_STATE_MC_NONE_RATE */
					state = "MC_STATIC_NRL";
					break;
				case 0x4:
					state = "MC_STATIC_POLICY";
					break;
				case 0xb: case 0xa: case 0x9: case 0x8:
					/* Reserved for future use */
				case 0x3: case 0x2: case 0x1:
					/* Reserved for future use */
				case 0x0: /* GLOBAL_ATU_DATA_STATE_UNUSED */
				default:
					state = "???";
					break;
				}
			} else {
				switch (addr.state) {
				case 0xf: /* GLOBAL_ATU_DATA_STATE_UC_PRIO_OVER */
					state = "UC_STATIC_PO";
					break;
				case 0xe: /* GLOBAL_ATU_DATA_STATE_UC_STATIC */
					state = "UC_STATIC";
					break;
				case 0xd: /* GLOBAL_ATU_DATA_STATE_UC_MGMT */
					state = "UC_STATIC_MGMT_PO";
					break;
				case 0xc:
					state = "UC_STATIC_MGMT";
					break;
				case 0xb:
					state = "UC_STATIC_NRL_PO";
					break;
				case 0xa:
					state = "UC_STATIC_NRL";
					break;
				case 0x9:
					state = "UC_STATIC_POLICY_PO";
					break;
				case 0x8:
					state = "UC_STATIC_POLICY";
					break;
				case 0x7:
					state = "Age 7 (newest)";
					break;
				case 0x6:
					state = "Age 6";
					break;
				case 0x5:
					state = "Age 5";
					break;
				case 0x4:
					state = "Age 4";
					break;
				case 0x3:
					state = "Age 3";
					break;
				case 0x2:
					state = "Age 2";
					break;
				case 0x1:
					state = "Age 1 (oldest)";
					break;
				case 0x0: /* GLOBAL_ATU_DATA_STATE_UNUSED */
				default:
					state = "???";
					break;
				}
			}

			seq_printf(s, "  %19s", state);

			if (addr.trunk) {
				seq_printf(s, "       y  %d",
					   addr.portv_trunkid);
			} else {
				seq_puts(s, "       n ");
				for (i = 0; i < ps->info->num_ports; ++i)
					seq_printf(s, " %c",
						   addr.portv_trunkid & BIT(i) ?
						   48 + i : '-');
			}

			seq_puts(s, "\n");
		} while (!is_broadcast_ether_addr(addr.mac));
	}

unlock:
	mutex_unlock(&ps->smi_mutex);

	return err;
}

static ssize_t mv88e6xxx_atu_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct mv88e6xxx_priv_state *ps = s->private;
	char cmd[64];
	unsigned int fid;
	int ret;

	if (copy_from_user(cmd, buf, sizeof(cmd)))
		return -EFAULT;

	ret = sscanf(cmd, "%u", &fid);
	if (ret != 1)
		return -EINVAL;

	if (fid >= mv88e6xxx_num_databases(ps))
		return -ERANGE;

	mutex_lock(&ps->smi_mutex);
	ret = _mv88e6xxx_atu_flush(ps, fid, true);
	mutex_unlock(&ps->smi_mutex);

	return ret < 0 ? ret : count;
}

static int mv88e6xxx_atu_open(struct inode *inode, struct file *file)
{
	return single_open(file, mv88e6xxx_atu_show, inode->i_private);
}

static const struct file_operations mv88e6xxx_atu_fops = {
	.open   = mv88e6xxx_atu_open,
	.read   = seq_read,
	.write   = mv88e6xxx_atu_write,
	.llseek = no_llseek,
	.release = single_release,
	.owner  = THIS_MODULE,
};

static int mv88e6xxx_default_vid_show(struct seq_file *s, void *p)
{
	struct mv88e6xxx_priv_state *ps = s->private;
	u16 pvid;
	int i, err;

	seq_puts(s, " Port  DefaultVID\n");

	mutex_lock(&ps->smi_mutex);

	for (i = 0; i < ps->info->num_ports; ++i) {
		err = _mv88e6xxx_port_pvid_get(ps, i, &pvid);
		if (err)
			break;

		seq_printf(s, "%4d  %d\n", i, pvid);
	}

	mutex_unlock(&ps->smi_mutex);

	return err;
}

static ssize_t mv88e6xxx_default_vid_write(struct file *file,
					   const char __user *buf, size_t count,
					   loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct mv88e6xxx_priv_state *ps = s->private;
	char cmd[32];
	unsigned int port, pvid;
	int ret;

	if (copy_from_user(cmd, buf, sizeof(cmd)))
		return -EFAULT;

	ret = sscanf(cmd, "%u %u", &port, &pvid);
	if (ret != 2)
		return -EINVAL;

	if (port >= ps->info->num_ports || pvid > 0xfff)
		return -ERANGE;

	mutex_lock(&ps->smi_mutex);
	ret = _mv88e6xxx_port_pvid_set(ps, port, pvid);
	mutex_unlock(&ps->smi_mutex);

	return ret < 0 ? ret : count;
}

static int mv88e6xxx_default_vid_open(struct inode *inode, struct file *file)
{
	return single_open(file, mv88e6xxx_default_vid_show, inode->i_private);
}

static const struct file_operations mv88e6xxx_default_vid_fops = {
	.open		= mv88e6xxx_default_vid_open,
	.read		= seq_read,
	.write		= mv88e6xxx_default_vid_write,
	.llseek		= no_llseek,
	.release	= single_release,
	.owner		= THIS_MODULE,
};

static int mv88e6xxx_fid_show(struct seq_file *s, void *p)
{
	struct mv88e6xxx_priv_state *ps = s->private;
	u16 fid;
	int i, err;

	seq_puts(s, " Port  FID\n");

	mutex_lock(&ps->smi_mutex);

	for (i = 0; i < ps->info->num_ports; ++i) {
		err = _mv88e6xxx_port_fid_get(ps, i, &fid);
		if (err)
			break;

		seq_printf(s, "%4d  %d\n", i, fid);
	}

	mutex_unlock(&ps->smi_mutex);

	return err;
}

static int mv88e6xxx_fid_open(struct inode *inode, struct file *file)
{
	return single_open(file, mv88e6xxx_fid_show, inode->i_private);
}

static const struct file_operations mv88e6xxx_fid_fops = {
	.open		= mv88e6xxx_fid_open,
	.read		= seq_read,
	.llseek		= no_llseek,
	.release	= single_release,
	.owner		= THIS_MODULE,
};

static int mv88e6xxx_state_show(struct seq_file *s, void *p)
{
	struct mv88e6xxx_priv_state *ps = s->private;
	int i, ret;

	/* header */
	seq_puts(s, " Port  Mode\n");

	mutex_lock(&ps->smi_mutex);

	/* One line per input port */
	for (i = 0; i < ps->info->num_ports; ++i) {
		seq_printf(s, "%4d ", i);

		ret = _mv88e6xxx_reg_read(ps, REG_PORT(i), PORT_CONTROL);
		if (ret < 0)
			goto unlock;

		ret &= PORT_CONTROL_STATE_MASK;
		seq_printf(s, " %s\n", mv88e6xxx_port_state_names[ret]);
		ret = 0;
	}

unlock:
	mutex_unlock(&ps->smi_mutex);

	return ret;
}

static int mv88e6xxx_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, mv88e6xxx_state_show, inode->i_private);
}

static const struct file_operations mv88e6xxx_state_fops = {
	.open		= mv88e6xxx_state_open,
	.read		= seq_read,
	.llseek		= no_llseek,
	.release	= single_release,
	.owner		= THIS_MODULE,
};

static int mv88e6xxx_8021q_mode_show(struct seq_file *s, void *p)
{
	struct mv88e6xxx_priv_state *ps = s->private;
	int i, ret;

	/* header */
	seq_puts(s, " Port  Mode\n");

	mutex_lock(&ps->smi_mutex);

	/* One line per input port */
	for (i = 0; i < ps->info->num_ports; ++i) {
		seq_printf(s, "%4d ", i);

		ret = _mv88e6xxx_reg_read(ps, REG_PORT(i), PORT_CONTROL_2);
		if (ret < 0)
			goto unlock;

		ret &= PORT_CONTROL_2_8021Q_MASK;
		seq_printf(s, " %s\n", mv88e6xxx_port_8021q_mode_names[ret]);
		ret = 0;
	}

unlock:
	mutex_unlock(&ps->smi_mutex);

	return ret;
}

static int mv88e6xxx_8021q_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, mv88e6xxx_8021q_mode_show, inode->i_private);
}

static const struct file_operations mv88e6xxx_8021q_mode_fops = {
	.open		= mv88e6xxx_8021q_mode_open,
	.read		= seq_read,
	.llseek		= no_llseek,
	.release	= single_release,
	.owner		= THIS_MODULE,
};

static int mv88e6xxx_vlan_table_show(struct seq_file *s, void *p)
{
	struct mv88e6xxx_priv_state *ps = s->private;
	int i, j, ret;

	/* header */
	seq_puts(s, " Port");
	for (i = 0; i < ps->info->num_ports; ++i)
		seq_printf(s, " %2d", i);
	seq_puts(s, "\n");

	mutex_lock(&ps->smi_mutex);

	/* One line per input port */
	for (i = 0; i < ps->info->num_ports; ++i) {
		seq_printf(s, "%4d ", i);

		ret = _mv88e6xxx_reg_read(ps, REG_PORT(i), PORT_BASE_VLAN);
		if (ret < 0)
			goto unlock;

		/* One column per output port */
		for (j = 0; j < ps->info->num_ports; ++j)
			seq_printf(s, "  %c", ret & BIT(j) ? '*' : '-');
		seq_puts(s, "\n");
	}

	ret = 0;
unlock:
	mutex_unlock(&ps->smi_mutex);

	return ret;
}

static int mv88e6xxx_vlan_table_open(struct inode *inode, struct file *file)
{
	return single_open(file, mv88e6xxx_vlan_table_show, inode->i_private);
}

static const struct file_operations mv88e6xxx_vlan_table_fops = {
	.open		= mv88e6xxx_vlan_table_open,
	.read		= seq_read,
	.llseek		= no_llseek,
	.release	= single_release,
	.owner		= THIS_MODULE,
};

static int mv88e6xxx_pvt_show(struct seq_file *s, void *p)
{
	struct mv88e6xxx_priv_state *ps = s->private;
	int port, src_dev, src_port;
	u16 pvlan;
	int err;

	if (mv88e6xxx_6185_family(ps))
		return -ENODEV;

	/* header */
	seq_puts(s, " Dev Port PVLAN");
	for (port = 0; port < ps->info->num_ports; ++port)
		seq_printf(s, " %2d", port);
	seq_puts(s, "\n");

	mutex_lock(&ps->smi_mutex);

	/* One line per external port */
	for (src_dev = 0; src_dev < 32; ++src_dev) {
		if (src_dev >= ps->ds->dst->pd->nr_chips)
			break;

		if (src_dev == ps->ds->index)
			continue;

		seq_puts(s, "\n");
		for (src_port = 0; src_port < 16; ++src_port) {
			if (src_port >= DSA_MAX_PORTS)
				break;

			err = _mv88e6xxx_pvt_read(ps, src_dev, src_port,
						  &pvlan);
			if (err)
				goto unlock;

			seq_printf(s, "  %d   %2d   %03hhx ", src_dev, src_port,
				   pvlan);

			/* One column per internal output port */
			for (port = 0; port < ps->info->num_ports; ++port)
				seq_printf(s, "  %c",
					   pvlan & BIT(port) ? '*' : '-');
			seq_puts(s, "\n");
		}
	}

unlock:
	mutex_unlock(&ps->smi_mutex);

	return err;
}

static ssize_t mv88e6xxx_pvt_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct mv88e6xxx_priv_state *ps = s->private;
	const u16 mask = (1 << ps->info->num_ports) - 1;
	char cmd[32];
	unsigned int src_dev, src_port, pvlan;
	int ret;

	if (copy_from_user(cmd, buf, sizeof(cmd)))
		return -EFAULT;

	if (sscanf(cmd, "%d %d %x", &src_dev, &src_port, &pvlan) != 3)
		return -EINVAL;

	if (src_dev >= 32 || src_port >= 16 || pvlan & ~mask)
		return -ERANGE;

	mutex_lock(&ps->smi_mutex);
	ret = _mv88e6xxx_pvt_write(ps, src_dev, src_port, pvlan);
	mutex_unlock(&ps->smi_mutex);

	return ret < 0 ? ret : count;
}

static int mv88e6xxx_pvt_open(struct inode *inode, struct file *file)
{
	return single_open(file, mv88e6xxx_pvt_show, inode->i_private);
}

static const struct file_operations mv88e6xxx_pvt_fops = {
	.open		= mv88e6xxx_pvt_open,
	.read		= seq_read,
	.write		= mv88e6xxx_pvt_write,
	.llseek		= no_llseek,
	.release	= single_release,
	.owner		= THIS_MODULE,
};

static int mv88e6xxx_vtu_show(struct seq_file *s, void *p)
{
	struct mv88e6xxx_priv_state *ps = s->private;
	int port, ret = 0, vid = GLOBAL_VTU_VID_MASK; /* first or lowest VID */

	seq_puts(s, " VID  FID  SID");
	for (port = 0; port < ps->info->num_ports; ++port)
		seq_printf(s, " %2d", port);
	seq_puts(s, "\n");

	mutex_lock(&ps->smi_mutex);

	ret = _mv88e6xxx_vtu_vid_write(ps, vid);
	if (ret < 0)
		goto unlock;

	do {
		struct mv88e6xxx_vtu_stu_entry next = { 0 };

		ret = _mv88e6xxx_vtu_getnext(ps, &next);
		if (ret < 0)
			goto unlock;

		if (!next.valid)
			break;

		seq_printf(s, "%4d %4d   %2d", next.vid, next.fid, next.sid);
		for (port = 0; port < ps->info->num_ports; ++port) {
			switch (next.data[port]) {
			case GLOBAL_VTU_DATA_MEMBER_TAG_UNMODIFIED:
				seq_puts(s, "  =");
				break;
			case GLOBAL_VTU_DATA_MEMBER_TAG_UNTAGGED:
				seq_puts(s, "  u");
				break;
			case GLOBAL_VTU_DATA_MEMBER_TAG_TAGGED:
				seq_puts(s, "  t");
				break;
			case GLOBAL_VTU_DATA_MEMBER_TAG_NON_MEMBER:
				seq_puts(s, "  x");
				break;
			default:
				seq_puts(s, " ??");
				break;
			}
		}
		seq_puts(s, "\n");

		vid = next.vid;
	} while (vid < GLOBAL_VTU_VID_MASK);

unlock:
	mutex_unlock(&ps->smi_mutex);

	return ret;
}

static ssize_t mv88e6xxx_vtu_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct mv88e6xxx_priv_state *ps = s->private;
	struct mv88e6xxx_vtu_stu_entry entry = { 0 };
	bool valid = true;
	char cmd[64], tags[12]; /* DSA_MAX_PORTS */
	int vid, fid, sid, port, ret;

	if (copy_from_user(cmd, buf, sizeof(cmd)))
		return -EFAULT;

	/* scan 12 chars instead of num_ports to avoid dynamic scanning... */
	ret = sscanf(cmd, "%d %d %d %c %c %c %c %c %c %c %c %c %c %c %c", &vid,
		     &fid, &sid, &tags[0], &tags[1], &tags[2], &tags[3],
		     &tags[4], &tags[5], &tags[6], &tags[7], &tags[8], &tags[9],
		     &tags[10], &tags[11]);
	if (ret == 1)
		valid = false;
	else if (ret != 3 + ps->info->num_ports)
		return -EINVAL;

	entry.vid = vid;
	entry.valid = valid;

	if (valid) {
		entry.fid = fid;
		entry.sid = sid;
		/* Note: The VTU entry pointed by VID will be loaded but not
		 * considered valid until the STU entry pointed by SID is valid.
		 */

		for (port = 0; port < ps->info->num_ports; ++port) {
			u8 tag;

			switch (tags[port]) {
			case 'u':
				tag = GLOBAL_VTU_DATA_MEMBER_TAG_UNTAGGED;
				break;
			case 't':
				tag = GLOBAL_VTU_DATA_MEMBER_TAG_TAGGED;
				break;
			case 'x':
				tag = GLOBAL_VTU_DATA_MEMBER_TAG_NON_MEMBER;
				break;
			case '=':
				tag = GLOBAL_VTU_DATA_MEMBER_TAG_UNMODIFIED;
				break;
			default:
				return -EINVAL;
			}

			entry.data[port] = tag;
		}
	}

	mutex_lock(&ps->smi_mutex);
	ret = _mv88e6xxx_vtu_loadpurge(ps, &entry);
	mutex_unlock(&ps->smi_mutex);

	return ret < 0 ? ret : count;
}

static int mv88e6xxx_vtu_open(struct inode *inode, struct file *file)
{
	return single_open(file, mv88e6xxx_vtu_show, inode->i_private);
}

static const struct file_operations mv88e6xxx_vtu_fops = {
	.open		= mv88e6xxx_vtu_open,
	.read		= seq_read,
	.write		= mv88e6xxx_vtu_write,
	.llseek		= no_llseek,
	.release	= single_release,
	.owner		= THIS_MODULE,
};

static int mv88e6xxx_stats_show(struct seq_file *s, void *p)
{
	struct mv88e6xxx_priv_state *ps = s->private;
	int stat, port;
	int err = 0;

	seq_puts(s, "          Statistic  ");
	for (port = 0; port < ps->info->num_ports; port++)
		seq_printf(s, " Port %2d ", port);
	seq_puts(s, "\n");

	mutex_lock(&ps->smi_mutex);

	for (stat = 0; stat < ARRAY_SIZE(mv88e6xxx_hw_stats); stat++) {
		struct mv88e6xxx_hw_stat *hw_stat = &mv88e6xxx_hw_stats[stat];

		if (!mv88e6xxx_has_stat(ps, hw_stat))
			continue;

		seq_printf(s, "%19s: ", hw_stat->string);
		for (port = 0 ; port < ps->info->num_ports; port++) {
			u64 value;

			err = _mv88e6xxx_stats_snapshot(ps, port);
			if (err)
				goto unlock;

			value = _mv88e6xxx_get_ethtool_stat(ps, hw_stat, port);
			seq_printf(s, "%8llu ", value);
		}
		seq_puts(s, "\n");
	}

unlock:
	mutex_unlock(&ps->smi_mutex);

	return err;
}

static int mv88e6xxx_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, mv88e6xxx_stats_show, inode->i_private);
}

static const struct file_operations mv88e6xxx_stats_fops = {
	.open   = mv88e6xxx_stats_open,
	.read   = seq_read,
	.llseek = no_llseek,
	.release = single_release,
	.owner  = THIS_MODULE,
};

static int mv88e6xxx_device_map_show(struct seq_file *s, void *p)
{
	struct mv88e6xxx_priv_state *ps = s->private;
	int target, ret;

	seq_puts(s, "Target Port\n");

	mutex_lock(&ps->smi_mutex);
	for (target = 0; target < 32; target++) {
		ret = _mv88e6xxx_reg_write(
			ps, REG_GLOBAL2, GLOBAL2_DEVICE_MAPPING,
			target << GLOBAL2_DEVICE_MAPPING_TARGET_SHIFT);
		if (ret < 0)
			goto out;
		ret = _mv88e6xxx_reg_read(ps, REG_GLOBAL2,
					  GLOBAL2_DEVICE_MAPPING);
		seq_printf(s, "  %2d   %2d\n", target,
			   ret & GLOBAL2_DEVICE_MAPPING_PORT_MASK);
	}
out:
	mutex_unlock(&ps->smi_mutex);

	return 0;
}

static int mv88e6xxx_device_map_open(struct inode *inode, struct file *file)
{
	return single_open(file, mv88e6xxx_device_map_show, inode->i_private);
}

static const struct file_operations mv88e6xxx_device_map_fops = {
	.open   = mv88e6xxx_device_map_open,
	.read   = seq_read,
	.llseek = no_llseek,
	.release = single_release,
	.owner  = THIS_MODULE,
};

/* Must be called with SMI lock held */
static int _mv88e6xxx_scratch_wait(struct mv88e6xxx_priv_state *ps)
{
	return _mv88e6xxx_wait(ps, REG_GLOBAL2, GLOBAL2_SCRATCH_MISC,
			       GLOBAL2_SCRATCH_BUSY);
}

static int mv88e6xxx_scratch_show(struct seq_file *s, void *p)
{
	struct mv88e6xxx_priv_state *ps = s->private;
	int reg, ret;

	seq_puts(s, "Register Value\n");

	mutex_lock(&ps->smi_mutex);
	for (reg = 0; reg < 0x80; reg++) {
		ret = _mv88e6xxx_reg_write(
			ps, REG_GLOBAL2, GLOBAL2_SCRATCH_MISC,
			reg << GLOBAL2_SCRATCH_REGISTER_SHIFT);
		if (ret < 0)
			goto out;

		ret = _mv88e6xxx_scratch_wait(ps);
		if (ret < 0)
			goto out;

		ret = _mv88e6xxx_reg_read(ps, REG_GLOBAL2,
					  GLOBAL2_SCRATCH_MISC);
		seq_printf(s, "  %2x   %2x\n", reg,
			   ret & GLOBAL2_SCRATCH_VALUE_MASK);
	}
out:
	mutex_unlock(&ps->smi_mutex);

	return 0;
}

static int mv88e6xxx_scratch_open(struct inode *inode, struct file *file)
{
	return single_open(file, mv88e6xxx_scratch_show, inode->i_private);
}

static const struct file_operations mv88e6xxx_scratch_fops = {
	.open   = mv88e6xxx_scratch_open,
	.read   = seq_read,
	.llseek = no_llseek,
	.release = single_release,
	.owner  = THIS_MODULE,
};

static void mv88e6xxx_init_debugfs(struct mv88e6xxx_priv_state *ps)
{
	char *name;

	name = kasprintf(GFP_KERNEL, "mv88e6xxx.%d", ps->ds->index);
	ps->dbgfs = debugfs_create_dir(name, NULL);
	kfree(name);

	debugfs_create_file("regs", S_IRUGO | S_IWUSR, ps->dbgfs, ps,
			    &mv88e6xxx_regs_fops);

	debugfs_create_file("atu", S_IRUGO | S_IWUSR, ps->dbgfs, ps,
			    &mv88e6xxx_atu_fops);

	debugfs_create_file("default_vid", S_IRUGO | S_IWUSR, ps->dbgfs, ps,
			    &mv88e6xxx_default_vid_fops);

	debugfs_create_file("fid", S_IRUGO, ps->dbgfs, ps, &mv88e6xxx_fid_fops);

	debugfs_create_file("state", S_IRUGO, ps->dbgfs, ps,
			    &mv88e6xxx_state_fops);

	debugfs_create_file("8021q_mode", S_IRUGO, ps->dbgfs, ps,
			    &mv88e6xxx_8021q_mode_fops);

	debugfs_create_file("vlan_table", S_IRUGO, ps->dbgfs, ps,
			    &mv88e6xxx_vlan_table_fops);

	debugfs_create_file("pvt", S_IRUGO | S_IWUSR, ps->dbgfs, ps,
			    &mv88e6xxx_pvt_fops);

	debugfs_create_file("vtu", S_IRUGO | S_IWUSR, ps->dbgfs, ps,
			    &mv88e6xxx_vtu_fops);

	debugfs_create_file("stats", S_IRUGO, ps->dbgfs, ps,
			    &mv88e6xxx_stats_fops);

	debugfs_create_file("device_map", S_IRUGO, ps->dbgfs, ps,
			    &mv88e6xxx_device_map_fops);

	debugfs_create_file("scratch", S_IRUGO, ps->dbgfs, ps,
			    &mv88e6xxx_scratch_fops);
}
