/*
 * Technologic Systems TS-5500 Single Board Computer platform driver
 *
 * Copyright (c) 2012 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * This driver registers the Technologic Systems TS-5500 Single Board Computer
 * (SBC) and its devices, and adds sysfs entries to display information about
 * it, such as jumpers state or available features. For further information
 * about sysfs entries, see Documentation/ABI/testing/sysfs-platform-ts5500.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_data/gpio-ts5500.h>
#include <linux/platform_data/max197.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* Product code register */
#define TS5500_PRODUCT_CODE_ADDR	0x74
#define TS5500_PRODUCT_CODE		0x60	/* TS-5500 product code */

/* SRAM/RS-485/ADC options, and RS-485 RTS/Automatic RS-485 flags register */
#define TS5500_SRAM_RS485_ADC_ADDR	0x75
#define TS5500_SRAM			0x01	/* SRAM option */
#define TS5500_RS485			0x02	/* RS-485 option */
#define TS5500_ADC			0x04	/* A/D converter option */
#define TS5500_RS485_RTS		0x40	/* RTS for RS-485 */
#define TS5500_RS485_AUTO		0x80	/* Automatic RS-485 */

/* External Reset/Industrial Temperature Range options register */
#define TS5500_ERESET_ITR_ADDR		0x76
#define TS5500_ERESET			0x01	/* External Reset option */
#define TS5500_ITR			0x02	/* Indust. Temp. Range option */

/* LED/Jumpers register */
#define TS5500_LED_JP_ADDR		0x77
#define TS5500_LED			0x01	/* LED flag */
#define TS5500_JP1			0x02	/* Automatic CMOS */
#define TS5500_JP2			0x04	/* Enable Serial Console */
#define TS5500_JP3			0x08	/* Write Enable Drive A */
#define TS5500_JP4			0x10	/* Fast Console (115K baud) */
#define TS5500_JP5			0x20	/* User Jumper */
#define TS5500_JP6			0x40	/* Console on COM1 (req. JP2) */
#define TS5500_JP7			0x80	/* Undocumented (Unused) */

/* A/D Converter registers */
#define TS5500_ADC_CONV_BUSY_ADDR	0x195	/* Conversion state register */
#define TS5500_ADC_CONV_BUSY		0x01
#define TS5500_ADC_CONV_INIT_LSB_ADDR	0x196	/* Start conv. / LSB register */
#define TS5500_ADC_CONV_MSB_ADDR	0x197	/* MSB register */
#define TS5500_ADC_CONV_DELAY		12	/* usec */

/**
 * struct ts5500_sbc - TS-5500 SBC main structure
 * @id:		Board product ID.
 * @sram:	Check SRAM option.
 * @rs485:	Check RS-485 option.
 * @adc:	Check Analog/Digital converter option.
 * @ereset:	Check External Reset option.
 * @itr:	Check Industrial Temperature Range option.
 * @jumpers:	States of jumpers 1-7.
 */
struct ts5500_sbc {
	int	id;
	bool	sram;
	bool	rs485;
	bool	adc;
	bool	ereset;
	bool	itr;
	u8	jumpers;
};

/* Board signatures in BIOS shadow RAM */
static const struct {
	const unsigned char *string;
	const ssize_t offset;
} ts5500_signatures[] __initdata = {
	{ "TS-5x00 AMD Elan", 0xb14 },
};

static int __init ts5500_check_signature(void)
{
	void __iomem *bios;
	int i, ret = -ENODEV;

	bios = ioremap(0xf0000, 0x10000);
	if (!bios)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(ts5500_signatures); i++) {
		if (check_signature(bios + ts5500_signatures[i].offset,
				    ts5500_signatures[i].string,
				    strlen(ts5500_signatures[i].string))) {
			ret = 0;
			break;
		}
	}

	iounmap(bios);
	return ret;
}

static int __init ts5500_detect_config(struct ts5500_sbc *sbc)
{
	u8 tmp;
	int ret = 0;

	if (!request_region(TS5500_PRODUCT_CODE_ADDR, 4, "ts5500"))
		return -EBUSY;

	tmp = inb(TS5500_PRODUCT_CODE_ADDR);
	if (tmp != TS5500_PRODUCT_CODE) {
		pr_err("This platform is not a TS-5500 (found ID 0x%x)\n", tmp);
		ret = -ENODEV;
		goto cleanup;
	}
	sbc->id = tmp;

	tmp = inb(TS5500_SRAM_RS485_ADC_ADDR);
	sbc->sram = tmp & TS5500_SRAM;
	sbc->rs485 = tmp & TS5500_RS485;
	sbc->adc = tmp & TS5500_ADC;

	tmp = inb(TS5500_ERESET_ITR_ADDR);
	sbc->ereset = tmp & TS5500_ERESET;
	sbc->itr = tmp & TS5500_ITR;

	tmp = inb(TS5500_LED_JP_ADDR);
	sbc->jumpers = tmp & ~TS5500_LED;

cleanup:
	release_region(TS5500_PRODUCT_CODE_ADDR, 4);
	return ret;
}

static ssize_t ts5500_show_id(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ts5500_sbc *sbc = dev_get_drvdata(dev);

	return sprintf(buf, "0x%x\n", sbc->id);
}

static ssize_t ts5500_show_jumpers(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct ts5500_sbc *sbc = dev_get_drvdata(dev);

	return sprintf(buf, "0x%2x\n", sbc->jumpers >> 1);
}

#define TS5500_SHOW(att)						\
	static ssize_t ts5500_show_##att(struct device *dev,		\
					 struct device_attribute *attr,	\
					 char *buf)			\
	{								\
		struct ts5500_sbc *sbc = dev_get_drvdata(dev);		\
		return sprintf(buf, "%d\n", sbc->att);			\
	}

TS5500_SHOW(sram)
TS5500_SHOW(rs485)
TS5500_SHOW(adc)
TS5500_SHOW(ereset)
TS5500_SHOW(itr)

static DEVICE_ATTR(id, S_IRUGO, ts5500_show_id, NULL);
static DEVICE_ATTR(jumpers, S_IRUGO, ts5500_show_jumpers, NULL);
static DEVICE_ATTR(sram, S_IRUGO, ts5500_show_sram, NULL);
static DEVICE_ATTR(rs485, S_IRUGO, ts5500_show_rs485, NULL);
static DEVICE_ATTR(adc, S_IRUGO, ts5500_show_adc, NULL);
static DEVICE_ATTR(ereset, S_IRUGO, ts5500_show_ereset, NULL);
static DEVICE_ATTR(itr, S_IRUGO, ts5500_show_itr, NULL);

static struct attribute *ts5500_attributes[] = {
	&dev_attr_id.attr,
	&dev_attr_jumpers.attr,
	&dev_attr_sram.attr,
	&dev_attr_rs485.attr,
	&dev_attr_adc.attr,
	&dev_attr_ereset.attr,
	&dev_attr_itr.attr,
	NULL
};

static const struct attribute_group ts5500_attr_group = {
	.attrs = ts5500_attributes,
};

static struct ts5500_gpio_platform_data ts5500_gpio_pdata = {
	.base = -1,
};

static struct platform_device ts5500_gpio_pdev = {
	.name = "gpio-ts5500",
	.id = -1,
	.dev = {
		.platform_data = &ts5500_gpio_pdata,
	},
};

static void ts5500_led_set(struct led_classdev *led_cdev,
			   enum led_brightness brightness)
{
	outb(!!brightness, TS5500_LED_JP_ADDR);
}

static enum led_brightness ts5500_led_get(struct led_classdev *led_cdev)
{
	return (inb(TS5500_LED_JP_ADDR) & TS5500_LED) ? LED_FULL : LED_OFF;
}

static struct led_classdev ts5500_led_cdev = {
	.name = "ts5500:green:activity",
	.brightness_set = ts5500_led_set,
	.brightness_get = ts5500_led_get,
};

static int ts5500_adc_convert(u8 ctrl)
{
	u8 lsb, msb;

	/* Start conversion (ensure the 3 MSB are set to 0) */
	outb(ctrl & 0x1F, TS5500_ADC_CONV_INIT_LSB_ADDR);

	/*
	 * The platform has CPLD logic driving the A/D converter.
	 * The conversion must complete within 11 microseconds,
	 * otherwise we have to re-initiate a conversion.
	 */
	udelay(TS5500_ADC_CONV_DELAY);
	if (inb(TS5500_ADC_CONV_BUSY_ADDR) & TS5500_ADC_CONV_BUSY)
		return -EBUSY;

	/* Read the raw data */
	lsb = inb(TS5500_ADC_CONV_INIT_LSB_ADDR);
	msb = inb(TS5500_ADC_CONV_MSB_ADDR);

	return (msb << 8) | lsb;
}

static struct max197_platform_data ts5500_adc_pdata = {
	.convert = ts5500_adc_convert,
};

static struct platform_device ts5500_adc_pdev = {
	.name = "max197",
	.id = -1,
	.dev = {
		.platform_data = &ts5500_adc_pdata,
	},
};

static int __init ts5500_init(void)
{
	struct platform_device *pdev;
	struct ts5500_sbc *sbc;
	int ret;

	/*
	 * There is no DMI available, or PCI bridge subvendor info,
	 * only the BIOS provides a 16-bit identification call.
	 * It is safer to find a signature in the BIOS shadow RAM.
	 */
	ret = ts5500_check_signature();
	if (ret)
		return ret;

	pdev = platform_device_register_simple("ts5500", -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	sbc = devm_kzalloc(&pdev->dev, sizeof(struct ts5500_sbc), GFP_KERNEL);
	if (!sbc) {
		ret = -ENOMEM;
		goto release;
	}

	ret = ts5500_detect_config(sbc);
	if (ret)
		goto release;

	ret = sysfs_create_group(&pdev->dev.kobj, &ts5500_attr_group);
	if (ret)
		goto release;

	platform_set_drvdata(pdev, sbc);

	ts5500_gpio_pdev.dev.parent = &pdev->dev;
	if (platform_device_register(&ts5500_gpio_pdev))
		dev_warn(&pdev->dev, "DIO headers registration failed\n");
	if (led_classdev_register(&pdev->dev, &ts5500_led_cdev))
		dev_warn(&pdev->dev, "LED registration failed\n");
	if (sbc->adc) {
		ts5500_adc_pdev.dev.parent = &pdev->dev;
		if (platform_device_register(&ts5500_adc_pdev))
			dev_warn(&pdev->dev, "ADC registration failed\n");
	}

	return 0;

release:
	platform_device_unregister(pdev);

	return ret;
}
device_initcall(ts5500_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Savoir-faire Linux Inc. <kernel@savoirfairelinux.com>");
MODULE_DESCRIPTION("Technologic Systems TS-5500 platform driver");
