/*
 * GPIO (DIO) header for Technologic Systems TS-5500
 *
 * Copyright (c) 2012 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The TS-5500 board has 38 Digital Input/Output lines, referred to as DIO in
 * the product's literature. See section 6 "Digital I/O" of the datasheet:
 * http://embeddedx86.com/documentation/ts-5500-manual.pdf
 *
 * As each IRQable line is input only, the platform data has an option for each
 * header to return the corresponding IRQ line on request. This might be useful
 * to bind a bidirectional line with the IRQ line of the same header.
 */

/**
 * struct ts5500_gpio_platform_data - TS-5500 GPIO configuration
 * @base:	The GPIO base number to use.
 * @lcd_dio:	Use the LCD port as 11 additional digital I/O lines.
 * @lcd_irq:	Return IRQ1 for every line of LCD DIO header.
 * @dio1_irq:	Return IRQ7 for every line of DIO1 header.
 * @dio2_irq:	Return IRQ6 for every line of DIO2 header.
 */
struct ts5500_gpio_platform_data {
	int base;
	bool lcd_dio;
	bool lcd_irq;
	bool dio1_irq;
	bool dio2_irq;
};
