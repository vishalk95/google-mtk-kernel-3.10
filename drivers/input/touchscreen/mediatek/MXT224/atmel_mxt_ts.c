/*
 * Atmel maXTouch Touchscreen driver
 *
 * Copyright (C) 2010 Samsung Electronics Co.Ltd
 * Copyright (C) 2011-2014 Atmel Corporation
 * Copyright (C) 2012 Google, Inc.
 *
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <mach/mt_pm_ldo.h>
#include <cust_eint.h>
#include "atmel_mxt_ts.h"
#include "tpd.h"
#include "tpd_custom_mxt224.h"

//Global variable
static const struct i2c_device_id mxt_tpd_id[] = {{MXT224_DEV_NAME,0},{}};
static struct i2c_board_info __initdata mxt_i2c_tpd={ I2C_BOARD_INFO(MXT224_DEV_NAME, MXT_I2C_ADDRESS)};
static u8 *MXTI2CDMABuf_va = NULL;
static u64 MXTI2CDMABuf_pa = NULL;
static int tpd_flag;
static char atmel_cfg_path[] = "maxtouch.raw";
static struct task_struct *thread = NULL;
static int thread_exit = FALSE;

static DECLARE_WAIT_QUEUE_HEAD(waiter);

//Declare
extern struct tpd_device *tpd;
static struct i2c_driver tpd_i2c_driver;
static int mxt_input_open(struct input_dev *dev);
static void mxt_input_close(struct input_dev *dev);

#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#endif

/* Configuration file */
#define MXT_CFG_MAGIC			"OBP_RAW V1"

/* Registers */
#define MXT_OBJECT_START		0x07
#define MXT_OBJECT_SIZE			6
#define MXT_INFO_CHECKSUM_SIZE		3
#define MXT_MAX_BLOCK_WRITE		255

/* Object types */
#define MXT_DEBUG_DIAGNOSTIC_T37	37
#define MXT_GEN_MESSAGE_T5		5
#define MXT_GEN_COMMAND_T6		6
#define MXT_GEN_POWER_T7		7
#define MXT_GEN_ACQUIRE_T8		8
#define MXT_GEN_DATASOURCE_T53		53
#define MXT_TOUCH_MULTI_T9		9
#define MXT_TOUCH_KEYARRAY_T15		15
#define MXT_TOUCH_PROXIMITY_T23		23
#define MXT_TOUCH_PROXKEY_T52		52
#define MXT_PROCI_GRIPFACE_T20		20
#define MXT_PROCG_NOISE_T22		22
#define MXT_PROCI_ONETOUCH_T24		24
#define MXT_PROCI_TWOTOUCH_T27		27
#define MXT_PROCI_GRIP_T40		40
#define MXT_PROCI_PALM_T41		41
#define MXT_PROCI_TOUCHSUPPRESSION_T42	42
#define MXT_PROCI_STYLUS_T47		47
#define MXT_PROCG_NOISESUPPRESSION_T48	48
#define MXT_SPT_COMMSCONFIG_T18		18
#define MXT_SPT_GPIOPWM_T19		19
#define MXT_SPT_SELFTEST_T25		25
#define MXT_SPT_CTECONFIG_T28		28
#define MXT_SPT_USERDATA_T38		38
#define MXT_SPT_DIGITIZER_T43		43
#define MXT_SPT_MESSAGECOUNT_T44	44
#define MXT_SPT_CTECONFIG_T46		46
#define MXT_PROCI_ACTIVE_STYLUS_T63	63
#define MXT_TOUCH_MULTITOUCHSCREEN_T100	100
#define MXT_PROCI_ACTIVESTYLUS_T107	107

/* MXT_GEN_MESSAGE_T5 object */
#define MXT_RPTID_NOMSG			0xff

/* MXT_GEN_COMMAND_T6 field */
#define MXT_COMMAND_RESET		0
#define MXT_COMMAND_BACKUPNV		1
#define MXT_COMMAND_CALIBRATE		2
#define MXT_COMMAND_REPORTALL		3
#define MXT_COMMAND_DIAGNOSTIC		5

/* Define for T6 status byte */
#define MXT_T6_STATUS_RESET		(1 << 7)
#define MXT_T6_STATUS_OFL		(1 << 6)
#define MXT_T6_STATUS_SIGERR		(1 << 5)
#define MXT_T6_STATUS_CAL		(1 << 4)
#define MXT_T6_STATUS_CFGERR		(1 << 3)
#define MXT_T6_STATUS_COMSERR		(1 << 2)

/* MXT_GEN_POWER_T7 field */
struct t7_config {
	u8 idle;
	u8 active;
} __packed;

#define MXT_POWER_CFG_RUN		0
#define MXT_POWER_CFG_DEEPSLEEP		1

/* MXT_TOUCH_MULTI_T9 field */
#define MXT_T9_ORIENT			9
#define MXT_T9_RANGE			18

/* MXT_TOUCH_MULTI_T9 status */
#define MXT_T9_UNGRIP			(1 << 0)
#define MXT_T9_SUPPRESS			(1 << 1)
#define MXT_T9_AMP			(1 << 2)
#define MXT_T9_VECTOR			(1 << 3)
#define MXT_T9_MOVE			(1 << 4)
#define MXT_T9_RELEASE			(1 << 5)
#define MXT_T9_PRESS			(1 << 6)
#define MXT_T9_DETECT			(1 << 7)

struct t9_range {
	u16 x;
	u16 y;
} __packed;

/* MXT_TOUCH_MULTI_T9 orient */
#define MXT_T9_ORIENT_SWITCH		(1 << 0)

/* MXT_SPT_COMMSCONFIG_T18 */
#define MXT_COMMS_CTRL			0
#define MXT_COMMS_CMD			1
#define MXT_COMMS_RETRIGEN		(1 << 6)

/* Define for MXT_GEN_COMMAND_T6 */
#define MXT_BOOT_VALUE			0xa5
#define MXT_RESET_VALUE			0x01
#define MXT_BACKUP_VALUE		0x55

/* Define for MXT_PROCI_TOUCHSUPPRESSION_T42 */
#define MXT_T42_MSG_TCHSUP		(1 << 0)

/* T63 Stylus */
#define MXT_T63_STYLUS_PRESS		(1 << 0)
#define MXT_T63_STYLUS_RELEASE		(1 << 1)
#define MXT_T63_STYLUS_MOVE		(1 << 2)
#define MXT_T63_STYLUS_SUPPRESS		(1 << 3)

#define MXT_T63_STYLUS_DETECT		(1 << 4)
#define MXT_T63_STYLUS_TIP		(1 << 5)
#define MXT_T63_STYLUS_ERASER		(1 << 6)
#define MXT_T63_STYLUS_BARREL		(1 << 7)

#define MXT_T63_STYLUS_PRESSURE_MASK	0x3F

/* T100 Multiple Touch Touchscreen */
#define MXT_T100_CTRL			0
#define MXT_T100_CFG1			1
#define MXT_T100_TCHAUX			3
#define MXT_T100_XRANGE			13
#define MXT_T100_YRANGE			24

#define MXT_T100_CFG_SWITCHXY		(1 << 5)

#define MXT_T100_TCHAUX_VECT		(1 << 0)
#define MXT_T100_TCHAUX_AMPL		(1 << 1)
#define MXT_T100_TCHAUX_AREA		(1 << 2)

#define MXT_T100_DETECT			(1 << 7)
#define MXT_T100_TYPE_MASK		0x70

enum t100_type {
	MXT_T100_TYPE_FINGER		= 1,
	MXT_T100_TYPE_PASSIVE_STYLUS	= 2,
	MXT_T100_TYPE_ACTIVE_STYLUS	= 3,
	MXT_T100_TYPE_HOVERING_FINGER	= 4,
	MXT_T100_TYPE_GLOVE		= 5,
	MXT_T100_TYPE_LARGE_TOUCH	= 6,
};

/* Gen2 Active Stylus */
#define MXT_T107_STYLUS_STYAUX		42
#define MXT_T107_STYLUS_STYAUX_PRESSURE	(1 << 0)
#define MXT_T107_STYLUS_STYAUX_PEAK	(1 << 4)

#define MXT_T107_STYLUS_HOVER		(1 << 0)
#define MXT_T107_STYLUS_TIPSWITCH	(1 << 1)
#define MXT_T107_STYLUS_BUTTON0		(1 << 2)
#define MXT_T107_STYLUS_BUTTON1		(1 << 3)

#define MXT_TOUCH_MAJOR_DEFAULT		1
#define MXT_PRESSURE_DEFAULT		1

/* Delay times */
#define MXT_BACKUP_TIME		50	/* msec */
#define MXT_RESET_TIME		200	/* msec */
#define MXT_RESET_TIMEOUT	3000	/* msec */
#define MXT_CRC_TIMEOUT		1000	/* msec */
#define MXT_FW_RESET_TIME	3000	/* msec */
#define MXT_FW_CHG_TIMEOUT	300	/* msec */
#define MXT_WAKEUP_TIME		25	/* msec */
#define MXT_REGULATOR_DELAY	150	/* msec */
#define MXT_CHG_DELAY		100	/* msec */
#define MXT_POWERON_DELAY	150	/* msec */

/* Command to unlock bootloader */
#define MXT_UNLOCK_CMD_MSB	0xaa
#define MXT_UNLOCK_CMD_LSB	0xdc

/* Bootloader mode status */
#define MXT_WAITING_BOOTLOAD_CMD	0xc0	/* valid 7 6 bit only */
#define MXT_WAITING_FRAME_DATA	0x80	/* valid 7 6 bit only */
#define MXT_FRAME_CRC_CHECK	0x02
#define MXT_FRAME_CRC_FAIL	0x03
#define MXT_FRAME_CRC_PASS	0x04
#define MXT_APP_CRC_FAIL	0x40	/* valid 7 8 bit only */
#define MXT_BOOT_STATUS_MASK	0x3f
#define MXT_BOOT_EXTENDED_ID	(1 << 5)
#define MXT_BOOT_ID_MASK	0x1f

/* Touchscreen absolute values */
#define MXT_MAX_AREA		0xff

#define MXT_PIXELS_PER_MM	20

#define DEBUG_MSG_MAX		200

struct mxt_info {
	u8 family_id;
	u8 variant_id;
	u8 version;
	u8 build;
	u8 matrix_xsize;
	u8 matrix_ysize;
	u8 object_num;
};

struct mxt_object {
	u8 type;
	u16 start_address;
	u8 size_minus_one;
	u8 instances_minus_one;
	u8 num_report_ids;
} __packed;

/* Each client has this additional data */
struct mxt_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	char phys[64];		/* device physical location */
	const struct mxt_platform_data *pdata;
	struct mxt_object *object_table;
	struct mxt_info *info;
	void *raw_info_block;
	unsigned int irq;
	unsigned int max_x;
	unsigned int max_y;
	bool in_bootloader;
	u16 mem_size;
	u8 t100_aux_ampl;
	u8 t100_aux_area;
	u8 t100_aux_vect;
	struct bin_attribute mem_access_attr;
	bool debug_enabled;
	bool debug_v2_enabled;
	u8 *debug_msg_data;
	u16 debug_msg_count;
	struct bin_attribute debug_msg_attr;
	struct mutex debug_msg_lock;
	u8 max_reportid;
	u32 config_crc;
	u32 info_crc;
	u8 bootloader_addr;
	u8 *msg_buf;
	u8 t6_status;
	bool update_input;
	u8 last_message_count;
	u8 num_touchids;
	struct t7_config t7_cfg;
	u8 num_stylusids;
	unsigned long t15_keystatus;
	u8 stylus_aux_pressure;
	u8 stylus_aux_peak;
	bool use_retrigen_workaround;
	bool use_regulator;
	struct regulator *reg_vdd;
	struct regulator *reg_avdd;
	char *fw_name;
	char *cfg_name;

	/* Cached parameters from object table */
	u16 T5_address;
	u8 T5_msg_size;
	u8 T6_reportid;
	u16 T6_address;
	u16 T7_address;
	u8 T9_reportid_min;
	u8 T9_reportid_max;
	u16 T9_address;
	u8 T15_reportid_min;
	u8 T15_reportid_max;
	u16 T18_address;
	u8 T19_reportid;
	u8 T42_reportid_min;
	u8 T42_reportid_max;
	u16 T44_address;
	u8 T48_reportid;
	u8 T63_reportid_min;
	u8 T63_reportid_max;
	u8 T100_reportid_min;
	u8 T100_reportid_max;
	u16 T107_address;

	/* for fw update in bootloader */
	struct completion bl_completion;

	/* for reset handling */
	struct completion reset_completion;

	/* for config update handling */
	struct completion crc_completion;

	/* Indicates whether device is in suspend */
	bool suspended;

	/* Indicates whether device is updating configuration */
	bool updating_config;
};


static s32 i2c_dma_read(struct i2c_client *client, u16 addr, u8 *rxbuf, s32 len)
{
	int ret;
	s32 retry = 0;
	u8 buffer[2];

	struct i2c_msg msg[2] =
	{
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.flags = 0,//(client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG),
			.buf = buffer,
			.len = 2,
			.timing = I2C_MASTER_CLOCK
		},
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.ext_flag = (client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG),
			.flags = I2C_M_RD,
			.buf = MXTI2CDMABuf_pa,
			.len = len,
			.timing = I2C_MASTER_CLOCK
		},
	};

	buffer[0] = addr & 0xFF;
	buffer[1] = (addr>>8) & 0xFF;

	if (rxbuf == NULL)
		return -1;

	//TPD_DEBUG("dma i2c read: 0x%04X, %d bytes(s)", addr, len);
	for (retry = 0; retry < 10; ++retry)
	{
		ret = i2c_transfer(client->adapter, &msg[0], 2);
		if (ret < 0)
		{
			TPD_DMESG("I2C DMA read error retry=%d\n", retry);
			continue;
		}
		memcpy(rxbuf, MXTI2CDMABuf_va, len);
		return 0;
	}
	TPD_DMESG("Dma I2C Read Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);
	return ret;
}


static s32 i2c_dma_write(struct i2c_client *client, u16 addr, u8 *txbuf, s32 len)
{
	int ret;
	s32 retry = 0;
	u8 *wr_buf = MXTI2CDMABuf_va;
	//TPD_DEBUG("fwq3,.....%x",txbuf[0]);
	struct i2c_msg msg =
	{
		.addr = (client->addr & I2C_MASK_FLAG),
		.ext_flag = (client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG),
		.flags = 0,
		.buf = MXTI2CDMABuf_pa,
		.len = 2+len,
		.timing = I2C_MASTER_CLOCK
	};

	wr_buf[0] = (addr & 0xFF);
	wr_buf[1] = (addr >> 8) & 0xFF;

	if (txbuf == NULL)
		return -1;

	//TPD_DEBUG("dma i2c write: 0x%04X, %d bytes(s)", addr, len);
	memcpy(wr_buf+2, txbuf, len);

	for (retry = 0; retry < 5; ++retry)
	{
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0)
		{
			TPD_DMESG("I2C DMA write error retry=%d\n", retry);
			continue;
		}
		return 0;
	}
	TPD_DMESG("Dma I2C Write Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);
	return ret;
}

static s32 i2c_read_bytes_dma(struct i2c_client *client, u16 addr, u8 *rxbuf, s32 len)
{
	s32 left = len;
	s32 read_len = 0;
	u8 *rd_buf = rxbuf;
	s32 ret = 0;

	//TPD_DMBUG("[I2C_READ]0x%04X, %d byte(s)\n", addr, len);
	while (left > 0)
	{
		if (left > TPD_DMA_MAX_TRANSACTION_LENGTH)
		{
			read_len = TPD_DMA_MAX_TRANSACTION_LENGTH;
		}
		else
		{
			read_len = left;
		}
		ret = i2c_dma_read(client, addr, rd_buf, read_len);
		if (ret < 0)
		{
			TPD_DEBUG("dma read failed\n");
			return -1;
		}

		left -= read_len;
		addr += read_len;
		rd_buf += read_len;
	}
	return 0;
}

static s32 i2c_write_bytes_dma(struct i2c_client *client, u16 addr, u8 *txbuf, s32 len)
{

	s32 ret = 0;
	s32 write_len = 0;
	s32 left = len;
	u8 *wr_buf = txbuf;
	//TPD_DEBUG("fwq2...%x",txbuf[0]);
	//TPD_DEBUG("[I2C_WRITE]0x%04X, %d byte(s)\n", addr, len);
	while (left > 0)
	{
		if (left > TPD_DMA_MAX_I2C_TRANSFER_SIZE)
		{
			write_len = TPD_DMA_MAX_I2C_TRANSFER_SIZE;
		}
		else
		{
			write_len = left;
		}
		ret = i2c_dma_write(client, addr, wr_buf, write_len);

		if (ret < 0)
		{
			TPD_DEBUG("dma i2c write failed!\n");
			return -1;
		}

		left -= write_len;
		addr += write_len;
		wr_buf += write_len;
	}
	return 0;
}

static int read_mem(struct i2c_client *client, u16 reg, u8 len, u8 *buf)
{
	return i2c_read_bytes_dma(client, reg, buf, len);
}

static int write_mem(struct i2c_client *client, u16 reg, u8 len, const u8 *buf)
{
	return i2c_write_bytes_dma(client, reg, buf, len);
}

static size_t mxt_obj_size(const struct mxt_object *obj)
{
	return obj->size_minus_one + 1;
}

static size_t mxt_obj_instances(const struct mxt_object *obj)
{
	return obj->instances_minus_one + 1;
}

static bool mxt_object_readable(unsigned int type)
{
	switch (type) {
	case MXT_GEN_COMMAND_T6:
	case MXT_GEN_POWER_T7:
	case MXT_GEN_ACQUIRE_T8:
	case MXT_GEN_DATASOURCE_T53:
	case MXT_TOUCH_MULTI_T9:
	case MXT_TOUCH_KEYARRAY_T15:
	case MXT_TOUCH_PROXIMITY_T23:
	case MXT_TOUCH_PROXKEY_T52:
	case MXT_PROCI_GRIPFACE_T20:
	case MXT_PROCG_NOISE_T22:
	case MXT_PROCI_ONETOUCH_T24:
	case MXT_PROCI_TWOTOUCH_T27:
	case MXT_PROCI_GRIP_T40:
	case MXT_PROCI_PALM_T41:
	case MXT_PROCI_TOUCHSUPPRESSION_T42:
	case MXT_PROCI_STYLUS_T47:
	case MXT_PROCG_NOISESUPPRESSION_T48:
	case MXT_SPT_COMMSCONFIG_T18:
	case MXT_SPT_GPIOPWM_T19:
	case MXT_SPT_SELFTEST_T25:
	case MXT_SPT_CTECONFIG_T28:
	case MXT_SPT_USERDATA_T38:
	case MXT_SPT_DIGITIZER_T43:
	case MXT_SPT_CTECONFIG_T46:
		return true;
	default:
		return false;
	}
}

static void mxt_dump_message(struct mxt_data *data, u8 *message)
{
	TPD_DEBUG( "MXT MSG: %*ph\n",
			   data->T5_msg_size, message);
}

static void mxt_debug_msg_enable(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;

	if (data->debug_v2_enabled)
		return;

	mutex_lock(&data->debug_msg_lock);

	data->debug_msg_data = kcalloc(DEBUG_MSG_MAX,
				data->T5_msg_size, GFP_KERNEL);
	if (!data->debug_msg_data)
		return;

	data->debug_v2_enabled = true;
	mutex_unlock(&data->debug_msg_lock);

	TPD_DMESG("Enabled message output\n");
}

static void mxt_debug_msg_disable(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;

	if (!data->debug_v2_enabled)
		return;

	TPD_DMESG("disabling message output\n");
	data->debug_v2_enabled = false;

	mutex_lock(&data->debug_msg_lock);
	kfree(data->debug_msg_data);
	data->debug_msg_data = NULL;
	data->debug_msg_count = 0;
	mutex_unlock(&data->debug_msg_lock);
	TPD_DMESG("Disabled message output\n");
}

static void mxt_debug_msg_add(struct mxt_data *data, u8 *msg)
{
	struct device *dev = &data->client->dev;

	mutex_lock(&data->debug_msg_lock);

	if (!data->debug_msg_data) {
		TPD_DMESG("No buffer!\n");
		return;
	}

	if (data->debug_msg_count < DEBUG_MSG_MAX) {
		memcpy(data->debug_msg_data +
			   data->debug_msg_count * data->T5_msg_size,
			   msg,
			   data->T5_msg_size);
		data->debug_msg_count++;
	} else {
		TPD_DMESG("Discarding %u messages\n", data->debug_msg_count);
		data->debug_msg_count = 0;
	}

	mutex_unlock(&data->debug_msg_lock);

	sysfs_notify(&data->client->dev.kobj, NULL, "debug_notify");
}

static ssize_t mxt_debug_msg_write(struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buf, loff_t off,
	size_t count)
{
	return -EIO;
}

static ssize_t mxt_debug_msg_read(struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buf, loff_t off, size_t bytes)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct mxt_data *data = dev_get_drvdata(dev);
	int count;
	size_t bytes_read;

	if (!data->debug_msg_data) {
		TPD_DMESG("No buffer!\n");
		return 0;
	}

	count = bytes / data->T5_msg_size;

	if (count > DEBUG_MSG_MAX)
		count = DEBUG_MSG_MAX;

	mutex_lock(&data->debug_msg_lock);

	if (count > data->debug_msg_count)
		count = data->debug_msg_count;

	bytes_read = count * data->T5_msg_size;

	memcpy(buf, data->debug_msg_data, bytes_read);
	data->debug_msg_count = 0;

	mutex_unlock(&data->debug_msg_lock);

	return bytes_read;
}

static int mxt_debug_msg_init(struct mxt_data *data)
{
	sysfs_bin_attr_init(&data->debug_msg_attr);
	data->debug_msg_attr.attr.name = "debug_msg";
	data->debug_msg_attr.attr.mode = 0666;
	data->debug_msg_attr.read = mxt_debug_msg_read;
	data->debug_msg_attr.write = mxt_debug_msg_write;
	data->debug_msg_attr.size = data->T5_msg_size * DEBUG_MSG_MAX;

	if (sysfs_create_bin_file(&data->client->dev.kobj,
				  &data->debug_msg_attr) < 0) {
		TPD_DMESG("Failed to create %s\n",
			data->debug_msg_attr.attr.name);
		return -EINVAL;
	}

	return 0;
}

static void mxt_debug_msg_remove(struct mxt_data *data)
{
	if (data->debug_msg_attr.attr.name)
		sysfs_remove_bin_file(&data->client->dev.kobj,
					  &data->debug_msg_attr);
}

static int mxt_wait_for_completion(struct mxt_data *data,
				   struct completion *comp,
				   unsigned int timeout_ms)
{
	struct device *dev = &data->client->dev;
	unsigned long timeout = msecs_to_jiffies(timeout_ms);
	long ret;

	ret = wait_for_completion_interruptible_timeout(comp, timeout);
	if (ret < 0) {
		return ret;
	} else if (ret == 0) {
		TPD_DMESG("Wait for completion timed out.\n");
		return -ETIMEDOUT;
	}
	return 0;
}

static int mxt_bootloader_read(struct mxt_data *data,
				   u8 *val, unsigned int count)
{
	int ret;

	struct i2c_msg msg;

	msg.addr = data->bootloader_addr;
	//msg.flags = data->client->flags & I2C_M_TEN;
	msg.flags |= I2C_M_RD;
	msg.len = count;
	msg.buf = val;
	msg.timing = I2C_MASTER_CLOCK;

	ret = i2c_transfer(data->client->adapter, &msg, 1);
	if (ret == 1) {
		ret = 0;
	} else {
		ret = ret < 0 ? ret : -EIO;
		TPD_DMESG("%s: i2c recv failed (%d)\n",
			__func__, ret);
	}

	return ret;
}

static int mxt_bootloader_write(struct mxt_data *data,
				const u8 * const val, unsigned int count)
{
	int ret;
	struct i2c_msg msg;

	msg.addr = data->bootloader_addr;
	msg.flags = data->client->flags & I2C_M_TEN;
	msg.len = count;
	msg.buf = (u8 *)val;

	ret = i2c_transfer(data->client->adapter, &msg, 1);
	if (ret == 1) {
		ret = 0;
	} else {
		ret = ret < 0 ? ret : -EIO;
		TPD_DMESG("%s: i2c send failed (%d)\n",
			__func__, ret);
	}

	return ret;
}

static int mxt_lookup_bootloader_address(struct mxt_data *data, bool retry)
{
	u8 appmode = data->client->addr;
	u8 bootloader;
	u8 family_id = data->info ? data->info->family_id : 0;

	switch (appmode) {
	case 0x4a:
	case 0x4b:
		/* Chips after 1664S use different scheme */
		if (retry || family_id >= 0xa2) {
			bootloader = appmode - 0x24;
			break;
		}
		/* Fall through for normal case */
	case 0x4c:
	case 0x4d:
	case 0x5a:
	case 0x5b:
		bootloader = appmode - 0x26;
		break;

	default:
		TPD_DMESG(
			"Appmode i2c address 0x%02x not found\n",
			appmode);
		return -EINVAL;
	}

	data->bootloader_addr = bootloader;
	return 0;
}

static int mxt_probe_bootloader(struct mxt_data *data, bool alt_address)
{
	struct device *dev = &data->client->dev;
	int error;
	u8 val;
	bool crc_failure;

	error = mxt_lookup_bootloader_address(data, alt_address);
	if (error)
		return error;

	error = mxt_bootloader_read(data, &val, 1);
	if (error)
		return error;

	/* Check app crc fail mode */
	crc_failure = (val & ~MXT_BOOT_STATUS_MASK) == MXT_APP_CRC_FAIL;

	TPD_DMESG("Detected bootloader, status:%02X%s\n",
			val, crc_failure ? ", APP_CRC_FAIL" : "");

	return 0;
}

static u8 mxt_get_bootloader_version(struct mxt_data *data, u8 val)
{
	struct device *dev = &data->client->dev;
	u8 buf[3];

	if (val & MXT_BOOT_EXTENDED_ID) {
		if (mxt_bootloader_read(data, &buf[0], 3) != 0) {
			TPD_DMESG("%s: i2c failure\n", __func__);
			return val;
		}

		TPD_DMESG("Bootloader ID:%d Version:%d\n", buf[1], buf[2]);

		return buf[0];
	} else {
		TPD_DMESG("Bootloader ID:%d\n", val & MXT_BOOT_ID_MASK);

		return val;
	}
}

static int mxt_check_bootloader(struct mxt_data *data, unsigned int state,
				bool wait)
{
	struct device *dev = &data->client->dev;
	u8 val;
	int ret;

recheck:
	if (wait) {
		/*
		 * In application update mode, the interrupt
		 * line signals state transitions. We must wait for the
		 * CHG assertion before reading the status byte.
		 * Once the status byte has been read, the line is deasserted.
		 */
		ret = mxt_wait_for_completion(data, &data->bl_completion,
						  MXT_FW_CHG_TIMEOUT);
		if (ret) {
			/*
			 * TODO: handle -ERESTARTSYS better by terminating
			 * fw update process before returning to userspace
			 * by writing length 0x000 to device (iff we are in
			 * WAITING_FRAME_DATA state).
			 */
			TPD_DMESG("Update wait error %d\n", ret);
			return ret;
		}
	}

	ret = mxt_bootloader_read(data, &val, 1);
	if (ret)
		return ret;

	if (state == MXT_WAITING_BOOTLOAD_CMD)
		val = mxt_get_bootloader_version(data, val);

	switch (state) {
	case MXT_WAITING_BOOTLOAD_CMD:
	case MXT_WAITING_FRAME_DATA:
	case MXT_APP_CRC_FAIL:
		val &= ~MXT_BOOT_STATUS_MASK;
		break;
	case MXT_FRAME_CRC_PASS:
		if (val == MXT_FRAME_CRC_CHECK) {
			goto recheck;
		} else if (val == MXT_FRAME_CRC_FAIL) {
			TPD_DMESG("Bootloader CRC fail\n");
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	if (val != state) {
		TPD_DMESG("Invalid bootloader state %02X != %02X\n",
			val, state);
		return -EINVAL;
	}

	return 0;
}

static int mxt_send_bootloader_cmd(struct mxt_data *data, bool unlock)
{
	int ret;
	u8 buf[2];

	if (unlock) {
		buf[0] = MXT_UNLOCK_CMD_LSB;
		buf[1] = MXT_UNLOCK_CMD_MSB;
	} else {
		buf[0] = 0x01;
		buf[1] = 0x01;
	}

	ret = mxt_bootloader_write(data, buf, 2);
	if (ret)
		return ret;

	return 0;
}

static struct mxt_object *
mxt_get_object(struct mxt_data *data, u8 type)
{
	struct mxt_object *object;
	int i;

	for (i = 0; i < data->info->object_num; i++) {
		object = data->object_table + i;
		if (object->type == type)
			return object;
	}

	TPD_DMESG("Invalid object type T%u\n", type);
	return NULL;
}

static void mxt_proc_t6_messages(struct mxt_data *data, u8 *msg)
{
	struct device *dev = &data->client->dev;
	u8 status = msg[1];
	u32 crc = msg[2] | (msg[3] << 8) | (msg[4] << 16);

	if (crc != data->config_crc) {
		data->config_crc = crc;
		TPD_DMESG("T6 Config Checksum: 0x%06X\n", crc);
	}

	complete(&data->crc_completion);

	/* Detect reset */
	if (status & MXT_T6_STATUS_RESET)
		complete(&data->reset_completion);

	/* Output debug if status has changed */
	if (status != data->t6_status)
		TPD_DMESG("T6 Status 0x%02X%s%s%s%s%s%s%s\n",
			status,
			status == 0 ? " OK" : "",
			status & MXT_T6_STATUS_RESET ? " RESET" : "",
			status & MXT_T6_STATUS_OFL ? " OFL" : "",
			status & MXT_T6_STATUS_SIGERR ? " SIGERR" : "",
			status & MXT_T6_STATUS_CAL ? " CAL" : "",
			status & MXT_T6_STATUS_CFGERR ? " CFGERR" : "",
			status & MXT_T6_STATUS_COMSERR ? " COMSERR" : "");

	/* Save current status */
	data->t6_status = status;
}

static void mxt_input_button(struct mxt_data *data, u8 *message)
{
	struct input_dev *input = data->input_dev;
	const struct mxt_platform_data *pdata = data->pdata;
	bool button;
	int i;

	/* Active-low switch */
	for (i = 0; i < pdata->t19_num_keys; i++) {
		if (pdata->t19_keymap[i] == KEY_RESERVED)
			continue;
		button = !(message[1] & (1 << i));
		input_report_key(input, pdata->t19_keymap[i], button);
	}
}

static void mxt_input_sync(struct mxt_data *data)
{
	if (data->input_dev) {
		input_mt_report_pointer_emulation(data->input_dev,
				data->pdata->t19_num_keys);
		input_sync(data->input_dev);
	}
}

static void mxt_proc_t9_message(struct mxt_data *data, u8 *message)
{
	struct device *dev = &data->client->dev;
	struct input_dev *input_dev = tpd->dev;
	int id;
	u8 status;
	int x;
	int y;
	int area;
	int amplitude;
	u8 vector;
	int tool;

	id = message[0] - data->T9_reportid_min;
	status = message[1];
	x = (message[2] << 4) | ((message[4] >> 4) & 0xf);
	y = (message[3] << 4) | ((message[4] & 0xf));

	/* Handle 10/12 bit switching */
	if (data->max_x < 1024)
		x >>= 2;
	if (data->max_y < 1024)
		y >>= 2;

	area = message[5];

	amplitude = message[6];
	vector = message[7];

	TPD_DEBUG(
		"[%u] %c%c%c%c%c%c%c%c x: %5u y: %5u area: %3u amp: %3u vector: %02X\n",
		id,
		(status & MXT_T9_DETECT) ? 'D' : '.',
		(status & MXT_T9_PRESS) ? 'P' : '.',
		(status & MXT_T9_RELEASE) ? 'R' : '.',
		(status & MXT_T9_MOVE) ? 'M' : '.',
		(status & MXT_T9_VECTOR) ? 'V' : '.',
		(status & MXT_T9_AMP) ? 'A' : '.',
		(status & MXT_T9_SUPPRESS) ? 'S' : '.',
		(status & MXT_T9_UNGRIP) ? 'U' : '.',
		x, y, area, amplitude, vector);

	input_mt_slot(input_dev, id);

	if (status & MXT_T9_DETECT) {
		/*
		 * Multiple bits may be set if the host is slow to read
		 * the status messages, indicating all the events that
		 * have happened.
		 */
		if (status & MXT_T9_RELEASE) {
			input_mt_report_slot_state(input_dev,
						   MT_TOOL_FINGER, 0);
			input_mt_sync(input_dev);
			mxt_input_sync(data);
		}

		/* A size of zero indicates touch is from a linked T47 Stylus */
		if (area == 0) {
			area = MXT_TOUCH_MAJOR_DEFAULT;
			tool = MT_TOOL_PEN;
		} else {
			tool = MT_TOOL_FINGER;
		}

		/* Touch active */
		input_mt_report_slot_state(input_dev, tool, 1);
		input_report_abs(input_dev, ABS_MT_POSITION_X, x);
		input_report_abs(input_dev, ABS_MT_POSITION_Y, y);
		input_report_abs(input_dev, ABS_MT_PRESSURE, amplitude);
		input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, area);
		input_report_abs(input_dev, ABS_MT_ORIENTATION, vector);
	} else {
		/* Touch no longer active, close out slot */
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 0);
	}

	data->update_input = true;
}

static void mxt_proc_t100_message(struct mxt_data *data, u8 *message)
{
	struct device *dev = &data->client->dev;
	struct input_dev *input_dev = data->input_dev;
	int id;
	u8 status;
	u8 type;
	int x;
	int y;
	int tool;
	u8 major = 0;
	u8 pressure = 0;
	u8 orientation = 0;
	bool active = false;
	bool hover = false;
	bool eraser = false;
	bool barrel = false;

	id = message[0] - data->T100_reportid_min - 2;

	/* ignore SCRSTATUS events */
	if (id < 0)
		return;

	status = message[1];
	x = (message[3] << 8) | message[2];
	y = (message[5] << 8) | message[4];

	if (status & MXT_T100_DETECT) {
		type = (status & MXT_T100_TYPE_MASK) >> 4;

		switch (type) {
		case MXT_T100_TYPE_HOVERING_FINGER:
			hover = true;
			/* fall through */
		case MXT_T100_TYPE_FINGER:
		case MXT_T100_TYPE_GLOVE:
			active = true;
			tool = MT_TOOL_FINGER;

			if (data->t100_aux_area)
				major = message[data->t100_aux_area];
			if (data->t100_aux_ampl)
				pressure = message[data->t100_aux_ampl];
			if (data->t100_aux_vect)
				orientation = message[data->t100_aux_vect];

			break;

		case MXT_T100_TYPE_PASSIVE_STYLUS:
			active = true;
			tool = MT_TOOL_PEN;

			/* Passive stylus is reported with size zero so
			 * hardcode */
			major = MXT_TOUCH_MAJOR_DEFAULT;

			if (data->t100_aux_ampl)
				pressure = message[data->t100_aux_ampl];

			break;

		case MXT_T100_TYPE_ACTIVE_STYLUS:
			/* stylus in range, but position unavailable */
			if (!(message[6] & MXT_T107_STYLUS_HOVER))
				break;

			active = true;
			tool = MT_TOOL_PEN;
			major = MXT_TOUCH_MAJOR_DEFAULT;
			eraser = message[6] & MXT_T107_STYLUS_BUTTON0;
			barrel = message[6] & MXT_T107_STYLUS_BUTTON1;

			if (!(message[6] & MXT_T107_STYLUS_TIPSWITCH))
				hover = true;
			else if (data->stylus_aux_pressure)
				pressure = message[data->stylus_aux_pressure];

			break;

		case MXT_T100_TYPE_LARGE_TOUCH:
			/* Ignore suppressed touch */
			break;

		default:
			TPD_DMESG("Unexpected T100 type\n");
			return;
		}
	}

	if (hover) {
		pressure = 0;
		major = 0;
	} else if (active) {
		/*
		 * Values reported should be non-zero if tool is touching the
		 * device
		 */
		if (pressure == 0)
			pressure = MXT_PRESSURE_DEFAULT;

		if (major == 0)
			major = MXT_TOUCH_MAJOR_DEFAULT;
	}

	input_mt_slot(input_dev, id);

	if (active) {
		TPD_DMESG("[%u] type:%u x:%u y:%u a:%02X p:%02X v:%02X\n",
			id, type, x, y, major, pressure, orientation);

		input_mt_report_slot_state(input_dev, tool, 1);
		input_report_abs(input_dev, ABS_MT_POSITION_X, x);
		input_report_abs(input_dev, ABS_MT_POSITION_Y, y);
		input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, major);
		input_report_abs(input_dev, ABS_MT_PRESSURE, pressure);
		input_report_abs(input_dev, ABS_MT_ORIENTATION, orientation);

		input_report_key(input_dev, BTN_STYLUS, eraser);
		input_report_key(input_dev, BTN_STYLUS2, barrel);
	} else {
		TPD_DMESG("[%u] release\n", id);

		/* close out slot */
		input_mt_report_slot_state(input_dev, 0, 0);
	}

	data->update_input = true;
}

static void mxt_proc_t15_messages(struct mxt_data *data, u8 *msg)
{
	struct input_dev *input_dev = data->input_dev;
	struct device *dev = &data->client->dev;
	int key;
	bool curr_state, new_state;
	bool sync = false;
	unsigned long keystates = le32_to_cpu(msg[2]);

	for (key = 0; key < data->pdata->t15_num_keys; key++) {
		curr_state = test_bit(key, &data->t15_keystatus);
		new_state = test_bit(key, &keystates);

		if (!curr_state && new_state) {
			TPD_DMESG("T15 key press: %u\n", key);
			__set_bit(key, &data->t15_keystatus);
			input_event(input_dev, EV_KEY,
					data->pdata->t15_keymap[key], 1);
			sync = true;
		} else if (curr_state && !new_state) {
			TPD_DMESG("T15 key release: %u\n", key);
			__clear_bit(key, &data->t15_keystatus);
			input_event(input_dev, EV_KEY,
					data->pdata->t15_keymap[key], 0);
			sync = true;
		}
	}

	if (sync)
		input_sync(input_dev);
}

static void mxt_proc_t42_messages(struct mxt_data *data, u8 *msg)
{
	struct device *dev = &data->client->dev;
	u8 status = msg[1];

	if (status & MXT_T42_MSG_TCHSUP)
		TPD_DMESG("T42 suppress\n");
	else
		TPD_DMESG("T42 normal\n");
}

static int mxt_proc_t48_messages(struct mxt_data *data, u8 *msg)
{
	struct device *dev = &data->client->dev;
	u8 status, state;

	status = msg[1];
	state  = msg[4];

	TPD_DMESG("T48 state %d status %02X %s%s%s%s%s\n", state, status,
		status & 0x01 ? "FREQCHG " : "",
		status & 0x02 ? "APXCHG " : "",
		status & 0x04 ? "ALGOERR " : "",
		status & 0x10 ? "STATCHG " : "",
		status & 0x20 ? "NLVLCHG " : "");

	return 0;
}

static void mxt_proc_t63_messages(struct mxt_data *data, u8 *msg)
{
	struct device *dev = &data->client->dev;
	struct input_dev *input_dev = data->input_dev;
	u8 id;
	u16 x, y;
	u8 pressure;

	/* stylus slots come after touch slots */
	id = data->num_touchids + (msg[0] - data->T63_reportid_min);

	if (id < 0 || id > (data->num_touchids + data->num_stylusids)) {
		TPD_DMESG("invalid stylus id %d, max slot is %d\n",
			id, data->num_stylusids);
		return;
	}

	x = msg[3] | (msg[4] << 8);
	y = msg[5] | (msg[6] << 8);
	pressure = msg[7] & MXT_T63_STYLUS_PRESSURE_MASK;

	dev_dbg(dev,
		"[%d] %c%c%c%c x: %d y: %d pressure: %d stylus:%c%c%c%c\n",
		id,
		msg[1] & MXT_T63_STYLUS_SUPPRESS	? 'S' : '.',
		msg[1] & MXT_T63_STYLUS_MOVE	 	? 'M' : '.',
		msg[1] & MXT_T63_STYLUS_RELEASE  	? 'R' : '.',
		msg[1] & MXT_T63_STYLUS_PRESS		? 'P' : '.',
		x, y, pressure,
		msg[2] & MXT_T63_STYLUS_BARREL   	? 'B' : '.',
		msg[2] & MXT_T63_STYLUS_ERASER   	? 'E' : '.',
		msg[2] & MXT_T63_STYLUS_TIP	  	? 'T' : '.',
		msg[2] & MXT_T63_STYLUS_DETECT   	? 'D' : '.');

	input_mt_slot(input_dev, id);

	if (msg[2] & MXT_T63_STYLUS_DETECT) {
		input_mt_report_slot_state(input_dev, MT_TOOL_PEN, 1);
		input_report_abs(input_dev, ABS_MT_POSITION_X, x);
		input_report_abs(input_dev, ABS_MT_POSITION_Y, y);
		input_report_abs(input_dev, ABS_MT_PRESSURE, pressure);
	} else {
		input_mt_report_slot_state(input_dev, MT_TOOL_PEN, 0);
	}

	input_report_key(input_dev, BTN_STYLUS,
			 (msg[2] & MXT_T63_STYLUS_ERASER));
	input_report_key(input_dev, BTN_STYLUS2,
			 (msg[2] & MXT_T63_STYLUS_BARREL));

	mxt_input_sync(data);
}

static int mxt_proc_message(struct mxt_data *data, u8 *message)
{
	u8 report_id = message[0];
	bool dump = data->debug_enabled;

	if (report_id == MXT_RPTID_NOMSG)
		return 0;

	if (report_id == data->T6_reportid) {
		mxt_proc_t6_messages(data, message);
	} else if (report_id >= data->T42_reportid_min
		   && report_id <= data->T42_reportid_max) {
		mxt_proc_t42_messages(data, message);
	} else if (report_id == data->T48_reportid) {
		mxt_proc_t48_messages(data, message);
	} else if (!data->input_dev || data->suspended) {
		/*
		 * Do not report events if input device is not
		 * yet registered or returning from suspend
		 */
		mxt_dump_message(data, message);
	} else if (report_id >= data->T9_reportid_min
		&& report_id <= data->T9_reportid_max) {
		mxt_proc_t9_message(data, message);
	} else if (report_id >= data->T100_reportid_min
		&& report_id <= data->T100_reportid_max) {
		mxt_proc_t100_message(data, message);
	} else if (report_id == data->T19_reportid) {
		mxt_input_button(data, message);
		data->update_input = true;
	} else if (report_id >= data->T63_reportid_min
		   && report_id <= data->T63_reportid_max) {
		mxt_proc_t63_messages(data, message);
	} else if (report_id >= data->T15_reportid_min
		   && report_id <= data->T15_reportid_max) {
		mxt_proc_t15_messages(data, message);
	} else {
		dump = true;
	}

	if (dump)
		mxt_dump_message(data, message);

	if (data->debug_v2_enabled)
		mxt_debug_msg_add(data, message);

	return 1;
}

static int mxt_read_and_process_messages(struct mxt_data *data, u8 count)
{
	struct device *dev = &data->client->dev;
	int ret;
	int i;
	u8 num_valid = 0;

	TPD_DEBUG("[%s] %d, %d\n", __FUNCTION__, count, data->max_reportid);
	/* Safety check for msg_buf */
	if (count > data->max_reportid)
		return -EINVAL;

	/* Process remaining messages if necessary */
	ret = read_mem(data->client, data->T5_address,
				data->T5_msg_size * count, data->msg_buf);
	if (ret) {
		TPD_DMESG("Failed to read %u messages (%d)\n", count, ret);
		return ret;
	}

	for (i = 0;  i < count; i++) {
		ret = mxt_proc_message(data,
			data->msg_buf + data->T5_msg_size * i);

		if (ret == 1)
			num_valid++;
	}

	/* return number of messages read */
	return num_valid;
}

static irqreturn_t mxt_process_messages_t44(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	int ret;
	u8 count, num_left;

	/* Read T44 and T5 together */
	ret = read_mem(data->client, data->T44_address,
		data->T5_msg_size + 1, data->msg_buf);
	if (ret) {
		TPD_DMESG("Failed to read T44 and T5 (%d)\n", ret);
		return IRQ_NONE;
	}

	count = data->msg_buf[0];

	if (count == 0) {
		/*
		 * This condition is caused by the CHG line being configured
		 * in Mode 0. It results in unnecessary I2C operations but it
		 * is benign.
		 */
		TPD_DMESG("Interrupt triggered but zero messages\n");
		return IRQ_NONE;
	} else if (count > data->max_reportid) {
		TPD_DMESG("T44 count %d exceeded max report id\n", count);
		count = data->max_reportid;
	}

	/* Process first message */
	ret = mxt_proc_message(data, data->msg_buf + 1);
	if (ret < 0) {
		TPD_DMESG("Unexpected invalid message\n");
		return IRQ_NONE;
	}

	num_left = count - 1;

	/* Process remaining messages if necessary */
	if (num_left) {
		ret = mxt_read_and_process_messages(data, num_left);
		if (ret < 0)
			goto end;
		else if (ret != num_left)
			TPD_DMESG("Unexpected invalid message\n");
	}

end:
	if (data->update_input) {
		mxt_input_sync(data);
		data->update_input = false;
	}

	return IRQ_HANDLED;
}

static int mxt_process_messages_until_invalid(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	int count, read;
	u8 tries = 2;

	count = data->max_reportid;

	/* Read messages until we force an invalid */
	do {
		read = mxt_read_and_process_messages(data, count);
		if (read < count)
			return 0;
	} while (--tries);

	if (data->update_input) {
		mxt_input_sync(data);
		data->update_input = false;
	}

	TPD_DMESG("CHG pin isn't cleared\n");
	return -EBUSY;
}

static irqreturn_t mxt_process_messages(struct mxt_data *data)
{
	int total_handled, num_handled;
	u8 count = data->last_message_count;

	if (count < 1 || count > data->max_reportid)
		count = 1;

	/* include final invalid message */
	total_handled = mxt_read_and_process_messages(data, count + 1);
	if (total_handled < 0)
		return IRQ_NONE;
	/* if there were invalid messages, then we are done */
	else if (total_handled <= count)
		goto update_count;

	/* keep reading two msgs until one is invalid or reportid limit */
	do {
		num_handled = mxt_read_and_process_messages(data, 2);
		if (num_handled < 0)
			return IRQ_NONE;

		total_handled += num_handled;

		if (num_handled < 2)
			break;
	} while (total_handled < data->num_touchids);

update_count:
	data->last_message_count = total_handled;

	if (data->update_input) {
		mxt_input_sync(data);
		data->update_input = false;
	}

	return IRQ_HANDLED;
}

static irqreturn_t mxt_interrupt(int irq, void *dev_id)
{
	struct mxt_data *data = dev_id;

	if (data->in_bootloader) {
		/* bootloader state transition completion */
		complete(&data->bl_completion);
		return IRQ_HANDLED;
	}

	if (!data->object_table)
		return IRQ_HANDLED;

	if (data->T44_address) {
		return mxt_process_messages_t44(data);
	} else {
		return mxt_process_messages(data);
	}
}

static int mxt_t6_command(struct mxt_data *data, u16 cmd_offset,
			  u8 value, bool wait)
{
	u16 reg;
	u8 command_register;
	int timeout_counter = 0;
	int ret;

	reg = data->T6_address + cmd_offset;

	ret = write_mem(data->client, reg, 1, &value);
	if (ret)
		return ret;

	if (!wait)
		return 0;

	do {
		msleep(20);
		ret = read_mem(data->client, reg, 1, &command_register);
		if (ret)
			return ret;
	} while (command_register != 0 && timeout_counter++ <= 100);

	TPD_DEBUG("mxt_t6_command: read back done(%d,%x)\n", ret, command_register);

	if (timeout_counter > 100) {
		TPD_DMESG("Command failed!\n");
		return -EIO;
	}

	return 0;
}

static int mxt_soft_reset(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	int ret = 0;

	TPD_DMESG("Resetting device\n");

	mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);

	INIT_COMPLETION(data->reset_completion);

	ret = mxt_t6_command(data, MXT_COMMAND_RESET, MXT_RESET_VALUE, false);
	if (ret)
		return ret;

	/* Ignore CHG line for 100ms after reset */
	msleep(100);

	mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);

	ret = mxt_wait_for_completion(data, &data->reset_completion,
					  MXT_RESET_TIMEOUT);
	if (ret) {
		TPD_DMESG("mxt_soft_reset:RESET timeout\n");
		return ret;
	}

	return 0;
}

static void mxt_update_crc(struct mxt_data *data, u8 cmd, u8 value)
{
	/*
	 * On failure, CRC is set to 0 and config will always be
	 * downloaded.
	 */
	data->config_crc = 0;
	INIT_COMPLETION(data->crc_completion);

	mxt_t6_command(data, cmd, value, true);

	/*
	 * Wait for crc message. On failure, CRC is set to 0 and config will
	 * always be downloaded.
	 */
	mxt_wait_for_completion(data, &data->crc_completion, MXT_CRC_TIMEOUT);
}

static void mxt_calc_crc24(u32 *crc, u8 firstbyte, u8 secondbyte)
{
	static const unsigned int crcpoly = 0x80001B;
	u32 result;
	u32 data_word;

	data_word = (secondbyte << 8) | firstbyte;
	result = ((*crc << 1) ^ data_word);

	if (result & 0x1000000)
		result ^= crcpoly;

	*crc = result;
}

static u32 mxt_calculate_crc(u8 *base, off_t start_off, off_t end_off)
{
	u32 crc = 0;
	u8 *ptr = base + start_off;
	u8 *last_val = base + end_off - 1;

	if (end_off < start_off)
		return -EINVAL;

	while (ptr < last_val) {
		mxt_calc_crc24(&crc, *ptr, *(ptr + 1));
		ptr += 2;
	}

	/* if len is odd, fill the last byte with 0 */
	if (ptr == last_val)
		mxt_calc_crc24(&crc, *ptr, 0);

	/* Mask to 24-bit */
	crc &= 0x00FFFFFF;

	return crc;
}

static int mxt_check_retrigen(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	int error;
	int val;

	if (data->pdata->irqflags & IRQF_TRIGGER_LOW)
		return 0;

	if (data->T18_address) {
		error = read_mem(client,
					   data->T18_address + MXT_COMMS_CTRL,
					   1, &val);
		if (error)
			return error;

		if (val & MXT_COMMS_RETRIGEN)
			return 0;
	}

	TPD_DMESG("Enabling RETRIGEN workaround\n");
	data->use_retrigen_workaround = true;
	return 0;
}

static int mxt_prepare_cfg_mem(struct mxt_data *data,
				   const struct firmware *cfg,
				   unsigned int data_pos,
				   unsigned int cfg_start_ofs,
				   u8 *config_mem,
				   size_t config_mem_size)
{
	struct device *dev = &data->client->dev;
	struct mxt_object *object;
	unsigned int type, instance, size, byte_offset;
	int offset;
	int ret;
	int i;
	u16 reg;
	u8 val;

	while (data_pos < cfg->size) {
		/* Read type, instance, length */
		ret = sscanf(cfg->data + data_pos, "%x %x %x%n",
				 &type, &instance, &size, &offset);
		if (ret == 0) {
			/* EOF */
			break;
		} else if (ret != 3) {
			TPD_DMESG("Bad format: failed to parse object\n");
			return -EINVAL;
		}
		data_pos += offset;

		//TPD_DMESG("type=%d, instance=%d, size=%d\n", type, instance, size);
		object = mxt_get_object(data, type);
		if (!object) {
			/* Skip object */
			for (i = 0; i < size; i++) {
				ret = sscanf(cfg->data + data_pos, "%hhx%n",
						 &val, &offset);
				if (ret != 1) {
					TPD_DMESG("Bad format in T%d at %d\n",
						type, i);
					return -EINVAL;
				}
				data_pos += offset;
			}
			continue;
		}

		//TPD_DMESG("type=%d, instance=%d, size=%d(%x)\n", type, instance, size, mxt_obj_size(object));
		if (size > mxt_obj_size(object)) {
			/*
			 * Either we are in fallback mode due to wrong
			 * config or config from a later fw version,
			 * or the file is corrupt or hand-edited.
			 */
			TPD_DMESG("Discarding %zu byte(s) in T%u\n",
				 size - mxt_obj_size(object), type);
		} else if (mxt_obj_size(object) > size) {
			/*
			 * If firmware is upgraded, new bytes may be added to
			 * end of objects. It is generally forward compatible
			 * to zero these bytes - previous behaviour will be
			 * retained. However this does invalidate the CRC and
			 * will force fallback mode until the configuration is
			 * updated. We warn here but do nothing else - the
			 * malloc has zeroed the entire configuration.
			 */
			TPD_DMESG("Zeroing %zu byte(s) in T%d\n",
				 mxt_obj_size(object) - size, type);
		}

		if (instance >= mxt_obj_instances(object)) {
			TPD_DMESG("Object instances exceeded!\n");
			return -EINVAL;
		}

		reg = object->start_address + mxt_obj_size(object) * instance;

		for (i = 0; i < size; i++) {
			ret = sscanf(cfg->data + data_pos, "%hhx%n",
					 &val,
					 &offset);
			if (ret != 1) {
				TPD_DMESG("Bad format in T%d at %d\n",
					type, i);
				return -EINVAL;
			}
			data_pos += offset;

			if (i > mxt_obj_size(object))
				continue;

			byte_offset = reg + i - cfg_start_ofs;

			if (byte_offset >= 0 && byte_offset < config_mem_size) {
				*(config_mem + byte_offset) = val;
			} else {
				TPD_DMESG("Bad object: reg:%d, T%d, ofs=%d\n",
					reg, object->type, byte_offset);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int mxt_upload_cfg_mem(struct mxt_data *data, unsigned int cfg_start,
				  u8 *config_mem, size_t config_mem_size)
{
	unsigned int byte_offset = 0;
	int error;

	TPD_DMESG("%s\n", __FUNCTION__);
	/* Write configuration as blocks */
	while (byte_offset < config_mem_size) {
		unsigned int size = config_mem_size - byte_offset;

		TPD_DMESG("%s: ==>size=%d(%d)\n", __FUNCTION__, size, config_mem_size);
		if (size > MXT_MAX_BLOCK_WRITE)
			size = MXT_MAX_BLOCK_WRITE;

		error = write_mem(data->client,
					cfg_start + byte_offset,
					size, config_mem + byte_offset);
		if (error) {
			TPD_DMESG(
				"Config write error, ret=%d\n", error);
			return error;
		}

		byte_offset += size;
	}

	return 0;
}

static int mxt_init_t7_power_cfg(struct mxt_data *data);

/*
 * mxt_update_cfg - download configuration to chip
 *
 * Atmel Raw Config File Format
 *
 * The first four lines of the raw config file contain:
 *  1) Version
 *  2) Chip ID Information (first 7 bytes of device memory)
 *  3) Chip Information Block 24-bit CRC Checksum
 *  4) Chip Configuration 24-bit CRC Checksum
 *
 * The rest of the file consists of one line per object instance:
 *   <TYPE> <INSTANCE> <SIZE> <CONTENTS>
 *
 *   <TYPE> - 2-byte object type as hex
 *   <INSTANCE> - 2-byte object instance number as hex
 *   <SIZE> - 2-byte object size as hex
 *   <CONTENTS> - array of <SIZE> 1-byte hex values
 */
static int mxt_update_cfg(struct mxt_data *data, const struct firmware *cfg)
{
	struct device *dev = &data->client->dev;
	struct mxt_info cfg_info;
	struct mxt_object *object;
	int ret;
	int offset;
	int data_pos;
	int byte_offset;
	int i;
	int cfg_start_ofs;
	u32 info_crc, config_crc, calculated_crc;
	u8 *config_mem;
	size_t config_mem_size;
	unsigned int type, instance, size;
	u8 val;
	u16 reg;

	mxt_update_crc(data, MXT_COMMAND_REPORTALL, 1);

	if (strncmp(cfg->data, MXT_CFG_MAGIC, strlen(MXT_CFG_MAGIC))) {
		TPD_DMESG("Unrecognised config file\n");
		ret = -EINVAL;
		goto release;
	}

	data_pos = strlen(MXT_CFG_MAGIC);

	/* Load information block and check */
	for (i = 0; i < sizeof(struct mxt_info); i++) {
		ret = sscanf(cfg->data + data_pos, "%hhx%n",
				 (unsigned char *)&cfg_info + i,
				 &offset);
		if (ret != 1) {
			TPD_DMESG("Bad format\n");
			ret = -EINVAL;
			goto release;
		}

		data_pos += offset;
	}

	if (cfg_info.family_id != data->info->family_id) {
		TPD_DMESG("Family ID mismatch!\n");
		ret = -EINVAL;
		goto release;
	}

	if (cfg_info.variant_id != data->info->variant_id) {
		TPD_DMESG("Variant ID mismatch!\n");
		ret = -EINVAL;
		goto release;
	}

	/* Read CRCs */
	ret = sscanf(cfg->data + data_pos, "%x%n", &info_crc, &offset);
	if (ret != 1) {
		TPD_DMESG("Bad format: failed to parse Info CRC\n");
		ret = -EINVAL;
		goto release;
	}
	data_pos += offset;

	ret = sscanf(cfg->data + data_pos, "%x%n", &config_crc, &offset);
	if (ret != 1) {
		TPD_DMESG("Bad format: failed to parse Config CRC\n");
		ret = -EINVAL;
		goto release;
	}
	data_pos += offset;

	TPD_DMESG("family_id=0x%x, variant_id=0x%x, info_crc=0x%x, config_crc=0x%x\n", cfg_info.family_id, cfg_info.variant_id, info_crc, config_crc);

	/*
	 * The Info Block CRC is calculated over mxt_info and the object
	 * table. If it does not match then we are trying to load the
	 * configuration from a different chip or firmware version, so
	 * the configuration CRC is invalid anyway.
	 */
	if (info_crc == data->info_crc) {
		if (config_crc == 0 || data->config_crc == 0) {
			TPD_DMESG("CRC zero, attempting to apply config\n");
		} else if (config_crc == data->config_crc) {
			TPD_DEBUG("Config CRC 0x%06X: OK\n",
				 data->config_crc);
			ret = 0;
			goto release;
		} else {
			TPD_DMESG("Config CRC 0x%06X: does not match file 0x%06X\n",
				 data->config_crc, config_crc);
		}
	} else {
		dev_warn(dev,
			 "Warning: Info CRC error - device=0x%06X file=0x%06X\n",
			 data->info_crc, info_crc);
	}

	/* Malloc memory to store configuration */
	cfg_start_ofs = MXT_OBJECT_START +
			data->info->object_num * sizeof(struct mxt_object) +
			MXT_INFO_CHECKSUM_SIZE;
	config_mem_size = data->mem_size - cfg_start_ofs;
	config_mem = kzalloc(config_mem_size, GFP_KERNEL);
	if (!config_mem) {
		TPD_DMESG("Failed to allocate memory\n");
		ret = -ENOMEM;
		goto release;
	}

	while (data_pos < cfg->size) {
		/* Read type, instance, length */
		ret = sscanf(cfg->data + data_pos, "%x %x %x%n",
				 &type, &instance, &size, &offset);
		if (ret == 0) {
			/* EOF */
			break;
		} else if (ret != 3) {
			TPD_DMESG("Bad format: failed to parse object\n");
			ret = -EINVAL;
			goto release_mem;
		}
		data_pos += offset;

		object = mxt_get_object(data, type);
		if (!object) {
			/* Skip object */
			for (i = 0; i < size; i++) {
				ret = sscanf(cfg->data + data_pos, "%hhx%n",
						 &val,
						 &offset);
				data_pos += offset;
			}
			continue;
		}

		if (size > mxt_obj_size(object)) {
			/*
			 * Either we are in fallback mode due to wrong
			 * config or config from a later fw version,
			 * or the file is corrupt or hand-edited.
			 */
			TPD_DEBUG("Discarding %zu byte(s) in T%u\n",
				 size - mxt_obj_size(object), type);
		} else if (mxt_obj_size(object) > size) {
			/*
			 * If firmware is upgraded, new bytes may be added to
			 * end of objects. It is generally forward compatible
			 * to zero these bytes - previous behaviour will be
			 * retained. However this does invalidate the CRC and
			 * will force fallback mode until the configuration is
			 * updated. We warn here but do nothing else - the
			 * malloc has zeroed the entire configuration.
			 */
			TPD_DEBUG("Zeroing %zu byte(s) in T%d\n",
				 mxt_obj_size(object) - size, type);
		}

		if (instance >= mxt_obj_instances(object)) {
			TPD_DMESG("Object instances exceeded!\n");
			ret = -EINVAL;
			goto release_mem;
		}

		reg = object->start_address + mxt_obj_size(object) * instance;

		for (i = 0; i < size; i++) {
			ret = sscanf(cfg->data + data_pos, "%hhx%n",
					 &val,
					 &offset);
			if (ret != 1) {
				TPD_DMESG("Bad format in T%d\n", type);
				ret = -EINVAL;
				goto release_mem;
			}
			data_pos += offset;

			if (i > mxt_obj_size(object))
				continue;

			byte_offset = reg + i - cfg_start_ofs;

			if ((byte_offset >= 0)
				&& (byte_offset <= config_mem_size)) {
				*(config_mem + byte_offset) = val;
			} else {
				TPD_DMESG("Bad object: reg:%d, T%d, ofs=%d\n",
					reg, object->type, byte_offset);
				ret = -EINVAL;
				goto release_mem;
			}
		}
	}

	/* Calculate crc of the received configs (not the raw config file) */
	if (data->T7_address < cfg_start_ofs) {
		TPD_DMESG("Bad T7 address, T7addr = %x, config offset %x\n",
			data->T7_address, cfg_start_ofs);
		ret = 0;
		goto release_mem;
	}

	calculated_crc = mxt_calculate_crc(config_mem,
					   data->T7_address - cfg_start_ofs,
					   config_mem_size);

	if (config_crc > 0 && (config_crc != calculated_crc))
		TPD_DEBUG("Config CRC error, calculated=%06X, file=%06X\n",
			 calculated_crc, config_crc);

	/* Write configuration as blocks */
	byte_offset = 0;
	while (byte_offset < config_mem_size) {
		size = config_mem_size - byte_offset;

		if (size > MXT_MAX_BLOCK_WRITE)
			size = MXT_MAX_BLOCK_WRITE;

		ret = write_mem(data->client,
					  cfg_start_ofs + byte_offset,
					  size, config_mem + byte_offset);
		if (ret != 0) {
			TPD_DMESG("Config write error, ret=%d\n", ret);
			goto release_mem;
		}

		byte_offset += size;
	}

	mxt_update_crc(data, MXT_COMMAND_BACKUPNV, MXT_BACKUP_VALUE);

	ret = mxt_check_retrigen(data);
	if (ret)
		goto release_mem;

	ret = mxt_soft_reset(data);
	if (ret)
		goto release_mem;

	TPD_DMESG("Config successfully updated\n");

	/* T7 config may have changed */
	mxt_init_t7_power_cfg(data);

release_mem:
	kfree(config_mem);
release:
	//release_firmware(cfg);
	return ret;
}

static int mxt_acquire_irq(struct mxt_data *data)
{
	int error;

	mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);

	if (data->use_retrigen_workaround) {
		error = mxt_process_messages_until_invalid(data);
		if (error)
			return error;
	}

	return 0;
}

static void mxt_free_input_device(struct mxt_data *data)
{
	if (data->input_dev) {
		input_unregister_device(data->input_dev);
		data->input_dev = NULL;
	}
}

static void mxt_free_object_table(struct mxt_data *data)
{
	mxt_debug_msg_remove(data);

	data->object_table = NULL;
	data->info = NULL;
	kfree(data->raw_info_block);
	data->raw_info_block = NULL;
	kfree(data->msg_buf);
	data->msg_buf = NULL;
	data->T5_address = 0;
	data->T5_msg_size = 0;
	data->T6_reportid = 0;
	data->T7_address = 0;
	data->T9_reportid_min = 0;
	data->T9_reportid_max = 0;
	data->T15_reportid_min = 0;
	data->T15_reportid_max = 0;
	data->T18_address = 0;
	data->T19_reportid = 0;
	data->T42_reportid_min = 0;
	data->T42_reportid_max = 0;
	data->T44_address = 0;
	data->T48_reportid = 0;
	data->T63_reportid_min = 0;
	data->T63_reportid_max = 0;
	data->T100_reportid_min = 0;
	data->T100_reportid_max = 0;
	data->max_reportid = 0;
}

static int mxt_parse_object_table(struct mxt_data *data,
				  struct mxt_object *object_table)
{
	struct i2c_client *client = data->client;
	int i;
	u8 reportid;
	u16 end_address;

	/* Valid Report IDs start counting from 1 */
	reportid = 1;
	data->mem_size = 0;
	for (i = 0; i < data->info->object_num; i++) {
		struct mxt_object *object = object_table + i;
		u8 min_id, max_id;

		le16_to_cpus(&object->start_address);

		if (object->num_report_ids) {
			min_id = reportid;
			reportid += object->num_report_ids *
					mxt_obj_instances(object);
			max_id = reportid - 1;
		} else {
			min_id = 0;
			max_id = 0;
		}

		//TPD_DEBUG(
		//	"T%u Start:0x%x Size:%zu Instances:%zu Report IDs:%u-%u\n",
		//	object->type, object->start_address,
		//	mxt_obj_size(object), mxt_obj_instances(object),
		//	min_id, max_id);

		switch (object->type) {
		case MXT_GEN_MESSAGE_T5:
			if (data->info->family_id == 0x80 &&
				data->info->version < 0x20) {
				/*
				 * On mXT224 firmware versions prior to V2.0
				 * read and discard unused CRC byte otherwise
				 * DMA reads are misaligned.
				 */
				data->T5_msg_size = mxt_obj_size(object);
			} else {
				/* CRC not enabled, so skip last byte */
				data->T5_msg_size = mxt_obj_size(object) - 1;
			}
			data->T5_address = object->start_address;
			break;
		case MXT_GEN_COMMAND_T6:
			data->T6_reportid = min_id;
			data->T6_address = object->start_address;
			break;
		case MXT_GEN_POWER_T7:
			data->T7_address = object->start_address;
			break;
		case MXT_TOUCH_MULTI_T9:
			/* Only handle messages from first T9 instance */
			data->T9_reportid_min = min_id;
			data->T9_reportid_max = min_id +
						object->num_report_ids - 1;
			data->num_touchids = object->num_report_ids;
			data->T9_address = object->start_address;
			break;
		case MXT_TOUCH_KEYARRAY_T15:
			data->T15_reportid_min = min_id;
			data->T15_reportid_max = max_id;
			break;
		case MXT_SPT_COMMSCONFIG_T18:
			data->T18_address = object->start_address;
			break;
		case MXT_PROCI_TOUCHSUPPRESSION_T42:
			data->T42_reportid_min = min_id;
			data->T42_reportid_max = max_id;
			break;
		case MXT_SPT_MESSAGECOUNT_T44:
			data->T44_address = object->start_address;
			break;
		case MXT_SPT_GPIOPWM_T19:
			data->T19_reportid = min_id;
			break;
		case MXT_PROCG_NOISESUPPRESSION_T48:
			data->T48_reportid = min_id;
			break;
		case MXT_PROCI_ACTIVE_STYLUS_T63:
			/* Only handle messages from first T63 instance */
			data->T63_reportid_min = min_id;
			data->T63_reportid_max = min_id;
			data->num_stylusids = 1;
			break;
		case MXT_TOUCH_MULTITOUCHSCREEN_T100:
			data->T100_reportid_min = min_id;
			data->T100_reportid_max = max_id;
			/* first two report IDs reserved */
			data->num_touchids = object->num_report_ids - 2;
			break;
		case MXT_PROCI_ACTIVESTYLUS_T107:
			data->T107_address = object->start_address;
			break;
		}

		end_address = object->start_address
			+ mxt_obj_size(object) * mxt_obj_instances(object) - 1;

		if (end_address >= data->mem_size)
			data->mem_size = end_address + 1;

	}

	/* Store maximum reportid */
	data->max_reportid = reportid;

	/* If T44 exists, T5 position has to be directly after */
	if (data->T44_address && (data->T5_address != data->T44_address + 1)) {
		TPD_DMESG("Invalid T44 position\n");
		return -EINVAL;
	}

	data->msg_buf = kcalloc(data->max_reportid,
				data->T5_msg_size, GFP_KERNEL);
	if (!data->msg_buf)
		return -ENOMEM;

	return 0;
}

static int mxt_read_info_block(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	int error;
	size_t size;
	void *id_buf, *buf;
	uint8_t num_objects;
	u32 calculated_crc;
	u8 *crc_ptr;

	/* If info block already allocated, free it */
	if (data->raw_info_block != NULL)
		mxt_free_object_table(data);

	/* Read 7-byte ID information block starting at address 0 */
	size = sizeof(struct mxt_info);
	id_buf = kzalloc(size, GFP_KERNEL);
	if (!id_buf)
		return -ENOMEM;

	error = read_mem(client, 0, size, id_buf);
	if (error) {
		kfree(id_buf);
		return error;
	}

	/* Resize buffer to give space for rest of info block */
	num_objects = ((struct mxt_info *)id_buf)->object_num;
	size += (num_objects * sizeof(struct mxt_object))
		+ MXT_INFO_CHECKSUM_SIZE;

	buf = krealloc(id_buf, size, GFP_KERNEL);
	if (!buf) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	/* Read rest of info block */
	error = read_mem(client, MXT_OBJECT_START,
				   size - MXT_OBJECT_START,
				   buf + MXT_OBJECT_START);
	if (error)
		goto err_free_mem;

	/* Extract & calculate checksum */
	crc_ptr = buf + size - MXT_INFO_CHECKSUM_SIZE;
	data->info_crc = crc_ptr[0] | (crc_ptr[1] << 8) | (crc_ptr[2] << 16);

	calculated_crc = mxt_calculate_crc(buf, 0,
					   size - MXT_INFO_CHECKSUM_SIZE);

	/*
	 * CRC mismatch can be caused by data corruption due to I2C comms
	 * issue or else device is not using Object Based Protocol (eg i2c-hid)
	 */
	if ((data->info_crc == 0) || (data->info_crc != calculated_crc)) {
		TPD_DMESG(
			"Info Block CRC error calculated=0x%06X read=0x%06X\n",
			calculated_crc, data->info_crc);
		error = -EIO;
		goto err_free_mem;
	}

	data->raw_info_block = buf;
	data->info = (struct mxt_info *)buf;

	TPD_DMESG(
		 "Family: %x Variant: %x Firmware V%u.%u.%02X Objects: %u\n",
		 data->info->family_id, data->info->variant_id,
		 data->info->version >> 4, data->info->version & 0xf,
		 data->info->build, data->info->object_num);

	/* Parse object table information */
	error = mxt_parse_object_table(data, buf + MXT_OBJECT_START);
	if (error) {
		TPD_DMESG("Error %d parsing object table\n", error);
		mxt_free_object_table(data);
		return error;
	}

	data->object_table = (struct mxt_object *)(buf + MXT_OBJECT_START);

	return 0;

err_free_mem:
	kfree(buf);
	return error;
}

static void mxt_regulator_enable(struct mxt_data *data)
{
	int error;

	gpio_set_value(data->pdata->gpio_reset, 0);

	error = regulator_enable(data->reg_vdd);
	if (error)
		return;

	error = regulator_enable(data->reg_avdd);
	if (error)
		return;

	msleep(MXT_REGULATOR_DELAY);
	gpio_set_value(data->pdata->gpio_reset, 1);
	msleep(MXT_CHG_DELAY);

retry_wait:
	INIT_COMPLETION(data->bl_completion);
	data->in_bootloader = true;
	error = mxt_wait_for_completion(data, &data->bl_completion,
					MXT_POWERON_DELAY);
	if (error == -EINTR)
		goto retry_wait;

	data->in_bootloader = false;
}

static void mxt_regulator_disable(struct mxt_data *data)
{
	regulator_disable(data->reg_vdd);
	regulator_disable(data->reg_avdd);
}

static int mxt_read_t9_resolution(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	int error;
	struct t9_range range;
	unsigned char orient;
	struct mxt_object *object;

	object = mxt_get_object(data, MXT_TOUCH_MULTI_T9);
	if (!object)
		return -EINVAL;

	error = read_mem(client,
				   object->start_address + MXT_T9_RANGE,
				   sizeof(range), &range);
	if (error)
		return error;

	le16_to_cpus(&range.x);
	le16_to_cpus(&range.y);

	error =  read_mem(client,
				object->start_address + MXT_T9_ORIENT,
				1, &orient);
	if (error)
		return error;

	/* Handle default values */
	if (range.x == 0)
		range.x = 1023;

	if (range.y == 0)
		range.y = 1023;

	if (orient & MXT_T9_ORIENT_SWITCH) {
		data->max_x = range.y;
		data->max_y = range.x;
	} else {
		data->max_x = range.x;
		data->max_y = range.y;
	}

	TPD_DMESG("Touchscreen size X%uY%u\n", data->max_x, data->max_y);

	return 0;
}

static int mxt_initialize_t9_input_device(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	const struct mxt_platform_data *pdata = data->pdata;
	struct input_dev *input_dev;
	int error;
	unsigned int num_mt_slots;
	unsigned int mt_flags = 0;
	int i;

	error = mxt_read_t9_resolution(data);
	if (error)
		TPD_DMESG("Failed to initialize T9 resolution\n");

	input_dev = tpd->dev;
	if (!input_dev)
		return -ENOMEM;

	input_dev->phys = data->phys;
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = dev;
	input_dev->open = mxt_input_open;
	input_dev->close = mxt_input_close;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);

	if (pdata->t19_num_keys) {
		__set_bit(INPUT_PROP_BUTTONPAD, input_dev->propbit);

		for (i = 0; i < pdata->t19_num_keys; i++)
			if (pdata->t19_keymap[i] != KEY_RESERVED)
				input_set_capability(input_dev, EV_KEY,
							 pdata->t19_keymap[i]);

		mt_flags |= INPUT_MT_POINTER;

		input_abs_set_res(input_dev, ABS_X, MXT_PIXELS_PER_MM);
		input_abs_set_res(input_dev, ABS_Y, MXT_PIXELS_PER_MM);
		input_abs_set_res(input_dev, ABS_MT_POSITION_X,
				  MXT_PIXELS_PER_MM);
		input_abs_set_res(input_dev, ABS_MT_POSITION_Y,
				  MXT_PIXELS_PER_MM);

	} else {
		mt_flags |= INPUT_MT_DIRECT;
	}

	/* For single touch */
	input_set_abs_params(input_dev, ABS_X,
				 0, data->max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_Y,
				 0, data->max_y, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE,
				 0, 255, 0, 0);

	/* For multi touch */
	num_mt_slots = data->num_touchids + data->num_stylusids;
	error = input_mt_init_slots(input_dev, num_mt_slots, mt_flags);
	if (error) {
		TPD_DMESG("Error %d initialising slots\n", error);
		goto err_free_mem;
	}

	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR,
				 0, MXT_MAX_AREA, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X,
				 0, data->max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
				 0, data->max_y, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_PRESSURE,
				 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_ORIENTATION,
				 0, 255, 0, 0);

	/* For T63 active stylus */
	if (data->T63_reportid_min) {
		input_set_capability(input_dev, EV_KEY, BTN_STYLUS);
		input_set_capability(input_dev, EV_KEY, BTN_STYLUS2);
		input_set_abs_params(input_dev, ABS_MT_TOOL_TYPE,
			0, MT_TOOL_MAX, 0, 0);
	}

	/* For T15 key array */
	if (data->T15_reportid_min) {
		data->t15_keystatus = 0;

		for (i = 0; i < data->pdata->t15_num_keys; i++)
			input_set_capability(input_dev, EV_KEY,
						 data->pdata->t15_keymap[i]);
	}

	input_set_drvdata(input_dev, data);

	mutex_init(&input_dev->mutex);
	data->input_dev = input_dev;

	return 0;

err_free_mem:
	return error;
}

static int mxt_read_t107_stylus_config(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	int error;
	struct mxt_object *object;
	u8 styaux;
	int aux;

	object = mxt_get_object(data, MXT_PROCI_ACTIVESTYLUS_T107);
	if (!object)
		return 0;

	error = read_mem(client,
				   object->start_address + MXT_T107_STYLUS_STYAUX,
				   1, &styaux);
	if (error)
		return error;

	/* map aux bits */
	aux = 7;

	if (styaux & MXT_T107_STYLUS_STYAUX_PRESSURE)
		data->stylus_aux_pressure = aux++;

	if (styaux & MXT_T107_STYLUS_STYAUX_PEAK)
		data->stylus_aux_peak = aux++;

	TPD_DMESG(
		"Enabling T107 active stylus, aux map pressure:%u peak:%u\n",
		data->stylus_aux_pressure, data->stylus_aux_peak);

	return 0;
}

static int mxt_read_t100_config(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	int error;
	struct mxt_object *object;
	u16 range_x, range_y;
	u8 cfg, tchaux;
	u8 aux;

	object = mxt_get_object(data, MXT_TOUCH_MULTITOUCHSCREEN_T100);
	if (!object)
		return -EINVAL;

	error = read_mem(client,
				   object->start_address + MXT_T100_XRANGE,
				   sizeof(range_x), &range_x);
	if (error)
		return error;

	le16_to_cpus(&range_x);

	error = read_mem(client,
				   object->start_address + MXT_T100_YRANGE,
				   sizeof(range_y), &range_y);
	if (error)
		return error;

	le16_to_cpus(&range_y);

	error =  read_mem(client,
				object->start_address + MXT_T100_CFG1,
				1, &cfg);
	if (error)
		return error;

	error =  read_mem(client,
				object->start_address + MXT_T100_TCHAUX,
				1, &tchaux);
	if (error)
		return error;

	/* Handle default values */
	if (range_x == 0)
		range_x = 1023;

	if (range_y == 0)
		range_y = 1023;

	if (cfg & MXT_T100_CFG_SWITCHXY) {
		data->max_x = range_y;
		data->max_y = range_x;
	} else {
		data->max_x = range_x;
		data->max_y = range_y;
	}

	/* allocate aux bytes */
	aux = 6;

	if (tchaux & MXT_T100_TCHAUX_VECT)
		data->t100_aux_vect = aux++;

	if (tchaux & MXT_T100_TCHAUX_AMPL)
		data->t100_aux_ampl = aux++;

	if (tchaux & MXT_T100_TCHAUX_AREA)
		data->t100_aux_area = aux++;

	TPD_DMESG(
		"T100 aux mappings vect:%u ampl:%u area:%u\n",
		data->t100_aux_vect, data->t100_aux_ampl, data->t100_aux_area);

	TPD_DMESG(
		 "T100 Touchscreen size X%uY%u\n", data->max_x, data->max_y);

	return 0;
}

static int mxt_initialize_t100_input_device(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	struct input_dev *input_dev;
	int error;

	error = mxt_read_t100_config(data);
	if (error) {
		TPD_DMESG("Failed to read T100 config\n");
		return error;
	}

	mxt_read_t107_stylus_config(data);

	input_dev = input_allocate_device();
	if (!data || !input_dev)
		return -ENOMEM;

	if (data->pdata->input_name)
		input_dev->name = data->pdata->input_name;
	else
		input_dev->name = "atmel_mxt_ts T100 touchscreen";

	input_dev->phys = data->phys;
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &data->client->dev;
	input_dev->open = mxt_input_open;
	input_dev->close = mxt_input_close;

	set_bit(EV_ABS, input_dev->evbit);
	input_set_capability(input_dev, EV_KEY, BTN_TOUCH);

	/* For single touch */
	input_set_abs_params(input_dev, ABS_X,
				 0, data->max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_Y,
				 0, data->max_y, 0, 0);

	if (data->t100_aux_ampl)
		input_set_abs_params(input_dev, ABS_PRESSURE,
					 0, 255, 0, 0);

	/* For multi touch */
	error = input_mt_init_slots(input_dev, data->num_touchids,
					INPUT_MT_DIRECT);
	if (error) {
		TPD_DMESG("Error %d initialising slots\n", error);
		goto err_free_mem;
	}

	input_set_abs_params(input_dev, ABS_MT_TOOL_TYPE, 0, MT_TOOL_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X,
				 0, data->max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
				 0, data->max_y, 0, 0);

	if (data->T107_address) {
		input_set_capability(input_dev, EV_KEY, BTN_STYLUS);
		input_set_capability(input_dev, EV_KEY, BTN_STYLUS2);
	}

	if (data->t100_aux_area)
		input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR,
					 0, MXT_MAX_AREA, 0, 0);

	if (data->t100_aux_ampl | data->stylus_aux_pressure)
		input_set_abs_params(input_dev, ABS_MT_PRESSURE,
					 0, 255, 0, 0);

	if (data->t100_aux_vect)
		input_set_abs_params(input_dev, ABS_MT_ORIENTATION,
					 0, 255, 0, 0);

	input_set_drvdata(input_dev, data);

	error = input_register_device(input_dev);
	if (error) {
		TPD_DMESG("Error %d registering input device\n", error);
		goto err_free_mem;
	}

	mutex_init(&input_dev->mutex);
	data->input_dev = input_dev;

	return 0;

err_free_mem:
	input_free_device(input_dev);
	return error;
}

int mxt_configure_objects(struct mxt_data *data, const struct firmware *cfg);

static void mxt_config_cb(const struct firmware *cfg, void *ctx)
{
	TPD_DMESG("%s:start\n", __FUNCTION__);
	//mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
	mxt_configure_objects(ctx, cfg);
	//mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
	release_firmware(cfg);
}

static int mxt_initialize(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	int recovery_attempts = 0;
	int error;

	while (1) {
		error = mxt_read_info_block(data);
		if (!error)
			break;

		/* Check bootloader state */
		error = mxt_probe_bootloader(data, false);
		if (error) {
			TPD_DMESG("Trying alternate bootloader address\n");
			error = mxt_probe_bootloader(data, true);
			if (error) {
				/* Chip is not in appmode or bootloader mode */
				return error;
			}
		}

		/* OK, we are in bootloader, see if we can recover */
		if (++recovery_attempts > 1) {
			TPD_DMESG("Could not recover from bootloader mode\n");
			/*
			 * We can reflash from this state, so do not
			 * abort initialization.
			 */
			data->in_bootloader = true;
			return 0;
		}

		/* Attempt to exit bootloader into app mode */
		mxt_send_bootloader_cmd(data, false);
		msleep(MXT_FW_RESET_TIME);
	}

	error = mxt_check_retrigen(data);
	if (error)
		goto err_free_object_table;

	error = mxt_acquire_irq(data);
	if (error)
		goto err_free_object_table;

	error = mxt_debug_msg_init(data);
	if (error)
		goto err_free_object_table;


	TPD_DEBUG("[mxt_initialize] :%s\n", data->cfg_name);

	if (data->cfg_name) {
		error = request_firmware_nowait(THIS_MODULE, true,
					data->cfg_name, &data->client->dev,
					GFP_KERNEL, data, mxt_config_cb);
		if (error) {
			TPD_DMESG("Failed to invoke firmware loader: %d\n",
				error);
			goto err_free_object_table;
		}
	} else {
		error = mxt_configure_objects(data, NULL);
		if (error)
			goto err_free_object_table;
	}

	TPD_DEBUG("[mxt_initialize] : END\n");

	return 0;

err_free_object_table:
	mxt_free_object_table(data);
	return error;
}

static int mxt_set_t7_power_cfg(struct mxt_data *data, u8 sleep)
{
	struct device *dev = &data->client->dev;
	int error;
	struct t7_config *new_config;
	struct t7_config deepsleep = { .active = 0, .idle = 0 };

	if (sleep == MXT_POWER_CFG_DEEPSLEEP)
		new_config = &deepsleep;
	else
		new_config = &data->t7_cfg;

	error = write_mem(data->client, data->T7_address,
				sizeof(data->t7_cfg), new_config);
	if (error)
		return error;

	TPD_DMESG("Set T7 ACTV:%d IDLE:%d\n",
		new_config->active, new_config->idle);

	return 0;
}

static int mxt_init_t7_power_cfg(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	int error;
	bool retry = false;

recheck:
	error = read_mem(data->client, data->T7_address,
				sizeof(data->t7_cfg), &data->t7_cfg);
	if (error)
		return error;

	if (data->t7_cfg.active == 0 || data->t7_cfg.idle == 0) {
		if (!retry) {
			TPD_DMESG("T7 cfg zero, resetting\n");
			mxt_soft_reset(data);
			retry = true;
			goto recheck;
		} else {
			TPD_DMESG("T7 cfg zero after reset, overriding\n");
			data->t7_cfg.active = 20;
			data->t7_cfg.idle = 100;
			return mxt_set_t7_power_cfg(data, MXT_POWER_CFG_RUN);
		}
	}

	TPD_DMESG("Initialized power cfg: ACTV %d, IDLE %d\n",
		data->t7_cfg.active, data->t7_cfg.idle);
	return 0;
}

int mxt_configure_objects(struct mxt_data *data,
				 const struct firmware *cfg)
{
	struct device *dev = &data->client->dev;
	int error;

	error = mxt_init_t7_power_cfg(data);
	if (error) {
		TPD_DMESG("Failed to initialize power cfg\n");
		goto err_free_object_table;
	}

	TPD_DEBUG("[mxt_configure_objects] ready to update cfg: 0x%x\n", (unsigned int)cfg);

	if (cfg) {
		error = mxt_update_cfg(data, cfg);
		if (error)
			TPD_DMESG("Error %d updating config\n", error);
	}

	if (data->T9_reportid_min) {
		error = mxt_initialize_t9_input_device(data);
		if (error)
			goto err_free_object_table;
	} else if (data->T100_reportid_min) {
		error = mxt_initialize_t100_input_device(data);
		if (error)
			goto err_free_object_table;
	} else {
		TPD_DMESG("No touch object detected\n");
	}

	return 0;

err_free_object_table:
	mxt_free_object_table(data);
	return error;
}

/* Configuration crc check sum is returned as hex xxxxxx */
static ssize_t mxt_config_csum_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%06x\n", data->config_crc);
}

/* Firmware Version is returned as Major.Minor.Build */
static ssize_t mxt_fw_version_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);

	if (!data->object_table)
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE, "%u.%u.%02X\n",
			 data->info->version >> 4, data->info->version & 0xf,
			 data->info->build);
}

/* Hardware Version is returned as FamilyID.VariantID */
static ssize_t mxt_hw_version_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);

	if (!data->object_table)
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE, "%u.%u\n",
			data->info->family_id, data->info->variant_id);
}

static ssize_t mxt_show_instance(char *buf, int count,
				 struct mxt_object *object, int instance,
				 const u8 *val)
{
	int i;

	if (mxt_obj_instances(object) > 1)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "Instance %u\n", instance);

	for (i = 0; i < mxt_obj_size(object); i++)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				"\t[%2u]: %02x (%d)\n", i, val[i], val[i]);
	count += scnprintf(buf + count, PAGE_SIZE - count, "\n");

	return count;
}

static ssize_t mxt_object_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct mxt_object *object;
	int count = 0;
	int i, j;
	int error;
	u8 *obuf;

	if (!data->object_table)
		return -EINVAL;

	/* Pre-allocate buffer large enough to hold max sized object. */
	obuf = kmalloc(256, GFP_KERNEL);
	if (!obuf)
		return -ENOMEM;

	error = 0;
	for (i = 0; i < data->info->object_num; i++) {
		object = data->object_table + i;

		if (!mxt_object_readable(object->type))
			continue;

		count += scnprintf(buf + count, PAGE_SIZE - count,
				"T%u:\n", object->type);

		for (j = 0; j < mxt_obj_instances(object); j++) {
			u16 size = mxt_obj_size(object);
			u16 addr = object->start_address + j * size;

			error = read_mem(data->client, addr, size, obuf);
			if (error)
				goto done;

			count = mxt_show_instance(buf, count, object, j, obuf);
		}
	}

done:
	kfree(obuf);
	return error ?: count;
}

static int mxt_check_firmware_format(struct device *dev,
					 const struct firmware *fw)
{
	unsigned int pos = 0;
	char c;

	while (pos < fw->size) {
		c = *(fw->data + pos);

		if (c < '0' || (c > '9' && c < 'A') || c > 'F')
			return 0;

		pos++;
	}

	/*
	 * To convert file try:
	 * xxd -r -p mXTXXX__APP_VX-X-XX.enc > maxtouch.fw
	 */
	TPD_DMESG("Aborting: firmware file must be in binary format\n");

	return -EINVAL;
}

static int mxt_load_fw(struct device *dev)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	const struct firmware *fw = NULL;
	unsigned int frame_size;
	unsigned int pos = 0;
	unsigned int retry = 0;
	unsigned int frame = 0;
	int ret;

	ret = request_firmware(&fw, data->fw_name, dev);
	if (ret) {
		TPD_DMESG("Unable to open firmware %s\n", data->fw_name);
		return ret;
	}

	/* Check for incorrect enc file */
	ret = mxt_check_firmware_format(dev, fw);
	if (ret)
		goto release_firmware;

	if (data->suspended) {
		if (data->use_regulator)
			mxt_regulator_enable(data);

		mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
		data->suspended = false;
	}

	if (!data->in_bootloader) {
		/* Change to the bootloader mode */
		data->in_bootloader = true;

		ret = mxt_t6_command(data, MXT_COMMAND_RESET,
					 MXT_BOOT_VALUE, false);
		if (ret)
			goto release_firmware;

		msleep(MXT_RESET_TIME);

		/* Do not need to scan since we know family ID */
		ret = mxt_lookup_bootloader_address(data, 0);
		if (ret)
			goto release_firmware;

		mxt_free_input_device(data);
		mxt_free_object_table(data);
	} else {
		mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
	}

	INIT_COMPLETION(data->bl_completion);

	ret = mxt_check_bootloader(data, MXT_WAITING_BOOTLOAD_CMD, false);
	if (ret) {
		/* Bootloader may still be unlocked from previous attempt */
		ret = mxt_check_bootloader(data, MXT_WAITING_FRAME_DATA, false);
		if (ret)
			goto disable_irq;
	} else {
		TPD_DMESG("Unlocking bootloader\n");

		/* Unlock bootloader */
		ret = mxt_send_bootloader_cmd(data, true);
		if (ret)
			goto disable_irq;
	}

	while (pos < fw->size) {
		ret = mxt_check_bootloader(data, MXT_WAITING_FRAME_DATA, true);
		if (ret)
			goto disable_irq;

		frame_size = ((*(fw->data + pos) << 8) | *(fw->data + pos + 1));

		/* Take account of CRC bytes */
		frame_size += 2;

		/* Write one frame to device */
		ret = mxt_bootloader_write(data, fw->data + pos, frame_size);
		if (ret)
			goto disable_irq;

		ret = mxt_check_bootloader(data, MXT_FRAME_CRC_PASS, true);
		if (ret) {
			retry++;

			/* Back off by 20ms per retry */
			msleep(retry * 20);

			if (retry > 20) {
				TPD_DMESG("Retry count exceeded\n");
				goto disable_irq;
			}
		} else {
			retry = 0;
			pos += frame_size;
			frame++;
		}

		if (frame % 50 == 0)
			TPD_DMESG("Sent %d frames, %d/%zd bytes\n",
				frame, pos, fw->size);
	}

	/* Wait for flash. */
	ret = mxt_wait_for_completion(data, &data->bl_completion,
					  MXT_FW_RESET_TIME);
	if (ret)
		goto disable_irq;

	TPD_DMESG("Sent %d frames, %d bytes\n", frame, pos);

	/*
	 * Wait for device to reset. Some bootloader versions do not assert
	 * the CHG line after bootloading has finished, so ignore potential
	 * errors.
	 */
	mxt_wait_for_completion(data, &data->bl_completion, MXT_FW_RESET_TIME);

	data->in_bootloader = false;

disable_irq:
	mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
release_firmware:
	release_firmware(fw);
	return ret;
}

static int mxt_update_file_name(struct device *dev, char **file_name,
				const char *buf, size_t count)
{
	char *file_name_tmp;

	/* Simple sanity check */
	if (count > 64) {
		TPD_DMESG("File name too long\n");
		return -EINVAL;
	}

	file_name_tmp = krealloc(*file_name, count + 1, GFP_KERNEL);
	if (!file_name_tmp)
		return -ENOMEM;

	*file_name = file_name_tmp;
	memcpy(*file_name, buf, count);

	/* Echo into the sysfs entry may append newline at the end of buf */
	if (buf[count - 1] == '\n')
		(*file_name)[count - 1] = '\0';
	else
		(*file_name)[count] = '\0';

	return 0;
}

static ssize_t mxt_update_fw_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int error;

	error = mxt_update_file_name(dev, &data->fw_name, buf, count);
	if (error)
		return error;

	error = mxt_load_fw(dev);
	if (error) {
		TPD_DMESG("The firmware update failed(%d)\n", error);
		count = error;
	} else {
		TPD_DMESG("The firmware update succeeded\n");

		data->suspended = false;

		error = mxt_initialize(data);
		if (error)
			return error;
	}

	return count;
}

static ssize_t mxt_update_cfg_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	const struct firmware *cfg;
	int ret;

	if (data->in_bootloader) {
		TPD_DMESG("Not in appmode\n");
		return -EINVAL;
	}

	ret = mxt_update_file_name(dev, &data->cfg_name, buf, count);
	if (ret)
		return ret;

	ret = request_firmware(&cfg, data->cfg_name, dev);
	if (ret < 0) {
		TPD_DMESG("Failure to request config file %s\n",
			data->cfg_name);
		ret = -ENOENT;
		goto out;
	}

	data->updating_config = true;

	mxt_free_input_device(data);

	if (data->suspended) {
		if (data->use_regulator) {
			mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
			mxt_regulator_enable(data);
		} else {
			mxt_set_t7_power_cfg(data, MXT_POWER_CFG_RUN);
			mxt_acquire_irq(data);
		}

		data->suspended = false;
	}

	ret = mxt_configure_objects(data, cfg);
	if (ret)
		goto release;

	ret = count;

release:
	release_firmware(cfg);
out:
	data->updating_config = false;
	return ret;
}

static ssize_t mxt_debug_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	char c;

	c = data->debug_enabled ? '1' : '0';
	return scnprintf(buf, PAGE_SIZE, "%c\n", c);
}

static ssize_t mxt_debug_notify_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "0\n");
}

static ssize_t mxt_debug_v2_enable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	u8 i;
	ssize_t ret;

	if (kstrtou8(buf, 0, &i) == 0 && i < 2) {
		if (i == 1)
			mxt_debug_msg_enable(data);
		else
			mxt_debug_msg_disable(data);

		ret = count;
	} else {
		TPD_DMESG("debug_enabled write error\n");
		ret = -EINVAL;
	}

	return ret;
}

static ssize_t mxt_debug_enable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	u8 i;
	ssize_t ret;

	if (kstrtou8(buf, 0, &i) == 0 && i < 2) {
		data->debug_enabled = (i == 1);

		TPD_DMESG("%s\n", i ? "debug enabled" : "debug disabled");
		ret = count;
	} else {
		TPD_DMESG("debug_enabled write error\n");
		ret = -EINVAL;
	}

	return ret;
}

static int mxt_check_mem_access_params(struct mxt_data *data, loff_t off,
					   size_t *count)
{
	if (off >= data->mem_size)
		return -EIO;

	if (off + *count > data->mem_size)
		*count = data->mem_size - off;

	if (*count > MXT_MAX_BLOCK_WRITE)
		*count = MXT_MAX_BLOCK_WRITE;

	return 0;
}

static ssize_t mxt_mem_access_read(struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct mxt_data *data = dev_get_drvdata(dev);
	int ret = 0;

	ret = mxt_check_mem_access_params(data, off, &count);
	if (ret < 0)
		return ret;

	if (count > 0)
		ret = read_mem(data->client, off, count, buf);

	return ret == 0 ? count : ret;
}

static ssize_t mxt_mem_access_write(struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buf, loff_t off,
	size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct mxt_data *data = dev_get_drvdata(dev);
	int ret = 0;

	ret = mxt_check_mem_access_params(data, off, &count);
	if (ret < 0)
		return ret;

	if (count > 0)
		ret = write_mem(data->client, off, count, buf);

	return ret == 0 ? count : ret;
}

static DEVICE_ATTR(fw_version, S_IRUGO, mxt_fw_version_show, NULL);
static DEVICE_ATTR(hw_version, S_IRUGO, mxt_hw_version_show, NULL);
static DEVICE_ATTR(object, S_IRUGO, mxt_object_show, NULL);
static DEVICE_ATTR(update_fw, S_IWUSR, NULL, mxt_update_fw_store);
static DEVICE_ATTR(update_cfg, S_IWUSR, NULL, mxt_update_cfg_store);
static DEVICE_ATTR(config_csum, S_IRUGO, mxt_config_csum_show, NULL);
static DEVICE_ATTR(debug_enable, S_IWUSR | S_IRUSR, mxt_debug_enable_show,
		   mxt_debug_enable_store);
static DEVICE_ATTR(debug_v2_enable, S_IWUSR | S_IRUSR, NULL,
		   mxt_debug_v2_enable_store);
static DEVICE_ATTR(debug_notify, S_IRUGO, mxt_debug_notify_show, NULL);

static struct attribute *mxt_attrs[] = {
	&dev_attr_fw_version.attr,
	&dev_attr_hw_version.attr,
	&dev_attr_object.attr,
	&dev_attr_update_fw.attr,
	&dev_attr_update_cfg.attr,
	&dev_attr_config_csum.attr,
	&dev_attr_debug_enable.attr,
	&dev_attr_debug_v2_enable.attr,
	&dev_attr_debug_notify.attr,
	NULL
};

static const struct attribute_group mxt_attr_group = {
	.attrs = mxt_attrs,
};

static void mxt_reset_slots(struct mxt_data *data)
{
	struct input_dev *input_dev = data->input_dev;
	unsigned int num_mt_slots;
	int id;

	if (!input_dev)
		return;

	num_mt_slots = data->num_touchids + data->num_stylusids;

	for (id = 0; id < num_mt_slots; id++) {
		input_mt_slot(input_dev, id);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 0);
	}

	mxt_input_sync(data);
}

static void mxt_start(struct mxt_data *data)
{
	if (!data->suspended || data->in_bootloader)
		return;

	if (data->use_regulator) {
		mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);

		mxt_regulator_enable(data);
	} else {
		/*
		 * Discard any messages still in message buffer
		 * from before chip went to sleep
		 */
		mxt_process_messages_until_invalid(data);

		mxt_set_t7_power_cfg(data, MXT_POWER_CFG_RUN);

		/* Recalibrate since chip has been in deep sleep */
		mxt_t6_command(data, MXT_COMMAND_CALIBRATE, 1, false);

		mxt_acquire_irq(data);
	}

	data->suspended = false;
}

static void mxt_stop(struct mxt_data *data)
{
	if (data->suspended || data->in_bootloader || data->updating_config)
		return;

	mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);

	if (data->use_regulator)
		mxt_regulator_disable(data);
	else
		mxt_set_t7_power_cfg(data, MXT_POWER_CFG_DEEPSLEEP);

	mxt_reset_slots(data);
	data->suspended = true;
}

static int mxt_input_open(struct input_dev *dev)
{
	struct mxt_data *data = input_get_drvdata(dev);

	mxt_start(data);

	return 0;
}

static void mxt_input_close(struct input_dev *dev)
{
	struct mxt_data *data = input_get_drvdata(dev);

	mxt_stop(data);
}

#ifdef CONFIG_OF
static struct mxt_platform_data *mxt_parse_dt(struct i2c_client *client)
{
	struct mxt_platform_data *pdata;
	u32 *keymap;
	int proplen, ret;

	if (!client->dev.of_node)
		return ERR_PTR(-ENODEV);

	pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	/* reset gpio */
	pdata->gpio_reset = of_get_named_gpio_flags(client->dev.of_node,
		"atmel,reset-gpio", 0, NULL);

	of_property_read_string(client->dev.of_node, "atmel,cfg_name",
				&pdata->cfg_name);

	of_property_read_string(client->dev.of_node, "atmel,input_name",
				&pdata->input_name);

	if (of_find_property(client->dev.of_node, "linux,gpio-keymap",
				 &proplen)) {
		pdata->t19_num_keys = proplen / sizeof(u32);

		keymap = devm_kzalloc(&client->dev,
				pdata->t19_num_keys * sizeof(keymap[0]),
				GFP_KERNEL);
		if (!keymap)
			return ERR_PTR(-ENOMEM);

		ret = of_property_read_u32_array(client->dev.of_node,
			"linux,gpio-keymap", keymap, pdata->t19_num_keys);
		if (ret) {
			TPD_DMESG(
				"Unable to read device tree key codes: %d\n",
				 ret);
			return NULL;
		}

		pdata->t19_keymap = keymap;
	}

	return pdata;
}
#else
static struct mxt_platform_data *mxt_parse_dt(struct i2c_client *client)
{
	struct mxt_platform_data *pdata;

	TPD_DMESG("%s:alloc pdata\n", __FUNCTION__);

	pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	/* Set default parameters */
	pdata->irqflags = IRQF_TRIGGER_LOW;

	return pdata;
}
#endif

static int touch_event_handler(void *pdata)
{
	struct mxt_data *data = pdata;
	int id;
	u8 msg[8];
	struct sched_param param = { .sched_priority = RTPM_PRIO_TPD };

	sched_setscheduler(current, SCHED_RR, &param);

	do {
		#if 1
		set_current_state(TASK_INTERRUPTIBLE);
		wait_event_interruptible(waiter, tpd_flag!=0);
		tpd_flag = 0;
		if (thread_exit) {
			break;
		}

		set_current_state(TASK_RUNNING);
		#else
		msleep(100);
		#endif

		mxt_interrupt(CUST_EINT_TOUCH_PANEL_NUM, data);
		mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
	}while(!kthread_should_stop());

	return 0;
}

static void tpd_eint_interrupt_handler(void)
{
	TPD_DEBUG_PRINT_INT;
	tpd_flag = 1;
	wake_up_interruptible(&waiter);
}

static int tpd_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct mxt_data *data;
	const struct mxt_platform_data *pdata;
	int error;

	TPD_DMESG("MXT probe begin\n");

	pdata = dev_get_platdata(&client->dev);
	if (!pdata) {
		pdata = mxt_parse_dt(client);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	}

	data = kzalloc(sizeof(struct mxt_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	MXTI2CDMABuf_va = (u8 *)dma_alloc_coherent(NULL, TPD_DMA_MAX_TRANSACTION_LENGTH, &MXTI2CDMABuf_pa, GFP_KERNEL);
	if(!MXTI2CDMABuf_va)
	{
		TPD_DMESG("dma_alloc_coherent error\n");
	}

	mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
	mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO);
	msleep(MXT_REGULATOR_DELAY);
#ifdef TPD_POWER_SOURCE_CUSTOM
	hwPowerOn(TPD_POWER_SOURCE_CUSTOM, VOL_2800, "TP");
#endif
	mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ONE);
	msleep(MXT_REGULATOR_DELAY);

	snprintf(data->phys, sizeof(data->phys), "i2c-%u-%04x/input0",
		 client->adapter->nr, client->addr);

	data->client = client;
	data->pdata = pdata;
	data->irq = client->irq;
	i2c_set_clientdata(client, data);

	if (data->pdata->cfg_name)
		mxt_update_file_name(&data->client->dev,
					 &data->cfg_name,
					 data->pdata->cfg_name,
					 strlen(data->pdata->cfg_name));

	else
		data->cfg_name = atmel_cfg_path;

	init_completion(&data->bl_completion);
	init_completion(&data->reset_completion);
	init_completion(&data->crc_completion);
	mutex_init(&data->debug_msg_lock);

	client->irq = CUST_EINT_TOUCH_PANEL_NUM;

	error = sysfs_create_group(&client->dev.kobj, &mxt_attr_group);
	if (error) {
		TPD_DMESG("Failure %d creating sysfs group\n",
			error);
		goto err_free_mem;
	}

	sysfs_bin_attr_init(&data->mem_access_attr);
	data->mem_access_attr.attr.name = "mem_access";
	data->mem_access_attr.attr.mode = S_IRUGO | S_IWUSR;
	data->mem_access_attr.read = mxt_mem_access_read;
	data->mem_access_attr.write = mxt_mem_access_write;
	data->mem_access_attr.size = data->mem_size;

	if (sysfs_create_bin_file(&client->dev.kobj,
				  &data->mem_access_attr) < 0) {
		TPD_DMESG("Failed to create %s\n",
			data->mem_access_attr.attr.name);
		goto err_remove_sysfs_group;
	}

	thread = kthread_run(touch_event_handler, data, TPD_DEVICE);

	// Init IRQ
	SET_GPIO_AS_INT(GPIO_CTP_EINT_PIN);
	mt_eint_set_sens(CUST_EINT_TOUCH_PANEL_NUM, MT_LEVEL_SENSITIVE);
	mt_eint_set_hw_debounce(CUST_EINT_TOUCH_PANEL_NUM, 64);
	mt_eint_registration(CUST_EINT_TOUCH_PANEL_NUM, EINTF_TRIGGER_LOW, tpd_eint_interrupt_handler, 0);

	error = mxt_initialize(data);
	if (error) {
		goto err_remove_mem_access;
	}

	tpd_load_status = 1;

	return 0;

err_remove_mem_access:
	mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
	thread_exit = TRUE;
	wake_up_interruptible(&waiter);
	sysfs_remove_bin_file(&client->dev.kobj, &data->mem_access_attr);
	data->mem_access_attr.attr.name = NULL;
err_remove_sysfs_group:
	sysfs_remove_group(&client->dev.kobj, &mxt_attr_group);
err_free_mem:
	kfree(data);
	return error;
}

static int tpd_remove(struct i2c_client *client)
{
	struct mxt_data *data = i2c_get_clientdata(client);

	if (data->mem_access_attr.attr.name)
		sysfs_remove_bin_file(&client->dev.kobj,
					  &data->mem_access_attr);

	sysfs_remove_group(&client->dev.kobj, &mxt_attr_group);
	free_irq(data->irq, data);
	regulator_put(data->reg_avdd);
	regulator_put(data->reg_vdd);
	mxt_free_input_device(data);
	mxt_free_object_table(data);
	kfree(data);

	return 0;
}

static int __maybe_unused mxt_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mxt_data *data = i2c_get_clientdata(client);
	struct input_dev *input_dev = data->input_dev;

#if 0 //vin

	mutex_lock(&input_dev->mutex);

	if (input_dev->users)
		mxt_stop(data);

	mutex_unlock(&input_dev->mutex);
#endif
	return 0;
}

static int __maybe_unused mxt_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mxt_data *data = i2c_get_clientdata(client);
	struct input_dev *input_dev = data->input_dev;

#if 0
	mutex_lock(&input_dev->mutex);

	if (input_dev->users)
		mxt_start(data);

	mutex_unlock(&input_dev->mutex);
#endif
	return 0;
}

static int tpd_local_init(void)
{
	TPD_DMESG("MXT I2C Touchscreen Driver (Built %s @ %s)\n", __DATE__, __TIME__);

	if (i2c_add_driver(&tpd_i2c_driver) != 0) {
		TPD_DMESG("unable to add i2c driver.");
		return -1;
	}

	if (tpd_load_status == 0)	/* if(tpd_load_status == 0) // disable auto load touch driver for linux3.0 porting */
	{
		TPD_DMESG("add error touch panel driver.");
		i2c_del_driver(&tpd_i2c_driver);
		return -1;
	}

	input_set_abs_params(tpd->dev, ABS_MT_TRACKING_ID, 0, 4, 0, 0);

#ifdef TPD_HAVE_BUTTON
	tpd_button_setting(TPD_KEY_COUNT, tpd_keys_local, tpd_keys_dim_local);	/* initialize tpd button data */
#endif

#if (defined(TPD_WARP_START) && defined(TPD_WARP_END))
	TPD_DO_WARP = 1;
	memcpy(tpd_wb_start, tpd_wb_start_local, TPD_WARP_CNT * 4);
	memcpy(tpd_wb_end, tpd_wb_start_local, TPD_WARP_CNT * 4);
#endif

#if (defined(TPD_HAVE_CALIBRATION) && !defined(TPD_CUSTOM_CALIBRATION))
	memcpy(tpd_calmat, tpd_def_calmat_local, 8 * 4);
	memcpy(tpd_def_calmat, tpd_def_calmat_local, 8 * 4);
#endif


	TPD_DMESG("end %s, %d", __func__, __LINE__);
	tpd_type_cap = 1;

	return 0;
}


//=================================
static struct i2c_driver tpd_i2c_driver = {
	.driver = {
		.name	= MXT224_DEV_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= tpd_probe,
	.remove		= tpd_remove,
	.id_table 	= mxt_tpd_id,
};

static struct tpd_driver_t tpd_device_driver = {
	.tpd_device_name = MXT224_DEV_NAME,
	.tpd_local_init = tpd_local_init,
	.suspend = mxt_suspend,
	.resume = mxt_resume,
#ifdef TPD_HAVE_BUTTON
	.tpd_have_button = 1,
#else
	.tpd_have_button = 0,
#endif
};

static int __init tpd_driver_init(void) {
	printk("MediaTek MXT touch panel driver init\n");
	i2c_register_board_info(TPD_I2C_NUMBER, &mxt_i2c_tpd, 1);
	if(tpd_driver_add(&tpd_device_driver) < 0)
		TPD_DMESG("add MXT driver failed\n");
	 return 0;
 }

 /* should never be called */
static void __exit tpd_driver_exit(void) {
	TPD_DMESG("MediaTek MXT touch panel driver exit\n");
	//input_unregister_device(tpd->dev);
	tpd_driver_remove(&tpd_device_driver);
}

module_init(tpd_driver_init);
module_exit(tpd_driver_exit);

MODULE_DESCRIPTION("Atmel MaXTouch 224 driver");
MODULE_LICENSE("GPL");

