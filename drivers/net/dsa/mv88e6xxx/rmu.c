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

#include <linux/if_ether.h>

#include "chip.h"
#include "rmu.h"

#define MV88E6XXX_RMU_TIMEOUT (msecs_to_jiffies(1000))

static int mv88e6xxx_rmu_wait_response(struct mv88e6xxx_chip *chip)
{
	long timeout;

	timeout = wait_for_completion_interruptible_timeout(&chip->rmu_response_received, MV88E6XXX_RMU_TIMEOUT);
	if (timeout < 0)
		return timeout;
	if (timeout == 0)
		return -ETIMEDOUT;

	dev_dbg(chip->dev, "got RMU response for request %d in %d msecs\n",
		chip->rmu_sequence_num, jiffies_to_msecs(MV88E6XXX_RMU_TIMEOUT - timeout));

	return 0;
}

#define DSA_LEN		4

struct edsahdr {
	unsigned char	eth_dest_addr[ETH_ALEN];
	unsigned char	eth_src_addr[ETH_ALEN];
	__be16		edsa_ethertype;
	__be16		edsa_reserved; /* 0x0000 */
	unsigned char	dsa_tag[DSA_LEN];
	__be16		eth_ethertype;
} __attribute__((packed));

struct dsahdr {
	unsigned char	eth_dest_addr[ETH_ALEN];
	unsigned char	eth_src_addr[ETH_ALEN];
	unsigned char	dsa_tag[DSA_LEN];
	__be16		eth_ethertype;
} __attribute__((packed));

struct mv88e6xxx_rmu_request {
	__be16	format;
	__be16	pad; /* 0x0000 on request, Prod Num/Rev on response */
	__be16	code;
} __attribute__((packed));

static int mv88e6xxx_rmu_request(struct mv88e6xxx_chip *chip, u16 code, u8 *data, size_t len)
{
	const unsigned char dest_addr[ETH_ALEN] = { 0x01, 0x50, 0x43, 0x00, 0x00, 0x00 };
	struct net_device *dev = chip->rmu_dev;
	struct mv88e6xxx_rmu_request *req;
	unsigned char *eth_dest_addr;
	unsigned char *eth_src_addr;
	unsigned char *dsa_tag;
	__be16 *eth_ethertype;
	struct edsahdr *edsa;
	struct sk_buff *skb;
	struct dsahdr *dsa;

	if (!dev)
		return -EOPNOTSUPP;

	switch (dev->dsa_ptr->tag_ops->proto) {
	case DSA_TAG_PROTO_DSA:
		skb = alloc_skb(sizeof(*dsa) + sizeof(*req) + len, GFP_KERNEL);
		if (!skb)
			return -ENOMEM;

		dsa = skb_put(skb, sizeof(*dsa));
		eth_dest_addr = dsa->eth_dest_addr;
		eth_src_addr = dsa->eth_src_addr;
		dsa_tag = dsa->dsa_tag;
		eth_ethertype = &dsa->eth_ethertype;
		break;
	case DSA_TAG_PROTO_EDSA:
		skb = alloc_skb(sizeof(*edsa) + sizeof(*req) + len, GFP_KERNEL);
		if (!skb)
			return -ENOMEM;

		edsa = skb_put(skb, sizeof(*edsa));
		eth_dest_addr = edsa->eth_dest_addr;
		eth_src_addr = edsa->eth_src_addr;
		edsa->edsa_ethertype = htons(ETH_P_EDSA);
		edsa->edsa_reserved = 0x0000;
		dsa_tag = edsa->dsa_tag;
		eth_ethertype = &edsa->eth_ethertype;
		break;
	default:
		return -EINVAL;
	}

	ether_addr_copy(eth_dest_addr, dest_addr); /* Marvell broadcast or switch MAC */
	ether_addr_copy(eth_src_addr, dev->dev_addr);
	dsa_tag[0] = 0x40 | (chip->ds->index & 0x1f); /* From_CPU */
	dsa_tag[1] = 0xfa;
	dsa_tag[2] = 0xf;
	dsa_tag[3] = ++chip->rmu_sequence_num;
	*eth_ethertype = htons(ETH_P_EDSA); /* User defined, useless really */

	req = skb_put(skb, sizeof(*req));
	req->format = htons(MV88E6XXX_RMU_REQUEST_FORMAT_SOHO);
	req->pad = 0x0000;
	req->code = htons(code);

	skb_put_data(skb, data, len);

	skb->dev = dev;

	dsa_switch_xmit(chip->ds, skb);

	return mv88e6xxx_rmu_wait_response(chip);
}

int mv88e6xxx_rmu_response(struct mv88e6xxx_chip *chip, struct sk_buff *skb)
{
	struct net_device *dev = chip->rmu_dev;
	struct mv88e6xxx_rmu_request *req;
	unsigned char *dsa_tag;
	size_t data_offset; 
	size_t req_offset;

	/* Check if RMU is enabled */
	if (dev != skb->dev)
		return -EOPNOTSUPP;

	if (chip->rmu_response)
		return -EBUSY;

	switch (dev->dsa_ptr->tag_ops->proto) {
	case DSA_TAG_PROTO_DSA:
		dsa_tag = skb->data - 2;
		req_offset = DSA_LEN + ETH_TLEN - 2;
		break;
	case DSA_TAG_PROTO_EDSA:
		/* skb->data points to the end of the (EDSA) ethertype */
		dsa_tag = skb->data + 2;
		req_offset = 2 + DSA_LEN + ETH_TLEN;
		break;
	default:
		return -EINVAL;
	}

	if ((dsa_tag[0] != chip->ds->index) ||
            (dsa_tag[1] != 0x00) ||
            ((dsa_tag[2] & 0x1f) != 0x1f) ||
            (dsa_tag[3] != chip->rmu_sequence_num))
		return -EINVAL;

	data_offset = req_offset + sizeof(*req);
	if (skb->len < data_offset)
		return -EINVAL;

	req = (struct mv88e6xxx_rmu_request *)(skb->data + req_offset);
	if (ntohs(req->code) == 0xffff)
		return -EINVAL;

	chip->rmu_response_data_len = skb->len - data_offset;
	if (chip->rmu_response_data_len > 0)
		chip->rmu_response_data = skb->data + data_offset;
	else
		chip->rmu_response_data = NULL;

	chip->rmu_response = skb_clone(skb, GFP_KERNEL);
	if (!chip->rmu_response)
		return -ENOMEM;

	complete(&chip->rmu_response_received);

	return 0;
}

static int mv88e6xxx_rmu_reg_read(struct mv88e6xxx_chip *chip,
				  int dev, int reg, u16 *data)
{
	unsigned char request_data[8];
	int err;

	request_data[0] = 0x08 | ((dev >> 3) & 0x03);
	request_data[1] = ((dev << 5) & 0xe0) | (reg & 0x1f);
	request_data[2] = 0x00;
	request_data[3] = 0x00;

	/* End Of List Command */
	memset(&request_data[4], 0xff, 4);

	err = mv88e6xxx_rmu_request(chip, MV88E6XXX_RMU_REQUEST_CODE_READ_WRITE, request_data, sizeof(request_data));
	if (err)
		return err;

	if (chip->rmu_response_data_len < sizeof(request_data))
		err = -EINVAL;
	else
		*data = (chip->rmu_response_data[2] << 8) | chip->rmu_response_data[3];

	kfree_skb(chip->rmu_response);
	chip->rmu_response = NULL;

	return err;
}

static int mv88e6xxx_rmu_reg_write(struct mv88e6xxx_chip *chip,
				   int dev, int reg, u16 data)
{
	unsigned char request_data[8];
	int err;

	request_data[0] = 0x04 | ((dev >> 3) & 0x03);
	request_data[1] = ((dev << 5) & 0xe0) | (reg & 0x1f);
	request_data[2] = data >> 8;
	request_data[3] = data & 0xff;

	/* End Of List Command */
	memset(&request_data[4], 0xff, 4);

	err = mv88e6xxx_rmu_request(chip, MV88E6XXX_RMU_REQUEST_CODE_READ_WRITE, request_data, sizeof(request_data));
	if (err)
		return err;

	if (chip->rmu_response_data_len < sizeof(request_data))
		err = -EINVAL;

	kfree_skb(chip->rmu_response);
	chip->rmu_response = NULL;

	return err;
}

static int mv88e6xxx_rmu_reg_wait_bit(struct mv88e6xxx_chip *chip,
				      int dev, int reg, int bit, int val)
{
	unsigned char request_data[8];
	int err;

	request_data[0] = 0x10 | (val ? 0x0c : 0x00) | ((dev >> 3) & 0x03);
	request_data[1] = ((dev << 5) & 0xe0) | (reg & 0x1f);
	request_data[2] = bit & 0x0f;
	request_data[3] = 0x00;

	/* End Of List Command */
	request_data[4] = 0xff;
	request_data[5] = 0xff;
	request_data[6] = 0xff;
	request_data[7] = 0xff;

	err = mv88e6xxx_rmu_request(chip, MV88E6XXX_RMU_REQUEST_CODE_READ_WRITE, request_data, sizeof(request_data));
	if (err)
		return err;

	if (chip->rmu_response_data_len < sizeof(request_data))
		err = -EINVAL;

	kfree_skb(chip->rmu_response);
	chip->rmu_response = NULL;

	return err;
}

static const struct mv88e6xxx_bus_ops mv88e6xxx_rmu_ops = {
	.read = mv88e6xxx_rmu_reg_read,
	.write = mv88e6xxx_rmu_reg_write,
	.wait_bit = mv88e6xxx_rmu_reg_wait_bit,
};

static int mv88e6xxx_rmu_setup_bus(struct mv88e6xxx_chip *chip,
				      struct net_device *dev)
{
	chip->rmu_ops = &mv88e6xxx_rmu_ops;
	chip->rmu_dev = dev;

	init_completion(&chip->rmu_response_received);

	dev_info(chip->dev, "RMU reachable via %s\n", netdev_name(dev));

	if (!chip->ops)
		chip->ops = chip->rmu_ops;

	return 0;
}

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
	struct net_device *dev;
	int port;
	int err;

	/* Find a local port (in)directly connected to the CPU to enable RMU on */
	for (port = 0; port < mv88e6xxx_num_ports(chip); port++) {
		if (dsa_is_upstream_port(ds, port)) {
			err = mv88e6xxx_rmu_setup_port(chip, port);
			if (err)
				continue;

			/* When the control CPU is local, use the master interface */
			dev = dsa_to_master(ds, port);
			if (!dev)
				return -ENODEV;

			err = mv88e6xxx_rmu_setup_bus(chip, dev);
			if (err)
				return err;

			break;
		}
	}

	return 0;
}
