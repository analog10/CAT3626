/*
 * cat3626.c - 16-bit Led dimmer
 *
 * Copyright (C) 2011 Jan Weitzel
 * Copyright (C) 2008 Riku Voipio
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * Datasheet: http://www.nxp.com/documents/data_sheet/CAT3626.pdf
 *
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

struct cat3626_led {
	u8 id;
	u8 i2c_reg;
	struct i2c_client *client;
	char *name;
	struct led_classdev ldev;
	struct work_struct work;

	/* Maximum current in microamps.
	 * 0 disables the LED. */
	int maximum_reg_value;

	/* Present current output. */
	int present_reg_value;

	/* The partner LED must have the same current. */
	struct cat3626_led* partner;
};

struct cat3626_platform_data {
	struct cat3626_led leds[6];
};

/* m =  num_leds*/

#define ADDR_REG_A 0
#define ADDR_REG_B 1
#define ADDR_REG_C 2
#define ADDR_REGEN 3

#define ldev_to_led(c)       container_of(c, struct cat3626_led, ldev)

struct cat3626_chip_info {
	u8	num_leds;
};

struct cat3626_data {
	struct i2c_client *client;
	struct cat3626_led leds[6];
	struct mutex update_lock;
	struct input_dev *idev;
	struct work_struct work;

	const struct cat3626_chip_info *chip_info;
};

static int cat3626_probe(struct i2c_client *client,
	const struct i2c_device_id *id);
static int cat3626_remove(struct i2c_client *client);

enum {
	cat3626
};

static const struct i2c_device_id cat3626_id[] = {
	{ "cat3626", cat3626 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, cat3626_id);

static const struct cat3626_chip_info cat3626_chip_info_tbl[] = {
	[cat3626] = {
		.num_leds = 6,
	}
};

static struct i2c_driver cat3626_driver = {
	.driver = {
		.name = "leds-cat3626",
	},
	.probe = cat3626_probe,
	.remove = cat3626_remove,
	.id_table = cat3626_id,
};

/* Set LED routing */
static void cat3626_setled(struct cat3626_led *led)
{
	struct i2c_client *client = led->client;
	struct cat3626_data *data = i2c_get_clientdata(client);
	char reg, toset;

	mutex_lock(&data->update_lock);

	/* Read the present register enable value. */
	reg = i2c_smbus_read_byte_data(client, ADDR_REGEN);
	toset = reg;

	/* Update based on desired LED setting. */
	if(0 == led->present_reg_value){
		toset &= ~(1 << led->id);
	}
	else{
		toset |= 1 << led->id;
	}

	/* If the enable settings change, write the new register. */
	if(toset != reg)
		i2c_smbus_write_byte_data(client, ADDR_REGEN, toset);

	/* If the LED has an nonzero brightness, set it on the device. */
	if(led->present_reg_value > 0)
		i2c_smbus_write_byte_data(client, led->i2c_reg, led->present_reg_value);

	mutex_unlock(&data->update_lock);
}

static void cat3626_brightness_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	struct cat3626_led *led = ldev_to_led(led_cdev);

	/* remove lower 3 bits. */
	value >>= 3;

	if(value > 39)
		value = 39;

	led->present_reg_value = value;
	schedule_work(&led->work);
}

static void cat3626_led_work(struct work_struct *work)
{
	struct cat3626_led *led;
	led = container_of(work, struct cat3626_led, work);
	cat3626_setled(led);
}

static int cat3626_destroy_devices(struct cat3626_data *data, int n_devs)
{
	int i = n_devs;

	if (!data)
		return -EINVAL;

	led_classdev_unregister(&data->leds[i].ldev);
	cancel_work_sync(&data->leds[i].work);

	return 0;
}

static int cat3626_configure(struct i2c_client *client,
	struct cat3626_data *data, struct cat3626_platform_data *pdata)
{
	int i, err = 0;

	/* Set the partner fields. */
	for (i = 0; i < data->chip_info->num_leds; i += 2) {
		data->leds[i].partner = data->leds + i + 1;
		data->leds[i + 1].partner = data->leds + i;
		data->leds[i].i2c_reg = i >> 1;
		data->leds[i + 1].i2c_reg = i >> 1;
	}

	for (i = 0; i < data->chip_info->num_leds; i++) {
		struct cat3626_led *led = &data->leds[i];
		struct cat3626_led *pled = &pdata->leds[i];
		led->client = client;
		led->id = i;

		led->name = pled->name;
		led->ldev.name = led->name;
		led->ldev.brightness = LED_OFF;
		/* 20ma max */
		led->ldev.max_brightness = (39 << 3);
		led->ldev.brightness_set = cat3626_brightness_set;
		INIT_WORK(&led->work, cat3626_led_work);
		err = led_classdev_register(&client->dev, &led->ldev);
		if (err < 0) {
			dev_err(&client->dev,
				"couldn't register LED %s\n",
				led->name);
			goto exit;
		}
		cat3626_setled(led);
	}

	return 0;

exit:
	cat3626_destroy_devices(data, i);
	return err;
}

static int cat3626_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct cat3626_data *data = i2c_get_clientdata(client);
	struct cat3626_platform_data *cat3626_pdata =
			dev_get_platdata(&client->dev);

	if (!cat3626_pdata)
		return -EIO;

	if (!i2c_check_functionality(client->adapter,
		I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->chip_info = &cat3626_chip_info_tbl[id->driver_data];

	dev_info(&client->dev, "setting platform data\n");
	i2c_set_clientdata(client, data);
	data->client = client;
	mutex_init(&data->update_lock);

	return cat3626_configure(client, data, cat3626_pdata);
}

static int cat3626_remove(struct i2c_client *client)
{
	struct cat3626_data *data = i2c_get_clientdata(client);
	int err;

	err = cat3626_destroy_devices(data, data->chip_info->num_leds);
	if (err)
		return err;

	return 0;
}

module_i2c_driver(cat3626_driver);

MODULE_AUTHOR("David Bender");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CAT3626 LED Driver");
