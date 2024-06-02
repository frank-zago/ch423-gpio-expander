// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * WinChipHead CH423 -- GPIO expander driver
 *
 * Copyright 2021, Frank Zago
 *
 * This chip is both a GPIO expander and a LED driver. The LED driver
 * operations, which use the IO and OC pins differently, are not
 * supported.
 *
 * The exposed GPIOs are:
 *  0 to  7: regular Input or Output, marked IO0 to IO7 in the datasheet.
 *  8 to 23: Output only, marked OC0 to OC15 in the datasheet.
 *
 * However there are some limitations:
 *   - The IO lines are grouped together to be either all input or all
 *     output (IO_OE config bit).
 *
 *   - Similarly, the OC lines can be push-pull (default) or
 *     open-drain (OD_EN config bit), but with the same setting for
 *     all of them.
 *
 *   - This i2c device doesn't have an address; instead each command
 *     is an address. 5 of these commands (0x22, 0x23, 0x24, 0x26 and
 *     0x30) are used by this driver, but 0x31 to 0x3f also exist. The
 *     address that is registered with the i2c subsystem will be
 *     ignored. This possibly means this chip should be the only
 *     device on an i2c bus.
 *
 * The Chinese datasheet and some 8051 code samples are available at:
 *     http://www.wch.cn/downloads/CH423EVT_ZIP.html
 */

#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>

#define CMD_SET_OC_L 0x22
#define CMD_SET_OC_H 0x23
#define CMD_CFG 0x24
#define   CMD_CFG_IO_OE  BIT(0)
#define   CMD_CFG_DEC_L  BIT(1)
#define   CMD_CFG_DEC_H  BIT(2)
#define   CMD_CFG_X_INT  BIT(3)
#define   CMD_CFG_OD_EN  BIT(4)
#define   CMD_CFG_INTENS GENMASK(6, 5)
#define   CMD_CFG_SLEEP  BIT(7)
#define CMD_READ_IO 0x26
#define CMD_SET_IO 0x30

struct ch423 {
	struct i2c_client *client;
	struct gpio_chip gpio;
	struct mutex lock;

	/* Current chip configuration. Bitfield of CMD_CFG_xxx */
	u8 config;

	/* GPIO_LINE_DIRECTION_XXX for the 8 bi-directional I/Os. */
	int io_dir;

	/* Output values for the 24 GPIOs. Bit 0 to 7 are the
	 * bi-directional I/O, bits 8 to 15 are the low OC and bits 16
	 * to 23 are the high OC. Protected by the lock.
	 */
	unsigned long last_output;
};

static int ch423_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct ch423 *dev = gpiochip_get_data(chip);

	if (offset >= 8)
		return GPIO_LINE_DIRECTION_OUT;
	else
		return dev->io_dir;
}

static int ch423_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct ch423 *dev = gpiochip_get_data(chip);
	u8 param;
	struct i2c_msg msg = {
		.addr = CMD_READ_IO,
		.len = 1,
		.buf = &param,
		.flags = I2C_M_RD,
	};
	int ret;

	ret = i2c_transfer(dev->client->adapter, &msg, 1);
	if (ret < 0)
		return ret;

	return (param >> offset) & 0x01;
}

/* Write the desired GPIOs output. Optimize by not issuing i2c
 * commands when it's not needed.
 */
static int write_outputs(struct ch423 *dev, unsigned long values)
{
	u8 param;
	struct i2c_msg msg = {
		.len = 1,
		.buf = &param,
	};
	int ret;

	if (dev->io_dir == GPIO_LINE_DIRECTION_OUT &&
	    (values & 0xff) != (dev->last_output & 0xff)) {
		msg.addr = CMD_SET_IO;
		param = values & 0xff;
		ret = i2c_transfer(dev->client->adapter, &msg, 1);
		if (ret < 0)
			return ret;
	}

	if ((values & 0xff00) != (dev->last_output & 0xff00)) {
		msg.addr = CMD_SET_OC_L;
		param = (values >> 8) & 0xff;
		ret = i2c_transfer(dev->client->adapter, &msg, 1);
		if (ret < 0)
			return ret;
	}

	if ((values & 0xff0000) != (dev->last_output & 0xff0000)) {
		msg.addr = CMD_SET_OC_H;
		param = (values >> 16) & 0xff;
		i2c_transfer(dev->client->adapter, &msg, 1);
		if (ret < 0)
			return ret;
	}

	dev->last_output = values;

	return 0;
}

static void ch423_gpio_set_multiple(struct gpio_chip *chip,
				    unsigned long *mask, unsigned long *bits)
{
	struct ch423 *dev = gpiochip_get_data(chip);
	unsigned long values;

	mutex_lock(&dev->lock);

	values = dev->last_output;
	values &= ~*mask;
	values |= (*bits & *mask);

	write_outputs(dev, values);

	mutex_unlock(&dev->lock);
}

static void ch423_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct ch423 *dev = gpiochip_get_data(chip);
	unsigned long values;

	mutex_lock(&dev->lock);

	values = dev->last_output;
	values &= ~BIT(offset);
	values |= value << offset;

	write_outputs(dev, values);

	mutex_unlock(&dev->lock);
}

/* Set some of the chip configuration bits. */
static int set_config(struct ch423 *dev, u8 config)
{
	struct i2c_msg msg = {
		.addr = CMD_CFG,
		.len = 1,
		.buf = &config,
	};
	int ret;

	if (dev->config == config)
		return 0;

	ret = i2c_transfer(dev->client->adapter, &msg, 1);
	if (ret < 0)
		return ret;

	dev->config = config;

	return 0;
}

static int ch423_gpio_direction_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct ch423 *dev = gpiochip_get_data(chip);
	int ret;

	if (offset >= 8)
		return -EINVAL;

	ret = set_config(dev, dev->config & ~CMD_CFG_IO_OE);
	if (ret == 0)
		dev->io_dir = GPIO_LINE_DIRECTION_IN;

	return ret;
}

static int ch423_gpio_direction_output(struct gpio_chip *chip,
				       unsigned int offset, int value)
{
	struct ch423 *dev = gpiochip_get_data(chip);
	int ret;

	if (offset < 8 && dev->io_dir != GPIO_LINE_DIRECTION_OUT) {
		ret = set_config(dev, dev->config | CMD_CFG_IO_OE);
		if (ret < 0)
			return ret;

		dev->io_dir = GPIO_LINE_DIRECTION_OUT;
	}

	ch423_gpio_set(chip, offset, value);

	return 0;
}

static int ch423_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
				 unsigned long config)
{
	struct ch423 *dev = gpiochip_get_data(chip);
	int ret;
	enum pin_config_param param = pinconf_to_config_param(config);

	if (offset < 8)
		return -ENOTSUPP;

	if (param == PIN_CONFIG_DRIVE_OPEN_DRAIN)
		ret = set_config(dev, dev->config | CMD_CFG_OD_EN);
	else if (param == PIN_CONFIG_DRIVE_PUSH_PULL)
		ret = set_config(dev, dev->config & ~CMD_CFG_OD_EN);
	else
		ret = -ENOTSUPP;

	return ret;
}

static const struct gpio_chip ch423_gpio_chip = {
	.owner = THIS_MODULE,
	.label = "ch423",
	.set_config = ch423_gpio_set_config,
	.get_direction = ch423_gpio_get_direction,
	.direction_input = ch423_gpio_direction_input,
	.direction_output = ch423_gpio_direction_output,
	.get = ch423_gpio_get,
	.set = ch423_gpio_set,
	.set_multiple = ch423_gpio_set_multiple,
	.can_sleep = true,
	.base = -1,
	.ngpio = 24,
};

static int ch423_probe(struct i2c_client *client
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,3,0)
		       , const struct i2c_device_id *id
#endif
)
{
	struct gpio_chip *gpio;
	struct ch423 *dev;
	int ret;

	dev = devm_kzalloc(&client->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->client = client;
	mutex_init(&dev->lock);

	i2c_set_clientdata(client, dev);

	gpio = &dev->gpio;
	*gpio = ch423_gpio_chip;
	gpio->parent = &client->dev;

	/* On power-up, the state of the chip is config=0, IO pins are
	 * input, and OC pins are output high. Reset the chip that
	 * way, except set the OC pins to low.
	 */
	dev->io_dir = GPIO_LINE_DIRECTION_IN;
	dev->config = 0xff;
	ret = set_config(dev, 0);
	if (ret)
		return ret;

	dev->last_output = 0xffff00;
	ret = write_outputs(dev, 0);
	if (ret)
		return ret;

	return devm_gpiochip_add_data(&client->dev, gpio, dev);
}

static const struct i2c_device_id ch423_i2c_id[] = {
	{ "ch423" },
	{},
};
MODULE_DEVICE_TABLE(i2c, ch423_i2c_id);

static struct i2c_driver ch423_driver = {
	.driver = {
		.name   = "ch423",
	},
	.probe      = ch423_probe,
	.id_table   = ch423_i2c_id,
};

module_i2c_driver(ch423_driver);

MODULE_AUTHOR("Frank Zago");
MODULE_DESCRIPTION("GPIO expander driver for CH423");
MODULE_LICENSE("GPL");
