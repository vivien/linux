#include <linux/debugfs.h>
#include <linux/seq_file.h>

static int mv88e6xxx_regs_show(struct seq_file *s, void *p)
{
	struct dsa_switch *ds = s->private;
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int port, reg, ret;

	seq_puts(s, "    GLOBAL GLOBAL2 SERDES   ");
	for (port = 0; port < ps->num_ports; port++)
		seq_printf(s, " %2d  ", port);
	seq_puts(s, "\n");

	mutex_lock(&ps->smi_mutex);

	for (reg = 0; reg < 32; reg++) {
		seq_printf(s, "%2x:", reg);

		ret = _mv88e6xxx_reg_read(ds, REG_GLOBAL, reg);
		if (ret < 0)
			goto unlock;
		seq_printf(s, "  %4x  ", ret);

		ret = _mv88e6xxx_reg_read(ds, REG_GLOBAL2, reg);
		if (ret < 0)
			goto unlock;
		seq_printf(s, "  %4x  ", ret);

		ret = _mv88e6xxx_phy_page_read(ds, REG_FIBER_SERDES,
					       PAGE_FIBER_SERDES, reg);
		if (ret < 0)
			goto unlock;
		seq_printf(s, "  %4x  ", ret);

		for (port = 0; port < ps->num_ports; port++) {
			ret = _mv88e6xxx_reg_read(ds, REG_PORT(port), reg);
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
	struct dsa_switch *ds = s->private;
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
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

	if (strcasecmp(name, "GLOBAL") == 0)
		ret = mv88e6xxx_reg_write(ds, REG_GLOBAL, reg, val);
	else if (strcasecmp(name, "GLOBAL2") == 0)
		ret = mv88e6xxx_reg_write(ds, REG_GLOBAL2, reg, val);
	else if (strcasecmp(name, "SERDES") == 0)
		ret = mv88e6xxx_phy_page_write(ds, REG_FIBER_SERDES,
					       PAGE_FIBER_SERDES, reg, val);
	else if (kstrtouint(name, 10, &port) == 0 && port < ps->num_ports)
		ret = mv88e6xxx_reg_write(ds, REG_PORT(port), reg, val);
	else
		return -EINVAL;

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

static int mv88e6xxx_name_show(struct seq_file *s, void *p)
{
	struct dsa_switch *ds = s->private;
	int i;

	seq_puts(s, " Port  Name\n");

	for (i = 0; i < DSA_MAX_PORTS; ++i) {
		if (!ds->pd->port_names[i])
			continue;

		seq_printf(s, "%4d   %s", i, ds->pd->port_names[i]);

		if (ds->ports[i])
			seq_printf(s, " (%s)", netdev_name(ds->ports[i]));

		seq_puts(s, "\n");
	}

	return 0;
}

static int mv88e6xxx_name_open(struct inode *inode, struct file *file)
{
	return single_open(file, mv88e6xxx_name_show, inode->i_private);
}

static const struct file_operations mv88e6xxx_name_fops = {
	.open		= mv88e6xxx_name_open,
	.read		= seq_read,
	.llseek		= no_llseek,
	.release	= single_release,
	.owner		= THIS_MODULE,
};

static int mv88e6xxx_atu_show(struct seq_file *s, void *p)
{
	struct dsa_switch *ds = s->private;
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	struct mv88e6xxx_atu_entry addr = {
		.mac = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
	};
	const char *state;
	int fid, i, err;

	seq_puts(s, " FID           MAC Addr         State  Trunk?  DPV/Trunk ID\n");

	mutex_lock(&ps->smi_mutex);

	err = _mv88e6xxx_atu_mac_write(ds, addr.mac);
	if (err)
		goto unlock;

	for (fid = 0; fid < 4096; ++fid) {
		do {
			err = _mv88e6xxx_atu_getnext(ds, fid, &addr);
			if (err)
				goto unlock;

			if (addr.state == GLOBAL_ATU_DATA_STATE_UNUSED)
				break;

			/* print ATU entry */
			seq_printf(s, "%4d", addr.fid);

			seq_printf(s, "  %.2x", addr.mac[0]);
			for (i = 1; i < ETH_ALEN; ++i)
				seq_printf(s, ":%.2x", addr.mac[i]);

			switch (addr.state) {
			case GLOBAL_ATU_DATA_STATE_UC_MGMT:
				state = "UC_MGMT";
				break;
			case GLOBAL_ATU_DATA_STATE_UC_STATIC:
				state = "UC_STATIC";
				break;
			case GLOBAL_ATU_DATA_STATE_UC_PRIO_OVER:
				state = "UC_PRIO_OVER";
				break;
#if 0
			case GLOBAL_ATU_DATA_STATE_MC_NONE_RATE:
				state = "MC_NONE_RATE";
				break;
			case GLOBAL_ATU_DATA_STATE_MC_STATIC:
				state = "MC_STATIC";
				break;
			case GLOBAL_ATU_DATA_STATE_MC_MGMT:
				state = "MC_MGMT";
				break;
			case GLOBAL_ATU_DATA_STATE_MC_PRIO_OVER:
				state = "MC_PRIO_OVER";
				break;
#endif
			case GLOBAL_ATU_DATA_STATE_UNUSED:
			default:
				state = "???";
				break;
			}
			seq_printf(s, "  %12s", state);

			if (addr.trunk) {
				seq_printf(s, "       y  %d",
					   addr.portv_trunkid);
			} else {
				seq_puts(s, "       n ");
				for (i = 0; i < ps->num_ports; ++i)
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

static int mv88e6xxx_atu_open(struct inode *inode, struct file *file)
{
	return single_open(file, mv88e6xxx_atu_show, inode->i_private);
}

static const struct file_operations mv88e6xxx_atu_fops = {
	.open   = mv88e6xxx_atu_open,
	.read   = seq_read,
	.llseek = no_llseek,
	.release = single_release,
	.owner  = THIS_MODULE,
};

static int mv88e6xxx_fid_show(struct seq_file *s, void *p)
{
	struct dsa_switch *ds = s->private;
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	u16 fid;
	int i, err;

	seq_puts(s, " Port  FID\n");

	mutex_lock(&ps->smi_mutex);

	for (i = 0; i < ps->num_ports; ++i) {
		seq_printf(s, "%4d ", i);

		err = _mv88e6xxx_port_fid_get(ds, i, &fid);
		if (err)
			break;

		seq_printf(s, " %d\n", fid);
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
	struct dsa_switch *ds = s->private;
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int i, ret;

	/* header */
	seq_puts(s, " Port  Mode\n");

	mutex_lock(&ps->smi_mutex);

	/* One line per input port */
	for (i = 0; i < ps->num_ports; ++i) {
		seq_printf(s, "%4d ", i);

		ret = _mv88e6xxx_reg_read(ds, REG_PORT(i), PORT_CONTROL);
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
	struct dsa_switch *ds = s->private;
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int i, ret;

	/* header */
	seq_puts(s, " Port  Mode\n");

	mutex_lock(&ps->smi_mutex);

	/* One line per input port */
	for (i = 0; i < ps->num_ports; ++i) {
		seq_printf(s, "%4d ", i);

		ret = _mv88e6xxx_reg_read(ds, REG_PORT(i), PORT_CONTROL_2);
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
	struct dsa_switch *ds = s->private;
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int i, j, ret;

	/* header */
	seq_puts(s, " Port");
	for (i = 0; i < ps->num_ports; ++i)
		seq_printf(s, " %2d", i);
	seq_puts(s, "\n");

	mutex_lock(&ps->smi_mutex);

	/* One line per input port */
	for (i = 0; i < ps->num_ports; ++i) {
		seq_printf(s, "%4d ", i);

		ret = _mv88e6xxx_reg_read(ds, REG_PORT(i), PORT_BASE_VLAN);
		if (ret < 0)
			goto unlock;

		/* One column per output port */
		for (j = 0; j < ps->num_ports; ++j)
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

static int mv88e6xxx_vtu_show(struct seq_file *s, void *p)
{
	struct dsa_switch *ds = s->private;
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int port, ret = 0, vid = GLOBAL_VTU_VID_MASK; /* first or lowest VID */

	seq_puts(s, " VID  FID  SID");
	for (port = 0; port < ps->num_ports; ++port)
		seq_printf(s, " %2d", port);
	seq_puts(s, "\n");

	mutex_lock(&ps->smi_mutex);

	ret = _mv88e6xxx_vtu_vid_write(ds, vid);
	if (ret < 0)
		goto unlock;

	do {
		struct mv88e6xxx_vtu_stu_entry next = { 0 };

		ret = _mv88e6xxx_vtu_getnext(ds, &next);
		if (ret < 0)
			goto unlock;

		if (!next.valid)
			break;

		seq_printf(s, "%4d %4d   %2d", next.vid, next.fid, next.sid);
		for (port = 0; port < ps->num_ports; ++port) {
			switch (next.data[port]) {
			case GLOBAL_VTU_DATA_MEMBER_TAG_UNMODIFIED:
				seq_puts(s, "  -");
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
				seq_puts(s, "  ?");
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
	struct dsa_switch *ds = s->private;
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
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
	else if (ret != 3 + ps->num_ports)
		return -EINVAL;

	entry.vid = vid;
	entry.valid = valid;

	if (valid) {
		entry.fid = fid;
		entry.sid = sid;
		/* Note: The VTU entry pointed by VID will be loaded but not
		 * considered valid until the STU entry pointed by SID is valid.
		 */

		for (port = 0; port < ps->num_ports; ++port) {
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
			case '-':
				tag = GLOBAL_VTU_DATA_MEMBER_TAG_UNMODIFIED;
				break;
			default:
				return -EINVAL;
			}

			entry.data[port] = tag;
		}
	}

	mutex_lock(&ps->smi_mutex);
	ret = _mv88e6xxx_vtu_loadpurge(ds, &entry);
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
	struct dsa_switch *ds = s->private;
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int stat, port;
	int err = 0;

	seq_puts(s, "          Statistic  ");
	for (port = 0; port < ps->num_ports; port++)
		seq_printf(s, " Port %2d ", port);
	seq_puts(s, "\n");

	mutex_lock(&ps->smi_mutex);

	for (stat = 0; stat < ARRAY_SIZE(mv88e6xxx_hw_stats); stat++) {
		struct mv88e6xxx_hw_stat *hw_stat = &mv88e6xxx_hw_stats[stat];

		if (!mv88e6xxx_has_stat(ds, hw_stat))
			continue;

		seq_printf(s, "%19s: ", hw_stat->string);
		for (port = 0 ; port < ps->num_ports; port++) {
			u64 value;

			err = _mv88e6xxx_stats_snapshot(ds, port);
			if (err)
				goto unlock;

			value = _mv88e6xxx_get_ethtool_stat(ds, hw_stat, port);
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
	struct dsa_switch *ds = s->private;
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int target, ret;

	seq_puts(s, "Target Port\n");

	mutex_lock(&ps->smi_mutex);
	for (target = 0; target < 32; target++) {
		ret = _mv88e6xxx_reg_write(
			ds, REG_GLOBAL2, GLOBAL2_DEVICE_MAPPING,
			target << GLOBAL2_DEVICE_MAPPING_TARGET_SHIFT);
		if (ret < 0)
			goto out;
		ret = _mv88e6xxx_reg_read(ds, REG_GLOBAL2,
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
static int _mv88e6xxx_scratch_wait(struct dsa_switch *ds)
{
	return _mv88e6xxx_wait(ds, REG_GLOBAL2, GLOBAL2_SCRATCH_MISC,
			       GLOBAL2_SCRATCH_BUSY);
}

static int mv88e6xxx_scratch_show(struct seq_file *s, void *p)
{
	struct dsa_switch *ds = s->private;
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int reg, ret;

	seq_puts(s, "Register Value\n");

	mutex_lock(&ps->smi_mutex);
	for (reg = 0; reg < 0x80; reg++) {
		ret = _mv88e6xxx_reg_write(
			ds, REG_GLOBAL2, GLOBAL2_SCRATCH_MISC,
			reg << GLOBAL2_SCRATCH_REGISTER_SHIFT);
		if (ret < 0)
			goto out;

		ret = _mv88e6xxx_scratch_wait(ds);
		if (ret < 0)
			goto out;

		ret = _mv88e6xxx_reg_read(ds, REG_GLOBAL2,
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

static void mv88e6xxx_init_debugfs(struct dsa_switch *ds)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	char *name;

	name = kasprintf(GFP_KERNEL, "mv88e6xxx.%d", ds->index);
	ps->dbgfs = debugfs_create_dir(name, NULL);
	kfree(name);

	debugfs_create_file("regs", S_IRUGO | S_IWUSR, ps->dbgfs, ds,
			    &mv88e6xxx_regs_fops);

	debugfs_create_file("name", S_IRUGO, ps->dbgfs, ds,
			    &mv88e6xxx_name_fops);

	debugfs_create_file("atu", S_IRUGO, ps->dbgfs, ds, &mv88e6xxx_atu_fops);

	debugfs_create_file("fid", S_IRUGO, ps->dbgfs, ds, &mv88e6xxx_fid_fops);

	debugfs_create_file("state", S_IRUGO, ps->dbgfs, ds,
			    &mv88e6xxx_state_fops);

	debugfs_create_file("8021q_mode", S_IRUGO, ps->dbgfs, ds,
			    &mv88e6xxx_8021q_mode_fops);

	debugfs_create_file("vlan_table", S_IRUGO, ps->dbgfs, ds,
			    &mv88e6xxx_vlan_table_fops);

	debugfs_create_file("vtu", S_IRUGO | S_IWUSR, ps->dbgfs, ds,
			    &mv88e6xxx_vtu_fops);

	debugfs_create_file("stats", S_IRUGO, ps->dbgfs, ds,
			    &mv88e6xxx_stats_fops);

	debugfs_create_file("device_map", S_IRUGO, ps->dbgfs, ds,
			    &mv88e6xxx_device_map_fops);

	debugfs_create_file("scratch", S_IRUGO, ps->dbgfs, ds,
			    &mv88e6xxx_scratch_fops);
}
