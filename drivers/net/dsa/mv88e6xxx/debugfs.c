#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

/* mv88e6xxx module debugfs directory */
static struct dentry *mv88e6xxx_dbg_dir;

struct mv88e6xxx_dbg_ops {
	int (*read)(struct mv88e6xxx_chip *chip, int id, struct seq_file *seq);
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

	if (!priv->ops->read) {
		pr_err("file mode bypassed???\n");
		return -EOPNOTSUPP;
	}

	mutex_lock(&chip->reg_lock);
	err = priv->ops->read(chip, priv->id, seq);
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int mv88e6xxx_dbg_open(struct inode *inode, struct file *file)
{
	return single_open(file, mv88e6xxx_dbg_show, inode->i_private);
}

static const struct file_operations mv88e6xxx_dbg_fops = {
	.open = mv88e6xxx_dbg_open,
	.read = seq_read,
	.llseek = no_llseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int mv88e6xxx_dbg_create_file(struct mv88e6xxx_chip *chip,
				     struct dentry *dir, char *name, int id,
				     const struct mv88e6xxx_dbg_ops *ops)
{
	struct mv88e6xxx_dbg_priv *priv;
	struct dentry *entry;
	umode_t mode;

	priv = devm_kzalloc(chip->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->chip = chip;
	priv->ops = ops;
	priv->id = id;

	mode = 0;
	if (ops->read)
		mode |= S_IRUGO;

	entry = debugfs_create_file(name, mode, dir, priv, &mv88e6xxx_dbg_fops);
	if (!entry)
		return -EFAULT;

	if (IS_ERR(entry))
		return PTR_ERR(entry);

	return 0;
}

static const char * const mv88e6xxx_atu_unicast_state_names[] = {
	[0x0] = "UNUSED", /* MV88E6XXX_G1_ATU_DATA_STATE_UNUSED */
	[0x1] = "Age 1 (oldest)",
	[0x2] = "Age 2",
	[0x3] = "Age 3",
	[0x4] = "Age 4",
	[0x5] = "Age 5",
	[0x6] = "Age 6",
	[0x7] = "Age 7 (newest)",
	[0x8] = "UC_STATIC_POLICY",
	[0x9] = "UC_STATIC_POLICY_PO",
	[0xa] = "UC_STATIC_NRL",
	[0xb] = "UC_STATIC_NRL_PO",
	[0xc] = "UC_STATIC_MGMT",
	[0xd] = "UC_STATIC_MGMT_PO", /* MV88E6XXX_G1_ATU_DATA_STATE_UC_MGMT */
	[0xe] = "UC_STATIC", /* MV88E6XXX_G1_ATU_DATA_STATE_UC_STATIC */
	[0xf] = "UC_STATIC_PO", /* MV88E6XXX_G1_ATU_DATA_STATE_UC_PRIO_OVER */
};

static const char * const mv88e6xxx_atu_multicast_state_names[] = {
	[0x0] = "UNUSED", /* MV88E6XXX_G1_ATU_DATA_STATE_UNUSED */
	[0x1] = "RESERVED",
	[0x2] = "RESERVED",
	[0x3] = "RESERVED",
	[0x4] = "MC_STATIC_POLICY",
	[0x5] = "MC_STATIC_NRL", /* MV88E6XXX_G1_ATU_DATA_STATE_MC_NONE_RATE */
	[0x6] = "MC_STATIC_MGMT",
	[0x7] = "MC_STATIC", /* MV88E6XXX_G1_ATU_DATA_STATE_MC_STATIC */
	[0x8] = "RESERVED",
	[0x9] = "RESERVED",
	[0xa] = "RESERVED",
	[0xb] = "RESERVED",
	[0xc] = "MC_STATIC_POLICY_PO",
	[0xd] = "MC_STATIC_NRL_PO",
	[0xe] = "MC_STATIC_MGMT_PO", /* MV88E6XXX_G1_ATU_DATA_STATE_MC_MGMT */
	[0xf] = "MC_STATIC_PO", /* MV88E6XXX_G1_ATU_DATA_STATE_MC_PRIO_OVER */
};

static void mv88e6xxx_dbg_atu_read_ent(struct mv88e6xxx_chip *chip,
				       struct seq_file *seq, u16 fid,
				       const struct mv88e6xxx_atu_entry *entry)
{
	int i;

	/* FID */
	seq_printf(seq, "%4d", fid);

	/* MAC address */
	seq_printf(seq, "  %.2x", entry->mac[0]);
	for (i = 1; i < ETH_ALEN; ++i)
		seq_printf(seq, ":%.2x", entry->mac[i]);

	/* State */
	seq_printf(seq, "  %19s", is_multicast_ether_addr(entry->mac) ?
		   mv88e6xxx_atu_multicast_state_names[entry->state] :
		   mv88e6xxx_atu_unicast_state_names[entry->state]);

	/* Trunk ID or Port vector */
	if (entry->trunk) {
		seq_printf(seq, "       y  %d", entry->portvec);
	} else {
		seq_puts(seq, "       n ");
		for (i = 0; i < mv88e6xxx_num_ports(chip); ++i)
			if (entry->portvec & BIT(i))
				seq_printf(seq, " %d", i);
			else
				seq_puts(seq, " -");
	}

	seq_puts(seq, "\n");
}

static int mv88e6xxx_dbg_atu_read(struct mv88e6xxx_chip *chip, int id,
				  struct seq_file *seq)
{
	struct mv88e6xxx_atu_entry next;
	int err;

	seq_puts(seq, " FID  MAC Addr                  State         Trunk?  DPV/Trunk ID\n");

	next.state = MV88E6XXX_G1_ATU_DATA_STATE_UNUSED;
	eth_broadcast_addr(next.mac);

	do {
		err = mv88e6xxx_g1_atu_getnext(chip, id, &next);
		if (err)
			return err;

		if (next.state == MV88E6XXX_G1_ATU_DATA_STATE_UNUSED)
			break;

		mv88e6xxx_dbg_atu_read_ent(chip, seq, id, &next);
	} while (!is_broadcast_ether_addr(next.mac));

	return 0;
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_atu_ops = {
		.read = mv88e6xxx_dbg_atu_read,
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

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_tcam_ops = {
	.read = mv88e6xxx_dbg_tcam_read,
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

static void mv88e6xxx_dbg_vtu_read_ent(struct mv88e6xxx_chip *chip,
				       struct seq_file *seq,
				       const struct mv88e6xxx_vtu_entry *entry)
{
	int i;

	seq_puts(seq, " VID  FID  SID");
	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i)
		seq_printf(seq, " %2d", i);
	seq_puts(seq, "\n");

	seq_printf(seq, "%4d %4d   %2d", entry->vid, entry->fid, entry->sid);

	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i) {
		switch (entry->member[i]) {
		case MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_UNMODIFIED:
			seq_puts(seq, "  =");
			break;
		case MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_UNTAGGED:
			seq_puts(seq, "  u");
			break;
		case MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_TAGGED:
			seq_puts(seq, "  t");
			break;
		case MV88E6XXX_G1_VTU_DATA_MEMBER_TAG_NON_MEMBER:
			seq_puts(seq, "  x");
			break;
		default:
			seq_puts(seq, " ??");
			break;
		}
	}

	seq_puts(seq, "\n");
}


static int mv88e6xxx_dbg_vtu_dump_read(struct mv88e6xxx_chip *chip, int id,
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

		mv88e6xxx_dbg_vtu_read_ent(chip, seq, &next);
	} while (next.vid < chip->info->max_vid);

	return 0;
}

static const struct mv88e6xxx_dbg_ops mv88e6xxx_dbg_vtu_dump_ops = {
	.read = mv88e6xxx_dbg_vtu_dump_read,
};

static int mv88e6xxx_dbg_init_atu(struct mv88e6xxx_chip *chip)
{
	struct dentry *dir;
	char name[32];
	int fid;
	int err;

	dir = debugfs_create_dir("atu", chip->debugfs_dir);
	if (!dir)
		return -EFAULT;

	if (IS_ERR(dir))
		return PTR_ERR(dir);

	for (fid = 0; fid < mv88e6xxx_num_databases(chip); ++fid) {
		snprintf(name, sizeof(name), "%d", fid);

		err = mv88e6xxx_dbg_create_file(chip, dir, name, fid,
						&mv88e6xxx_dbg_atu_ops);
		if (err)
			break;
	}

	return err;
}

static int mv88e6xxx_dbg_init_tcam(struct mv88e6xxx_chip *chip)
{
	struct dentry *dir;
	char name[32];
	int entry;
	int err;

	dir = debugfs_create_dir("tcam", chip->debugfs_dir);
	if (!dir)
		return -EFAULT;

	if (IS_ERR(dir))
		return PTR_ERR(dir);

	for (entry = 0; entry < 255; ++entry) {
		snprintf(name, sizeof(name), "%d", entry);

		err = mv88e6xxx_dbg_create_file(chip, dir, name, entry,
						&mv88e6xxx_dbg_tcam_ops);
		if (err)
			return err;
	}

	return mv88e6xxx_dbg_create_file(chip, dir, "dump", -1,
					 &mv88e6xxx_dbg_tcam_dump_ops);
}

static int mv88e6xxx_dbg_init_vtu(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_dbg_create_file(chip, chip->debugfs_dir, "vtu", -1,
					 &mv88e6xxx_dbg_vtu_dump_ops);
}

static void mv88e6xxx_dbg_destroy_chip(struct mv88e6xxx_chip *chip)
{
	/* handles NULL */
	debugfs_remove_recursive(chip->debugfs_dir);
}

static int mv88e6xxx_dbg_init_chip(struct mv88e6xxx_chip *chip)
{
	struct dentry *dir;
	char name[32];
	int err;

	/* skip if there is no debugfs support */
	if (!mv88e6xxx_dbg_dir)
		return 0;

	snprintf(name, sizeof(name), "sw%d", chip->ds->index);

	dir = debugfs_create_dir(name, mv88e6xxx_dbg_dir);
	if (!dir)
		return -EFAULT;

	if (IS_ERR(dir))
		return PTR_ERR(dir);

	chip->debugfs_dir = dir;

	err = mv88e6xxx_dbg_init_atu(chip);
	if (err)
		goto destroy;

	if (chip->info->global3_addr) {
		err = mv88e6xxx_dbg_init_tcam(chip);
		if (err)
			goto destroy;
	}

	if (chip->info->max_vid) {
		err = mv88e6xxx_dbg_init_vtu(chip);
		if (err)
			goto destroy;
	}

	return 0;
destroy:
	mv88e6xxx_dbg_destroy_chip(chip);

	return err;
}

static void mv88e6xxx_dbg_destroy_module(void)
{
	debugfs_remove_recursive(mv88e6xxx_dbg_dir);
}

static int mv88e6xxx_dbg_init_module(void)
{
	struct dentry *dir;
	int err;

	dir = debugfs_create_dir("mv88e6xxx", NULL);
	if (!dir)
		return -EFAULT;

	if (IS_ERR(dir)) {
		err = PTR_ERR(dir);

		/* kernel built without debugfs support */
		if (err == -ENODEV)
			return 0;

		return err;
	}

	mv88e6xxx_dbg_dir = dir;

	return 0;
}
