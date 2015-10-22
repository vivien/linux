#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include "chip.h"
#include "debugfs.h"
#include "global1.h"
#include "global2.h"
#include "global3.h"
#include "phy.h"
#include "port.h"
#include "serdes.h"

static struct dentry *mv88e6xxx_dbg_dir;

struct mv88e6xxx_dbg_ops {
	int (*read)(struct mv88e6xxx_chip *chip, int id, struct seq_file *seq);
	int (*write)(struct mv88e6xxx_chip *chip, int id, char *buf);
};

struct mv88e6xxx_dbg_priv {
	const struct mv88e6xxx_dbg_ops *ops;
	struct mv88e6xxx_chip *chip;
	int id;
};

static int mv88e6xxx_dbg_show(struct seq_file *seq, void *p)
{
	struct mv88e6xxx_dbg_priv *priv = seq->private;
	struct mv88e6xxx_chip *chip = priv->chip;
	int err;

	if (!priv->ops->read)
		return -EOPNOTSUPP;

	mv88e6xxx_reg_lock(chip);
	err = priv->ops->read(chip, priv->id, seq);
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static ssize_t mv88e6xxx_dbg_write(struct file *file,
				   const char __user *user_buf, size_t count,
				   loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct mv88e6xxx_dbg_priv *priv = seq->private;
	struct mv88e6xxx_chip *chip = priv->chip;
	char buf[256];
	int err;

	if (!priv->ops->write)
		return -EOPNOTSUPP;

	if (sizeof(buf) <= count)
		return -E2BIG;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	buf[count] = '\0';

	mv88e6xxx_reg_lock(chip);
	err = priv->ops->write(chip, priv->id, buf);
	mv88e6xxx_reg_unlock(chip);

	return err ? err : count;
}

static int mv88e6xxx_dbg_open(struct inode *inode, struct file *file)
{
	return single_open(file, mv88e6xxx_dbg_show, inode->i_private);
}

static const struct file_operations mv88e6xxx_dbg_fops = {
	.open = mv88e6xxx_dbg_open,
	.read = seq_read,
	.write = mv88e6xxx_dbg_write,
	.llseek = no_llseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static void mv88e6xxx_dbg_create_file(struct mv88e6xxx_chip *chip,
				      struct dentry *dir, char *name, int id,
				      const struct mv88e6xxx_dbg_ops *ops)
{
	struct mv88e6xxx_dbg_priv *priv;
	umode_t mode;

	priv = devm_kzalloc(chip->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return;

	priv->chip = chip;
	priv->ops = ops;
	priv->id = id;

	mode = 0;
	if (ops->read)
		mode |= S_IRUGO;
	if (ops->write)
		mode |= S_IWUSR;

	debugfs_create_file(name, mode, dir, priv, &mv88e6xxx_dbg_fops);
}

static const char * const mv88e6xxx_port_8021q_mode_names[] = {
	[MV88E6XXX_PORT_CTL2_8021Q_MODE_DISABLED] = "Disabled",
	[MV88E6XXX_PORT_CTL2_8021Q_MODE_FALLBACK] = "Fallback",
	[MV88E6XXX_PORT_CTL2_8021Q_MODE_CHECK] = "Check",
	[MV88E6XXX_PORT_CTL2_8021Q_MODE_SECURE] = "Secure",
};

static int mv88e6xxx_dbg_8021q_mode_read(struct mv88e6xxx_chip *chip, int id,
					 struct seq_file *seq)
{
	u16 val;
	int err;

	err = mv88e6xxx_port_read(chip, id, MV88E6XXX_PORT_CTL2, &val);
	if (err)
		return err;

	val &= MV88E6XXX_PORT_CTL2_8021Q_MODE_MASK;

	seq_printf(seq, " %s\n", mv88e6xxx_port_8021q_mode_names[val]);

	return 0;
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_8021q_mode_ops = {
	.read = mv88e6xxx_dbg_8021q_mode_read,
};

static int mv88e6xxx_g1_atu_get_age_time(struct mv88e6xxx_chip *chip,
					 unsigned int *msecs)
{
	u8 age_time;
	u16 val;
	int err;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_ATU_CTL, &val);
	if (err)
		return err;

	/* AgeTime is 11:4 bits */
	age_time = (val & 0xff0) >> 4;
	*msecs = age_time * chip->info->age_time_coeff;

	return 0;
}

static int mv88e6xxx_dbg_age_time_read(struct mv88e6xxx_chip *chip, int id,
				       struct seq_file *seq)
{
	unsigned int msecs;
	int err;

	err = mv88e6xxx_g1_atu_get_age_time(chip, &msecs);
	if (err)
		return err;

	seq_printf(seq, "%d\n", msecs);

	return 0;
}

static int mv88e6xxx_dbg_age_time_write(struct mv88e6xxx_chip *chip, int id,
					char *buf)
{
	unsigned int msecs;

	if (kstrtouint(buf, 10, &msecs))
		return -EINVAL;

	return mv88e6xxx_g1_atu_set_age_time(chip, msecs);
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_age_time_ops = {
	.read = mv88e6xxx_dbg_age_time_read,
	.write = mv88e6xxx_dbg_age_time_write,
};

static const char * const mv88e6xxx_atu_unicast_state_names[] = {
	[0x0] = "UC_UNUSED",
	[0x1] = "UC_AGE_1_OLDEST",
	[0x2] = "UC_AGE_2",
	[0x3] = "UC_AGE_3",
	[0x4] = "UC_AGE_4",
	[0x5] = "UC_AGE_5",
	[0x6] = "UC_AGE_6",
	[0x7] = "UC_AGE_7_NEWEST",
	[0x8] = "UC_STATIC_POLICY",
	[0x9] = "UC_STATIC_POLICY_PO",
	[0xa] = "UC_STATIC_AVB_NRL",
	[0xb] = "UC_STATIC_AVB_NRL_PO",
	[0xc] = "UC_STATIC_DA_MGMT",
	[0xd] = "UC_STATIC_DA_MGMT_PO",
	[0xe] = "UC_STATIC",
	[0xf] = "UC_STATIC_PO",
};

static const char * const mv88e6xxx_atu_multicast_state_names[] = {
	[0x0] = "MC_UNUSED",
	[0x1] = "MC_RESERVED",
	[0x2] = "MC_RESERVED",
	[0x3] = "MC_RESERVED",
	[0x4] = "MC_STATIC_POLICY",
	[0x5] = "MC_STATIC_AVB_NRL",
	[0x6] = "MC_STATIC_DA_MGMT",
	[0x7] = "MC_STATIC",
	[0x8] = "MC_RESERVED",
	[0x9] = "MC_RESERVED",
	[0xa] = "MC_RESERVED",
	[0xb] = "MC_RESERVED",
	[0xc] = "MC_STATIC_POLICY_PO",
	[0xd] = "MC_STATIC_AVB_NRL_PO",
	[0xe] = "MC_STATIC_DA_MGMT_PO",
	[0xf] = "MC_STATIC_PO",
};

static void mv88e6xxx_dbg_atu_puts(struct mv88e6xxx_chip *chip,
				   struct seq_file *seq,
				   const struct mv88e6xxx_atu_entry *entry)
{
	int port;

	seq_printf(seq, "fid %d", entry->fid);

	seq_printf(seq, "\tmac %.2x:%.2x:%.2x:%.2x:%.2x:%.2x",
		   entry->mac[0], entry->mac[1], entry->mac[2],
		   entry->mac[3], entry->mac[4], entry->mac[5]);

	if (entry->trunk) {
		seq_printf(seq, "\ttrunk %d", entry->portvec);
	} else {
		seq_puts(seq, "\tdpv");
		for (port = 0; port < mv88e6xxx_num_ports(chip); port++)
			if (entry->portvec & BIT(port))
				seq_printf(seq, " %d", port);
			else
				seq_puts(seq, " -");
	}

	seq_printf(seq, "\tstate %s", is_multicast_ether_addr(entry->mac) ?
		   mv88e6xxx_atu_multicast_state_names[entry->state] :
		   mv88e6xxx_atu_unicast_state_names[entry->state]);

	seq_puts(seq, "\n");
}

static int mv88e6xxx_dbg_atu_read(struct mv88e6xxx_chip *chip, int id,
				  struct seq_file *seq)
{
	struct mv88e6xxx_atu_entry next;
	int i, err;

	for (i = 0; i < mv88e6xxx_num_databases(chip); i++) {
		next.state = 0; /* Write MAC address and FID to iterate from */
		eth_broadcast_addr(next.mac);
		next.fid = i;

		 do {
			err = mv88e6xxx_g1_atu_getnext(chip, &next);
			if (err)
				return err;

			if (!next.state)
				break;

			mv88e6xxx_dbg_atu_puts(chip, seq, &next);
		} while (!is_broadcast_ether_addr(next.mac));

		/* Dumping up to 4096 databases can take a while, so allow
		 * interrupting the dump after each successful database dump.
		 */
		if (fatal_signal_pending(current))
			break;
	}

	return 0;
}

static int mv88e6xxx_dbg_atu_write(struct mv88e6xxx_chip *chip, int id,
				   char *buf)
{
	struct mv88e6xxx_atu_entry entry = { 0 };
	unsigned int port;
	int ret;

	ret = sscanf(buf, "fid %hu mac %2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx port %u state %hhx",
		     &entry.fid,
		     &entry.mac[0], &entry.mac[1], &entry.mac[2],
		     &entry.mac[3], &entry.mac[4], &entry.mac[5],
		     &port, &entry.state);
	if (ret == 0)
		return mv88e6xxx_g1_atu_flush(chip, 0, true);

	if (ret == 1)
		return mv88e6xxx_g1_atu_flush(chip, entry.fid, true);

	if (ret < 7)
		return -EINVAL;

	if (ret == 7)
		return mv88e6xxx_g1_atu_loadpurge(chip, &entry);

	if (port >= mv88e6xxx_num_ports(chip))
		return -ERANGE;

	if (ret == 8)
		return mv88e6xxx_g1_atu_remove(chip, entry.fid, port, true);

	entry.trunk = false;
	entry.portvec = BIT(port);

	if (entry.state & ~MV88E6XXX_G1_ATU_DATA_STATE_MASK)
		return -EINVAL;

	if (ret == 9)
		return mv88e6xxx_g1_atu_loadpurge(chip, &entry);

	return -EINVAL;
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_atu_ops = {
		.read = mv88e6xxx_dbg_atu_read,
		.write = mv88e6xxx_dbg_atu_write,
};

static int mv88e6xxx_dbg_atu_stats_type(struct mv88e6xxx_chip *chip,
					int id, struct seq_file *seq,
					u16 stats_type)
{
	struct mv88e6xxx_atu_entry next;
	int err, bin, total = 0;
	u16 reg, val;

	for (bin = 0; bin < 4; bin++) {
		reg = bin << MV88E6XXX_G2_ATU_STATS_BIN_SHIFT | stats_type;

		err = mv88e6xxx_g2_write(chip, MV88E6XXX_G2_ATU_STATS, reg);
		if (err)
			return err;

		next.state = 0;
		next.fid = id;
		eth_broadcast_addr(next.mac);

		err = mv88e6xxx_g1_atu_getnext(chip, &next);
		if (err)
			return err;

		err = mv88e6xxx_g2_read(chip, MV88E6XXX_G2_ATU_STATS, &val);
		if (err)
			return err;

		val &= MV88E6XXX_G2_ATU_STATS_MASK;
		total += val;

		seq_printf(seq, "%5d ", val);
	}
	seq_printf(seq, "%5d\n", total);

	return 0;
}

static int mv88e6xxx_dbg_atu_stats_read(struct mv88e6xxx_chip *chip,
					int id, struct seq_file *seq)
{
	int err;

	seq_printf(seq, "FID     type  bin0  bin1  bin2  bin3  total\n");
	seq_printf(seq, "%4d     all ", id);

	err = mv88e6xxx_dbg_atu_stats_type(chip, id, seq,
					   MV88E6XXX_G2_ATU_STATS_ALL_FID);
	if (err)
		return err;

	seq_printf(seq, "%4d dynamic ", id);

	err = mv88e6xxx_dbg_atu_stats_type(chip, id, seq,
					   MV88E6XXX_G2_ATU_STATS_DYNAMIC_FID);
	return err;
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_atu_stats_ops = {
		.read = mv88e6xxx_dbg_atu_stats_read,
};

static int mv88e6xxx_dbg_default_vid_read(struct mv88e6xxx_chip *chip, int id,
					  struct seq_file *seq)
{
	u16 pvid;
	int err;

	err = mv88e6xxx_port_get_pvid(chip, id, &pvid);
	if (err)
		return err;

	seq_printf(seq, "%d\n", pvid);

	return 0;
}

static int mv88e6xxx_dbg_default_vid_write(struct mv88e6xxx_chip *chip, int id,
					   char *buf)
{
	u16 pvid;

	if (kstrtou16(buf, 10, &pvid))
		return -EINVAL;

	if (pvid >= VLAN_N_VID)
		return -ERANGE;

	return mv88e6xxx_port_set_pvid(chip, id, pvid);
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_default_vid_ops = {
	.read = mv88e6xxx_dbg_default_vid_read,
	.write = mv88e6xxx_dbg_default_vid_write,
};

static int mv88e6xxx_dbg_device_map_read(struct mv88e6xxx_chip *chip, int id,
					 struct seq_file *seq)
{
	u16 val;
	int target, err;

	/* TODO: write mv88e6xxx_g2_device_mapping_read() */
	for (target = 0; target < 32; target++) {
		val = target << __bf_shf(MV88E6XXX_G2_DEVICE_MAPPING_DEV_MASK);
		err = mv88e6xxx_write(chip, chip->info->global2_addr,
				      MV88E6XXX_G2_DEVICE_MAPPING, val);
		if (err)
			return err;

		err = mv88e6xxx_read(chip, chip->info->global2_addr,
				     MV88E6XXX_G2_DEVICE_MAPPING, &val);
		if (err)
			return err;

		/* bit 5 is unused on older chips */
		seq_printf(seq, "target %d port %d\n", target,
			   val & MV88E6390_G2_DEVICE_MAPPING_PORT_MASK);
	}

	return 0;
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_device_map_ops = {
	.read = mv88e6xxx_dbg_device_map_read,
};

static int mv88e6xxx_dbg_fid_read(struct mv88e6xxx_chip *chip, int id,
				  struct seq_file *seq)
{
	u16 fid;
	int err;

	err = mv88e6xxx_port_get_fid(chip, id, &fid);
	if (err)
		return err;

	seq_printf(seq, "%d\n", fid);

	return 0;
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_fid_ops = {
	.read = mv88e6xxx_dbg_fid_read,
};

static int mv88e6xxx_dbg_name_read(struct mv88e6xxx_chip *chip, int id,
				   struct seq_file *seq)
{
	seq_printf(seq, "%s\n", chip->info->name);

	return 0;
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_name_ops = {
	.read = mv88e6xxx_dbg_name_read,
};

int mv88e6xxx_g2_pvt_read(struct mv88e6xxx_chip *chip, int src_dev,
			  int src_port, u16 *data);

static struct dsa_switch *mv88e6xxx_ds(struct mv88e6xxx_chip *chip, int device)
{
	struct dsa_switch_tree *dst = chip->ds->dst;
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dp->ds->index == device)
			return dp->ds;

	return NULL;
}

static int mv88e6xxx_dbg_pvt_read(struct mv88e6xxx_chip *chip, int id,
				  struct seq_file *seq)
{
	struct dsa_switch *ds;
	int src_dev, src_port;
	u16 pvlan;
	int port;
	int err;

	for (src_dev = 0; src_dev < 32; src_dev++) {
		ds = mv88e6xxx_ds(chip, src_dev);
		if (!ds)
			break;

		for (src_port = 0; src_port < ds->num_ports; src_port++) {
			err = mv88e6xxx_g2_pvt_read(chip, src_dev, src_port,
						    &pvlan);
			if (err)
				return err;

			seq_printf(seq, "src dev %d port %d pvlan",
				   src_dev, src_port);

			for (port = 0; port < mv88e6xxx_num_ports(chip); port++)
				if (pvlan & BIT(port))
					seq_printf(seq, " %d", port);
				else
					seq_puts(seq, " -");

			seq_puts(seq, "\n");
		}
	}

	return 0;
}

static int mv88e6xxx_dbg_pvt_write(struct mv88e6xxx_chip *chip, int id,
				   char *buf)
{
	const u16 mask = mv88e6xxx_port_mask(chip);
	unsigned int src_dev, src_port, pvlan;

	if (sscanf(buf, "%d %d %x", &src_dev, &src_port, &pvlan) != 3)
		return -EINVAL;

	if (src_dev >= 32 || src_port >= 16 || pvlan & ~mask)
		return -ERANGE;

	return mv88e6xxx_g2_pvt_write(chip, src_dev, src_port, pvlan);
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_pvt_ops = {
	.read = mv88e6xxx_dbg_pvt_read,
	.write = mv88e6xxx_dbg_pvt_write,
};

enum {
	/* Port Registers at 0 ... DSA_MAX_PORTS - 1 */
	MV88E6XXX_DBG_REGS_ID_GLOBAL1 = DSA_MAX_PORTS,
	MV88E6XXX_DBG_REGS_ID_GLOBAL2,
	MV88E6XXX_DBG_REGS_ID_SERDES,
};

static int mv88e6xxx_serdes_read(struct mv88e6xxx_chip *chip, int reg,
				 u16 *val)
{
	return mv88e6xxx_phy_page_read(chip, MV88E6352_ADDR_SERDES,
				       MV88E6352_SERDES_PAGE_FIBER,
				       reg, val);
}

static int mv88e6xxx_serdes_write(struct mv88e6xxx_chip *chip, int reg,
				  u16 val)
{
	return mv88e6xxx_phy_page_write(chip, MV88E6352_ADDR_SERDES,
					MV88E6352_SERDES_PAGE_FIBER,
					reg, val);
}

static int mv88e6xxx_dbg_regs_read(struct mv88e6xxx_chip *chip, int id,
				   struct seq_file *seq)
{
	u16 val;
	int reg;
	int err;

	/* Label */
	if (id == MV88E6XXX_DBG_REGS_ID_SERDES)
		seq_printf(seq, "SerDes@%d\n", chip->ds->index);
	else if (id == MV88E6XXX_DBG_REGS_ID_GLOBAL2)
		seq_printf(seq, "Global2@%d\n", chip->ds->index);
	else if (id == MV88E6XXX_DBG_REGS_ID_GLOBAL1)
		seq_printf(seq, "Global1@%d\n", chip->ds->index);
	else
		seq_printf(seq, "Port %d.%d\n", chip->ds->index, id);

	for (reg = 0; reg < 32; ++reg) {
		if (id == MV88E6XXX_DBG_REGS_ID_SERDES)
			err = mv88e6xxx_serdes_read(chip, reg, &val);
		else if (id == MV88E6XXX_DBG_REGS_ID_GLOBAL2)
			err = mv88e6xxx_read(chip, chip->info->global2_addr,
					     reg, &val);
		else if (id == MV88E6XXX_DBG_REGS_ID_GLOBAL1)
			err = mv88e6xxx_g1_read(chip, reg, &val);
		else
			err = mv88e6xxx_port_read(chip, id, reg, &val);
		if (err)
			break;

		seq_printf(seq, "%2d: %4x\n", reg, val);
	}

	return err;
}

static int mv88e6xxx_dbg_regs_write(struct mv88e6xxx_chip *chip, int id,
				    char *buf)
{
	unsigned int reg, val;
	int err;

	if (sscanf(buf, "%x %x", &reg, &val) != 2)
		return -EINVAL;

	if (reg > 0x1f || val > 0xffff)
		return -ERANGE;

	if (id == MV88E6XXX_DBG_REGS_ID_SERDES)
		err = mv88e6xxx_serdes_write(chip, reg, val);
	else if (id == MV88E6XXX_DBG_REGS_ID_GLOBAL2)
		err = mv88e6xxx_write(chip, chip->info->global2_addr, reg, val);
	else if (id == MV88E6XXX_DBG_REGS_ID_GLOBAL1)
		err = mv88e6xxx_g1_write(chip, reg, val);
	else
		err = mv88e6xxx_port_write(chip, id, reg, val);

	return err;
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_regs_ops = {
	.read = mv88e6xxx_dbg_regs_read,
	.write = mv88e6xxx_dbg_regs_write,
};

static int mv88e6xxx_scratch_wait(struct mv88e6xxx_chip *chip)
{
	int bit = __bf_shf(MV88E6XXX_G2_SCRATCH_MISC_UPDATE);

	return mv88e6xxx_wait_bit(chip, chip->info->global2_addr,
			      MV88E6XXX_G2_SCRATCH_MISC_MISC, bit, 0);
}

static int mv88e6xxx_dbg_scratch_read(struct mv88e6xxx_chip *chip, int id,
				      struct seq_file *seq)
{
	u16 val;
	int reg, err;

	seq_puts(seq, "Register Value\n");

	for (reg = 0; reg < 0x80; ++reg) {
		val = reg << __bf_shf(MV88E6XXX_G2_SCRATCH_MISC_PTR_MASK);
		err = mv88e6xxx_write(chip, chip->info->global2_addr,
				      MV88E6XXX_G2_SCRATCH_MISC_MISC, val);
		if (err)
			return err;

		err = mv88e6xxx_scratch_wait(chip);
		if (err)
			return err;

		err = mv88e6xxx_read(chip, chip->info->global2_addr,
				     MV88E6XXX_G2_SCRATCH_MISC_MISC, &val);
		if (err)
			return err;

		seq_printf(seq, "  %2x   %2x\n", reg,
			   val & MV88E6XXX_G2_SCRATCH_MISC_DATA_MASK);
	}

	return 0;
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_scratch_ops = {
	.read = mv88e6xxx_dbg_scratch_read,
};

static const char * const mv88e6xxx_port_state_names[] = {
	[MV88E6XXX_PORT_CTL0_STATE_DISABLED] = "Disabled",
	[MV88E6XXX_PORT_CTL0_STATE_BLOCKING] = "Blocking", /* /Listening */
	[MV88E6XXX_PORT_CTL0_STATE_LEARNING] = "Learning",
	[MV88E6XXX_PORT_CTL0_STATE_FORWARDING] = "Forwarding",
};

int mv88e6xxx_port_get_state(struct mv88e6xxx_chip *chip, int port, u8 *state);

static int mv88e6xxx_dbg_state_read(struct mv88e6xxx_chip *chip, int id,
				    struct seq_file *seq)
{
	u8 state;
	int err;

	err = mv88e6xxx_port_get_state(chip, id, &state);
	if (err)
		return err;

	seq_printf(seq, " %s\n", mv88e6xxx_port_state_names[state]);

	return 0;
}

static int mv88e6xxx_dbg_state_write(struct mv88e6xxx_chip *chip, int id,
				     char *buf)
{
	int state;

	for (state = 0; state < ARRAY_SIZE(mv88e6xxx_port_state_names); ++state)
		if (strncasecmp(mv88e6xxx_port_state_names[state], buf,
				strlen(mv88e6xxx_port_state_names[state])) == 0)
			return mv88e6xxx_port_set_state(chip, id, state);

	return -EINVAL;
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_state_ops = {
	.read = mv88e6xxx_dbg_state_read,
	.write = mv88e6xxx_dbg_state_write,
};

int mv88e6095_stats_get_stats(struct mv88e6xxx_chip *chip, int port,
			      uint64_t *data);

int mv88e6320_stats_get_stats(struct mv88e6xxx_chip *chip, int port,
			      uint64_t *data);

int mv88e6390_stats_get_stats(struct mv88e6xxx_chip *chip, int port,
			      uint64_t *data);

extern struct mv88e6xxx_hw_stat mv88e6xxx_hw_stats[];
extern int mv88e6xxx_hw_stats_size;

static int mv88e6xxx_stats_snapshot(struct mv88e6xxx_chip *chip, int port)
{
	if (!chip->info->ops->stats_snapshot)
		return -EOPNOTSUPP;

	return chip->info->ops->stats_snapshot(chip, port);
}

uint64_t _mv88e6xxx_get_ethtool_stat(struct mv88e6xxx_chip *chip,
				     struct mv88e6xxx_hw_stat *s,
				     int port, u16 bank1_select,
				     u16 histogram);

static int mv88e6xxx_dbg_stats_read(struct mv88e6xxx_chip *chip, int id,
				    struct seq_file *seq)
{
	int port = id;
	int stat;
	int err;
	int types = 0;
	u16 bank1_select;
	u16 histogram;
	u64 value;

	if (chip->info->ops->stats_get_stats == mv88e6095_stats_get_stats) {
		types = STATS_TYPE_BANK0 | STATS_TYPE_PORT;
		bank1_select = 0;
		histogram = MV88E6XXX_G1_STATS_OP_HIST_RX_TX;
	}

	if (chip->info->ops->stats_get_stats == mv88e6320_stats_get_stats) {
		types = STATS_TYPE_BANK0 | STATS_TYPE_BANK1;
		bank1_select = MV88E6XXX_G1_STATS_OP_BANK_1_BIT_9;
		histogram = MV88E6XXX_G1_STATS_OP_HIST_RX_TX;
	}

	if (chip->info->ops->stats_get_stats == mv88e6390_stats_get_stats) {
		types = STATS_TYPE_BANK0 | STATS_TYPE_BANK1;
		bank1_select = MV88E6XXX_G1_STATS_OP_BANK_1_BIT_10;
		histogram = 0;
	}

	seq_printf(seq, "         Stat       Port %d.%d\n", chip->ds->index,
		   port);

	for (stat = 0; stat < mv88e6xxx_hw_stats_size; ++stat) {
		struct mv88e6xxx_hw_stat *hw_stat = &mv88e6xxx_hw_stats[stat];

		if (!(hw_stat->type & types))
			continue;

		seq_printf(seq, "%19s: ", hw_stat->string);

		err = mv88e6xxx_stats_snapshot(chip, port);
		if (err)
			return err;

		value = _mv88e6xxx_get_ethtool_stat(chip, hw_stat, port,
						    bank1_select, histogram);
		seq_printf(seq, "%8llu \n", value);
	}

	return 0;
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_stats_ops = {
	.read = mv88e6xxx_dbg_stats_read,
};

static char *mv88e6xxx_dbg_tcam_frame_type_str(int frame_type)
{
	switch (frame_type) {
	case GLOBAL3_P0_KEY1_FRAME_TYPE_NORNAL:
		return "Frame type: Nornal";
	case GLOBAL3_P0_KEY1_FRAME_TYPE_DSA:
		return "frame type: DSA";
	case GLOBAL3_P0_KEY1_FRAME_TYPE_PROVIDER:
		return "frame type: Provider";
	default:
		return "frame type: Unknown";
	}
}

static int mv88e6xxx_dbg_tcam_read_entry(struct mv88e6xxx_chip *chip,
					 struct seq_file *s, int entry,
					 struct mv88e6xxx_tcam_data *data)
{
	int err, i, value;
	u8 octet, mask;

	seq_puts(s, "      Dst          Src          Tag      Type Data\n");
	seq_printf(s, "Entry %3d\n", entry);
	seq_puts(s, "Octet:");
	for (i = 0; i < 48; i++) {
		/* -Dst-------Src-------Tag--------Eth Type----Data-- */
		if (i == 6 || i == 12 || i == 16 || i == 18 || i == 26 ||
		    i == 34 || i == 42)
			seq_puts(s, " ");

		err = mv88e6xxx_g3_tcam_get_match(chip, data, i, &octet, &mask);
		if (err)
			return err;
		seq_printf(s, "%02x", octet);
	}
	seq_puts(s, "\n");

	seq_puts(s, "Mask: ");
	for (i = 0; i < 48; i++) {
		/* -Dst-------Src-------Tag--------Eth Type----Data-- */
		if (i == 6 || i == 12 || i == 16 || i == 18 || i == 26 ||
		    i == 34 || i == 42)
			seq_puts(s, " ");

		err = mv88e6xxx_g3_tcam_get_match(chip, data, i, &octet, &mask);
		if (err)
			return err;
		seq_printf(s, "%02x", mask);
	}
	seq_puts(s, "\n");

	mv88e6xxx_g3_tcam_get(chip, data, MV88E6XXX_P0_KEY1_FRAME_TYPE,
			   &value);
	if (value != MV88E6XXX_TCAM_PARAM_DISABLED)
		seq_printf(s, "%s ", mv88e6xxx_dbg_tcam_frame_type_str(value));

	mv88e6xxx_g3_tcam_get(chip, data, MV88E6XXX_P0_KEY2_SRC_PORT_VECTOR,
			   &value);
	if (value != MV88E6XXX_TCAM_PARAM_DISABLED)
		seq_printf(s, "Source port vector: %x ", value);

	mv88e6xxx_g3_tcam_get(chip, data, MV88E6XXX_P0_KEY3_PPRI, &value);
	if (value != MV88E6XXX_TCAM_PARAM_DISABLED)
		seq_printf(s, "Provider priority: %d ", value);

	mv88e6xxx_g3_tcam_get(chip, data, MV88E6XXX_P0_KEY4_PVID, &value);
	if (value != MV88E6XXX_TCAM_PARAM_DISABLED)
		seq_printf(s, "Provider VLAN ID: %d ", value);

	mv88e6xxx_g3_tcam_get(chip, data, MV88E6XXX_P2_ACTION1_INTERRUPT,
			   &value);
	seq_printf(s, "Interrupt: %d ",
		   value == GLOBAL3_P2_ACTION1_INTERRUPT);

	mv88e6xxx_g3_tcam_get(chip, data, MV88E6XXX_P2_ACTION1_INC_TCAM_COUNTER,
			   &value);
	seq_printf(s, "Inc TCAM counter: %d ",
		   value == GLOBAL3_P2_ACTION1_INC_TCAM_COUNTER);

	mv88e6xxx_g3_tcam_get(chip, data, MV88E6XXX_P2_ACTION1_VID, &value);
	if (value != MV88E6XXX_TCAM_PARAM_DISABLED)
		seq_printf(s, "VID: %d ", value);

	mv88e6xxx_g3_tcam_get(chip, data, MV88E6XXX_P2_ACTION2_FLOW_ID, &value);
	if (value != MV88E6XXX_TCAM_PARAM_DISABLED)
		seq_printf(s, "Flow ID: %d ",
			   value - GLOBAL3_P2_ACTION2_FLOW_ID_0);

	mv88e6xxx_g3_tcam_get(chip, data, MV88E6XXX_P2_ACTION2_QPRI, &value);
	if (value != MV88E6XXX_TCAM_PARAM_DISABLED)
		seq_printf(s, "Queue priority: %d ",
			   value - GLOBAL3_P2_ACTION2_QPRI_0);

	mv88e6xxx_g3_tcam_get(chip, data, MV88E6XXX_P2_ACTION2_FPRI, &value);
	if (value != MV88E6XXX_TCAM_PARAM_DISABLED)
		seq_printf(s, "Priority: %d ",
			   value - GLOBAL3_P2_ACTION2_FPRI_0);

	mv88e6xxx_g3_tcam_get(chip, data, MV88E6XXX_P2_ACTION3_DST_PORT_VECTOR,
			   &value);
	if (value != MV88E6XXX_TCAM_PARAM_DISABLED)
		seq_printf(s, "Destination port vector: %x ", value);

	mv88e6xxx_g3_tcam_get(chip, data, MV88E6XXX_P2_ACTION4_FRAME_ACTION,
			   &value);
	if (value != MV88E6XXX_TCAM_PARAM_DISABLED) {
		seq_printf(s, "Frame Action: %x ", value);
		if (value & GLOBAL3_P2_ACTION4_FRAME_ACTION_SRC_IS_TAGGED)
			seq_puts(s, "SRC_IS_TAGGED ");
		if (value & GLOBAL3_P2_ACTION4_FRAME_ACTION_PVID)
			seq_puts(s, "PVID ");
		if (value & GLOBAL3_P2_ACTION4_FRAME_ACTION_MGMT)
			seq_puts(s, "MGMT ");
		if (value & GLOBAL3_P2_ACTION4_FRAME_ACTION_SNOOP)
			seq_puts(s, "SNOOP ");
		if (value & GLOBAL3_P2_ACTION4_FRAME_ACTION_POLICY_MIRROR)
			seq_puts(s, "POLICY_MIRROR ");
		if (value & GLOBAL3_P2_ACTION4_FRAME_ACTION_POLICY_TRAP)
			seq_puts(s, "POLICY_TRAP ");
		if (value & GLOBAL3_P2_ACTION4_FRAME_ACTION_SANRL)
			seq_puts(s, "SaNRL ");
		if (value & GLOBAL3_P2_ACTION4_FRAME_ACTION_DANRL)
			seq_puts(s, "DaNRL ");
	}

	mv88e6xxx_g3_tcam_get(chip, data, MV88E6XXX_P2_ACTION4_LOAD_BALANCE,
			   &value);
	if (value != MV88E6XXX_TCAM_PARAM_DISABLED)
		seq_printf(s, "Load balance: %d", value);

	mv88e6xxx_g3_tcam_get(chip, data, MV88E6XXX_P2_DEBUG_PORT, &value);
	seq_printf(s, "Debug Port: %d ", value);
	mv88e6xxx_g3_tcam_get(chip, data, MV88E6XXX_P2_DEBUG_HIT, &value);
	seq_printf(s, "Debug Hit %x\n", value);

	return 0;
}

static int mv88e6xxx_dbg_tcam_read(struct mv88e6xxx_chip *chip, int id,
				   struct seq_file *seq)
{
	struct mv88e6xxx_tcam_data data;
	int err;

	err = mv88e6xxx_g3_tcam_read(chip, id, &data);
	if (err)
		return err;

	return mv88e6xxx_dbg_tcam_read_entry(chip, seq, id, &data);
}

static int mv88e6xxx_dbg_tcam_write(struct mv88e6xxx_chip *chip, int id,
				    char *buf)
{
	struct mv88e6xxx_tcam_data data;

	memset(&data, 0, sizeof(data));

	mv88e6xxx_g3_tcam_flush_all(chip);
	mv88e6xxx_port_enable_tcam(chip, 0);
	mv88e6xxx_port_enable_tcam(chip, 1);

	/* Destination - Broadcast address */
	mv88e6xxx_g3_tcam_set_match(chip, &data, 0, 0xff, 0xff);
	mv88e6xxx_g3_tcam_set_match(chip, &data, 1, 0xff, 0xff);
	mv88e6xxx_g3_tcam_set_match(chip, &data, 2, 0xff, 0xff);
	mv88e6xxx_g3_tcam_set_match(chip, &data, 3, 0xff, 0xff);
	mv88e6xxx_g3_tcam_set_match(chip, &data, 4, 0xff, 0xff);
	mv88e6xxx_g3_tcam_set_match(chip, &data, 5, 0xff, 0xff);

	/* Source Port 0 */
	mv88e6xxx_g3_tcam_set(chip, &data, MV88E6XXX_P0_KEY2_SRC_PORT_VECTOR,
			   (1 << 0));

	/* Destination port None, i.e. drop */
	mv88e6xxx_g3_tcam_set(chip, &data, MV88E6XXX_P2_ACTION3_DST_PORT_VECTOR,
			   0);

	mv88e6xxx_g3_tcam_load_entry(chip, 42, &data);

	memset(&data, 0, sizeof(data));

	/* Source 00:26:55:d2:27:a9 */
	mv88e6xxx_g3_tcam_set_match(chip, &data, 6, 0x00, 0xff);
	mv88e6xxx_g3_tcam_set_match(chip, &data, 7, 0x26, 0xff);
	mv88e6xxx_g3_tcam_set_match(chip, &data, 8, 0x55, 0xff);
	mv88e6xxx_g3_tcam_set_match(chip, &data, 9, 0xd2, 0xff);
	mv88e6xxx_g3_tcam_set_match(chip, &data, 10, 0x27, 0xff);
	mv88e6xxx_g3_tcam_set_match(chip, &data, 11, 0xa9, 0xff);

	/* Ether Type 0x0806 - ARP */
	mv88e6xxx_g3_tcam_set_match(chip, &data, 16, 0x08, 0xff);
	mv88e6xxx_g3_tcam_set_match(chip, &data, 17, 0x06, 0xff);

	/* ARP Hardware Type 1  - Ethernet */
	mv88e6xxx_g3_tcam_set_match(chip, &data, 18, 0x00, 0xff);
	mv88e6xxx_g3_tcam_set_match(chip, &data, 19, 0x01, 0xff);

	/* ARP protocol Type 0x0800 - IP */
	mv88e6xxx_g3_tcam_set_match(chip, &data, 20, 0x08, 0xff);
	mv88e6xxx_g3_tcam_set_match(chip, &data, 21, 0x00, 0xff);

	/* Operation 2 - reply */
	mv88e6xxx_g3_tcam_set_match(chip, &data, 24, 0x00, 0xff);
	mv88e6xxx_g3_tcam_set_match(chip, &data, 25, 0x02, 0xff);

	/* Source Port 1 */
	mv88e6xxx_g3_tcam_set(chip, &data, MV88E6XXX_P0_KEY2_SRC_PORT_VECTOR,
			   (1 << 1));

	/* Destination port None, i.e. drop */
	mv88e6xxx_g3_tcam_set(chip, &data, MV88E6XXX_P2_ACTION3_DST_PORT_VECTOR,
			   0);

	mv88e6xxx_g3_tcam_load_entry(chip, 43, &data);

	return 0;
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_tcam_ops = {
	.read = mv88e6xxx_dbg_tcam_read,
	.write = mv88e6xxx_dbg_tcam_write,
};

static int mv88e6xxx_dbg_tcam_dump_read(struct mv88e6xxx_chip *chip, int id,
					   struct seq_file *seq)
{
	struct mv88e6xxx_tcam_data data;
	int entry = 0;
	int err;

	while (1) {
		err = mv88e6xxx_g3_tcam_get_next(chip, &entry, &data);
		if (err)
			return err;

		if (entry == 0xff)
			break;

		err = mv88e6xxx_dbg_tcam_read_entry(chip, seq, entry, &data);
		if (err)
			return err;
	}

	return 0;
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_tcam_dump_ops = {
	.read = mv88e6xxx_dbg_tcam_dump_read,
};

static int mv88e6xxx_dbg_vlan_table_read(struct mv88e6xxx_chip *chip, int id,
					 struct seq_file *seq)
{
	u16 val;
	int port, err;

	err = mv88e6xxx_port_read(chip, id, MV88E6XXX_PORT_BASE_VLAN, &val);
	if (err)
		return err;

	seq_printf(seq, "input port %d vlantable", id);

	for (port = 0; port < mv88e6xxx_num_ports(chip); port++)
		if (val & BIT(port))
			seq_printf(seq, " %d", port);
		else
			seq_puts(seq, " -");

	seq_puts(seq, "\n");

	return 0;
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_vlan_table_ops = {
	.read = mv88e6xxx_dbg_vlan_table_read,
};

static void mv88e6xxx_dbg_init_atu(struct mv88e6xxx_chip *chip)
{
	struct dentry *dir;
	char name[32];
	int fid;

	mv88e6xxx_dbg_create_file(chip, chip->debugfs_dir, "atu", -1,
				  &mv88e6xxx_dbg_atu_ops);

	dir = debugfs_create_dir("atu-stats", chip->debugfs_dir);

	for (fid = 0; fid < mv88e6xxx_num_databases(chip); fid++) {
		snprintf(name, sizeof(name), "%d", fid);
		mv88e6xxx_dbg_create_file(chip, dir, name, fid,
					  &mv88e6xxx_dbg_atu_stats_ops);
	}
}


static void mv88e6xxx_dbg_port_symlink(struct mv88e6xxx_chip *chip,
				       struct dentry *dir, int port)
{
	struct dsa_port *dp = dsa_to_port(chip->ds, port);
	struct net_device *netdev;
	char name[54];

	if (dp->type == DSA_PORT_TYPE_CPU)
		netdev = dp->master;
	else
		netdev = dp->slave;

	if (!netdev)
		return;

	snprintf(name, sizeof(name), "/sys/class/net/%s", netdev_name(netdev));

	debugfs_create_symlink("net", dir, name);
}

static void mv88e6xxx_dbg_init_port(struct mv88e6xxx_chip *chip, int port)
{
	struct dentry *dir;
	char name[32];

	snprintf(name, sizeof(name), "p%d", port);

	dir = debugfs_create_dir(name, chip->debugfs_dir);

	mv88e6xxx_dbg_port_symlink(chip, dir, port);

	mv88e6xxx_dbg_create_file(chip, dir, "8021q_mode", port,
				  &mv88e6xxx_dbg_8021q_mode_ops);

	mv88e6xxx_dbg_create_file(chip, dir, "default_vid", port,
				  &mv88e6xxx_dbg_default_vid_ops);

	mv88e6xxx_dbg_create_file(chip, dir, "fid", port,
				  &mv88e6xxx_dbg_fid_ops);

	mv88e6xxx_dbg_create_file(chip, dir, "regs", port,
				  &mv88e6xxx_dbg_regs_ops);

	mv88e6xxx_dbg_create_file(chip, dir, "state", port,
				  &mv88e6xxx_dbg_state_ops);

	mv88e6xxx_dbg_create_file(chip, dir, "stats", port,
				  &mv88e6xxx_dbg_stats_ops);

	mv88e6xxx_dbg_create_file(chip, dir, "vlan_table", port,
				  &mv88e6xxx_dbg_vlan_table_ops);
}

static void mv88e6xxx_dbg_init_tcam(struct mv88e6xxx_chip *chip)
{
	struct dentry *dir;
	char name[32];
	int entry;

	if (!chip->info->global3_addr)
		return;

	dir = debugfs_create_dir("tcam", chip->debugfs_dir);

	for (entry = 0; entry < 255; ++entry) {
		snprintf(name, sizeof(name), "%d", entry);

		mv88e6xxx_dbg_create_file(chip, dir, name, entry,
					  &mv88e6xxx_dbg_tcam_ops);
	}

	mv88e6xxx_dbg_create_file(chip, dir, "dump", -1,
				  &mv88e6xxx_dbg_tcam_dump_ops);
}

static void mv88e6xxx_dbg_vtu_puts(struct mv88e6xxx_chip *chip, 
				   struct seq_file *seq,
				   const struct mv88e6xxx_vtu_entry *entry)
{
	int port;

	seq_printf(seq, "vid %d", entry->vid);
	seq_printf(seq, "\tfid %d", entry->fid);
	seq_printf(seq, "\tsid %d", entry->sid);

	seq_puts(seq, "\tdpv");

	for (port = 0; port < mv88e6xxx_num_ports(chip); port++) {
		switch (entry->member[port]) {
		case MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_UNMODIFIED:
			seq_printf(seq, " %d unmodified", port);
			break;
		case MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_UNTAGGED:
			seq_printf(seq, " %d untagged", port);
			break;
		case MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_TAGGED:
			seq_printf(seq, " %d tagged", port);
			break;
		case MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_NON_MEMBER:
		default:
			break;
		}
	}

	seq_puts(seq, "\n");
}

int mv88e6xxx_vtu_getnext(struct mv88e6xxx_chip *chip,
			  struct mv88e6xxx_vtu_entry *entry);

static int mv88e6xxx_dbg_vtu_read(struct mv88e6xxx_chip *chip, int id,
				  struct seq_file *seq)
{
	struct mv88e6xxx_vtu_entry next = {
		.vid = chip->info->max_vid,
	};
	int err;

	do {
		err = mv88e6xxx_vtu_getnext(chip, &next);
		if (err)
			return err;

		if (!next.valid)
			break;

		mv88e6xxx_dbg_vtu_puts(chip, seq, &next);
	} while (next.vid < chip->info->max_vid);

	return 0;
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_vtu_ops = {
	.read = mv88e6xxx_dbg_vtu_read,
};

void mv88e6xxx_dbg_create(struct mv88e6xxx_chip *chip)
{
	char name[32];
	int port;

	snprintf(name, sizeof(name), "sw%d", chip->ds->index);

	chip->debugfs_dir = debugfs_create_dir(name, mv88e6xxx_dbg_dir);

	mv88e6xxx_dbg_create_file(chip, chip->debugfs_dir, "age_time", -1,
				  &mv88e6xxx_dbg_age_time_ops);

	mv88e6xxx_dbg_init_atu(chip);

	mv88e6xxx_dbg_create_file(chip, chip->debugfs_dir, "device_map", -1,
				  &mv88e6xxx_dbg_device_map_ops);

	mv88e6xxx_dbg_create_file(chip, chip->debugfs_dir, "global1",
				  MV88E6XXX_DBG_REGS_ID_GLOBAL1,
				  &mv88e6xxx_dbg_regs_ops);

	mv88e6xxx_dbg_create_file(chip, chip->debugfs_dir, "global2",
				  MV88E6XXX_DBG_REGS_ID_GLOBAL2,
				  &mv88e6xxx_dbg_regs_ops);

	mv88e6xxx_dbg_create_file(chip, chip->debugfs_dir, "name", -1,
				  &mv88e6xxx_dbg_name_ops);

	for (port = 0; port < mv88e6xxx_num_ports(chip); port++)
		mv88e6xxx_dbg_init_port(chip, port);

	if (mv88e6xxx_has_pvt(chip))
		mv88e6xxx_dbg_create_file(chip, chip->debugfs_dir, "pvt", -1,
					  &mv88e6xxx_dbg_pvt_ops);

	mv88e6xxx_dbg_create_file(chip, chip->debugfs_dir, "scratch", -1,
				  &mv88e6xxx_dbg_scratch_ops);

	if (chip->info->ops->serdes_power)
		mv88e6xxx_dbg_create_file(chip, chip->debugfs_dir, "serdes",
					  MV88E6XXX_DBG_REGS_ID_SERDES,
					  &mv88e6xxx_dbg_regs_ops);

	mv88e6xxx_dbg_init_tcam(chip);

	if (chip->info->max_vid)
		mv88e6xxx_dbg_create_file(chip, chip->debugfs_dir, "vtu", -1,
					  &mv88e6xxx_dbg_vtu_ops);
}

void mv88e6xxx_dbg_destroy(struct mv88e6xxx_chip *chip)
{
	debugfs_remove_recursive(chip->debugfs_dir);
}

static int __init mv88e6xxx_dbg_init(void)
{
	mv88e6xxx_dbg_dir = debugfs_create_dir("mv88e6xxx", NULL);

	return 0;
}
module_init(mv88e6xxx_dbg_init);

static void __exit mv88e6xxx_dbg_cleanup(void)
{
	debugfs_remove_recursive(mv88e6xxx_dbg_dir);
}
module_exit(mv88e6xxx_dbg_cleanup);
