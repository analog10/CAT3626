/*
	cat3626.c - high efficiency 1x/1.5x fractional charge pump with 
		programmable dimming current in six LED channels
	Doug Szumski  <d.s.szumski@gmail.com>  2010-03-29
	based on ds1621.c by Christian W. Zuckschwerdt  <zany@triq.net>
      
	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
  
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
  
	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>

/* The CAT3626 can only exist at 0x66 */
static const unsigned short normal_i2c[] = { 0x66, I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD_1(CAT3626);

/* CAT3626 level registers */
static const u8 CAT3626_REG_LEVEL[3] = {
	0x00,	 	/* CHANNEL A: RED   */
	0x01,		/* CHANNEL B: GREEN */
	0x02,		/* CHANNEL C: BLUE  */
};

/* CAT3626 channel enable data */
static const u8 CAT3626_CHN_ENABLE[3] = {
	0x02,	 	/* CHANNEL A2: RED   */
	0x08,		/* CHANNEL B2: GREEN */
	0x10,		/* CHANNEL C1: BLUE  */
};

/*CAT3626 output control register*/
#define CAT3626_ENA		0x03

/*CAT3626 chip register settings */
/*NOTE: this enables channels A2,B2 and C1*/
#define CAT3626_ENA_CFG		0x1A
#define CAT3626_DIS_CFG		0x00

/*Constrain the maximum channel output to 20mA (limit of the LED)*/
#define CAT3626_MAX_BRI		0x27
#define CAT3626_MIN_BRI		0x00

/*Level and channel data*/
struct CAT3626_data {
	struct device *hwmon_dev;
	struct mutex update_lock;
	u8 level[3];			/* level values, byte */
	u8 channel[3];			/* channel values, byte, should be boolean */
	
};

static int CAT3626_write_level(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

static void CAT3626_init_client(struct i2c_client *client)
{
	/*Turn all channels off on startup */
	i2c_smbus_write_byte_data(client, CAT3626_ENA, CAT3626_DIS_CFG);
}

static ssize_t show_level(struct device *dev, struct device_attribute *da,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct i2c_client *client = to_i2c_client(dev);
	struct CAT3626_data *data = i2c_get_clientdata(client);
	return sprintf(buf, "%d\n", data->level[attr->index]);
}

static ssize_t set_level(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct i2c_client *client = to_i2c_client(dev);
	struct CAT3626_data *data = i2c_get_clientdata(client);

	/* Read the user level input and constrain to the LED capabilities */
	u8 level_input = simple_strtol(buf, NULL, 10); 
	u8 val = SENSORS_LIMIT(level_input, CAT3626_MIN_BRI, CAT3626_MAX_BRI);

	/* Update the LED level */
	mutex_lock(&data->update_lock);
	data->level[attr->index] = val;
	CAT3626_write_level(client, CAT3626_REG_LEVEL[attr->index],
			  data->level[attr->index]);
	mutex_unlock(&data->update_lock);
	
	return count;
}

static ssize_t set_channel(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct i2c_client *client = to_i2c_client(dev);
	struct CAT3626_data *data = i2c_get_clientdata(client);
	u8 conf, new_conf;

	/* Read the user channel input and constrain to boolean (must be a better way?) */
	u8 channel_input = simple_strtol(buf, NULL, 10); 
	u8 val = SENSORS_LIMIT(channel_input, 0, 1);

	/* Update the LED channel */
	mutex_lock(&data->update_lock);
	
	data->channel[attr->index] = val;

	new_conf = conf = i2c_smbus_read_byte_data(client, CAT3626_ENA);
	
	if (val == 0)
		new_conf &= ~CAT3626_CHN_ENABLE[attr->index];
	else if (val == 1)
		new_conf |= CAT3626_CHN_ENABLE[attr->index];
	
	if (conf != new_conf)
		CAT3626_write_level(client, CAT3626_ENA, new_conf);

	mutex_unlock(&data->update_lock);
	
	return count;
}

static ssize_t show_channel(struct device *dev, struct device_attribute *da,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct i2c_client *client = to_i2c_client(dev);
	struct CAT3626_data *data = i2c_get_clientdata(client);
	return sprintf(buf, "%d\n", data->channel[attr->index]);
}


static SENSOR_DEVICE_ATTR(red_level, S_IWUSR | S_IRUGO, show_level, set_level, 0);
static SENSOR_DEVICE_ATTR(grn_level, S_IWUSR | S_IRUGO, show_level, set_level, 1);
static SENSOR_DEVICE_ATTR(blu_level, S_IWUSR | S_IRUGO, show_level, set_level, 2);
static SENSOR_DEVICE_ATTR(red_channel, S_IWUSR | S_IRUGO, show_channel, set_channel, 0);
static SENSOR_DEVICE_ATTR(grn_channel, S_IWUSR | S_IRUGO, show_channel, set_channel, 1);
static SENSOR_DEVICE_ATTR(blu_channel, S_IWUSR | S_IRUGO, show_channel, set_channel, 2);

static struct attribute *CAT3626_attributes[] = {
	&sensor_dev_attr_red_level.dev_attr.attr,
	&sensor_dev_attr_grn_level.dev_attr.attr,
	&sensor_dev_attr_blu_level.dev_attr.attr,
	&sensor_dev_attr_red_channel.dev_attr.attr,
	&sensor_dev_attr_grn_channel.dev_attr.attr,
	&sensor_dev_attr_blu_channel.dev_attr.attr,
	NULL
};

static const struct attribute_group CAT3626_group = {
	.attrs = CAT3626_attributes,
};

static int CAT3626_detect(struct i2c_client *client, int kind,
			 struct i2c_board_info *info)
{
	/*add a detection routine here - currently the chip is always found! */
	strlcpy(info->type, "CAT3626", I2C_NAME_SIZE);
	return 0;
}

static int CAT3626_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct CAT3626_data *data;
	int err;

	data = kzalloc(sizeof(struct CAT3626_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	/* Initialize the CAT3626 chip */
	CAT3626_init_client(client);

	/* Register sysfs hooks */
	if ((err = sysfs_create_group(&client->dev.kobj, &CAT3626_group)))
		goto exit_free;

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove_files;
	}

	return 0;

      exit_remove_files:
	sysfs_remove_group(&client->dev.kobj, &CAT3626_group);
      exit_free:
	kfree(data);
      exit:
	return err;
}

static int CAT3626_remove(struct i2c_client *client)
{
	struct CAT3626_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &CAT3626_group);

	kfree(data);

	return 0;
}

static const struct i2c_device_id CAT3626_id[] = {
	{ "CAT3626", CAT3626 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, CAT3626_id);

/* This is the driver that will be inserted */
static struct i2c_driver CAT3626_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "CAT3626",
	},
	.probe		= CAT3626_probe,
	.remove		= CAT3626_remove,
	.id_table	= CAT3626_id,
	.detect		= CAT3626_detect,
	.address_data	= &addr_data,
};

static int __init CAT3626_init(void)
{
	return i2c_add_driver(&CAT3626_driver);
}

static void __exit CAT3626_exit(void)
{
	i2c_del_driver(&CAT3626_driver);
}


MODULE_AUTHOR("Doug Szumski <d.s.szumski@gmail.com>");
MODULE_DESCRIPTION("CAT3626 RGB LED driver");
MODULE_LICENSE("GPL");

module_init(CAT3626_init);
module_exit(CAT3626_exit);
