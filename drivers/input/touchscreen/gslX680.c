/*
 * drivers/input/touchscreen/gslX680.c
 *
 * Copyright (c) 2012 Shanghai Basewin
 *	Guan Yuwei<guanyuwei@basewin.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>

#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

#include "gslX680.h"

#define REPORT_DATA_ANDROID_4_0
#ifdef FILTER_POINT
#define FILTER_MAX 9
#endif

#define GSLX680_I2C_NAME "gslX680"
#define GSLX680_I2C_ADDR 0x40
#define IRQ_PORT PB_PIO_IRQ(CFG_IO_TOUCH_PENDOWN_DETECT)

#define GSL_DATA_REG 0x80
#define GSL_STATUS_REG 0xe0
#define GSL_PAGE_REG 0xf0

#define PRESS_MAX 255
#define MAX_FINGERS 10
#define MAX_CONTACTS 10
#define DMA_TRANS_LEN 0x20
#ifdef GSL_MONITOR
static struct delayed_work gsl_monitor_work;
static struct workqueue_struct *gsl_monitor_workqueue;
static u8 int_1st[4] = {0};
static u8 int_2nd[4] = {0};
static char dac_counter;
static char b0_counter;
static char bc_counter;
static char i2c_lock_flag;
#endif

static struct i2c_client *gsl_client;

#ifdef HAVE_TOUCH_KEY
static u16 key;
static int key_state_flag;
struct key_data {
	u16 key;
	u16 x_min;
	u16 x_max;
	u16 y_min;
	u16 y_max;
};

const u16 key_array[] = {
	KEY_BACK, KEY_HOME, KEY_MENU, KEY_SEARCH,
};
#define MAX_KEY_NUM ARRAY_SIZE(key_array)

struct key_data gsl_key_data[MAX_KEY_NUM] = {
	{KEY_BACK, 2048, 2048, 2048, 2048},
	{KEY_HOME, 2048, 2048, 2048, 2048},
	{KEY_MENU, 2048, 2048, 2048, 2048},
	{KEY_SEARCH, 2048, 2048, 2048, 2048},
};
#endif

struct gsl_ts_data {
	u8 x_index;
	u8 y_index;
	u8 z_index;
	u8 id_index;
	u8 touch_index;
	u8 data_reg;
	u8 status_reg;
	u8 data_size;
	u8 touch_bytes;
	u8 update_data;
	u8 touch_meta_data;
	u8 finger_size;
};

static struct gsl_ts_data devices[] = {
	{
		.x_index = 6,
		.y_index = 4,
		.z_index = 5,
		.id_index = 7,
		.data_reg = GSL_DATA_REG,
		.status_reg = GSL_STATUS_REG,
		.update_data = 0x4,
		.touch_bytes = 4,
		.touch_meta_data = 4,
		.finger_size = 70,
	},
};

struct gsl_ts {
	struct i2c_client *client;
	struct input_dev *input;
	struct work_struct work;
	struct workqueue_struct *wq;
	struct gsl_ts_data *dd;
	u8 *touch_data;
	u8 device_id;
	int irq;
	struct work_struct resume_work;
};

#ifdef GSL_DEBUG
#define print_info(fmt, args...)	pr_info(fmt, ##args)
#else
#define print_info(fmt, args...)
#endif

static u32 id_sign[MAX_CONTACTS + 1] = {0};
static u8 id_state_flag[MAX_CONTACTS + 1] = {0};
static u8 id_state_old_flag[MAX_CONTACTS + 1] = {0};
static u16 x_old[MAX_CONTACTS + 1] = {0};
static u16 y_old[MAX_CONTACTS + 1] = {0};
static u16 x_new;
static u16 y_new;

static int gslX680_init(void)
{
	return 0;
}

static int gslX680_shutdown_low(void) { return 0; }

static inline u16 join_bytes(u8 a, u8 b)
{
	u16 ab = 0;

	ab = ab | a;
	ab = ab << 8 | b;
	return ab;
}

static u32 gsl_write_interface(struct i2c_client *client, const u8 reg, u8 *buf,
			       u32 num)
{
	struct i2c_msg xfer_msg[1];

	buf[0] = reg;

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = num + 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = buf;

	return i2c_transfer(client->adapter, xfer_msg, 1) == 1 ? 0 : -EFAULT;
}

static int gsl_ts_write(struct i2c_client *client, u8 addr, u8 *pdata,
			int datalen)
{
	int ret = 0;
	u8 tmp_buf[128];
	unsigned int bytelen = 0;

	if (datalen > 125) {
		pr_info("%s too big datalen = %d!\n", __func__, datalen);
		return -1;
	}

	tmp_buf[0] = addr;
	bytelen++;

	if (datalen != 0 && pdata != NULL) {
		memcpy(&tmp_buf[bytelen], pdata, datalen);
		bytelen += datalen;
	}

	ret = i2c_master_send(client, tmp_buf, bytelen);
	return ret;
}

int gsl_ts_readbyte(struct i2c_client *client, u8 addr, u8 *pdata)
{
	int ret = 0;

	ret = gsl_ts_write(client, addr, NULL, 0);
	if (ret < 0) {
		pr_info("%s set data address fail!\n", __func__);
		return ret;
	}

	return i2c_master_recv(client, pdata, 1);
}

static int gsl_ts_read(struct i2c_client *client, u8 addr, u8 *pdata,
		       unsigned int datalen)
{
	int ret = 0;
	int i = 0;

	if (datalen > 126) {
		pr_info("%s too big datalen = %d!\n", __func__, datalen);
		return -1;
	}
	for (i = 0; i < datalen; i++) {
		ret = gsl_ts_readbyte(client, addr + i, pdata + i);
		if (ret < 0)
			return ret;
	}
	return ret;
}

static inline void fw2buf(u8 *buf, const u32 *fw)
{
	u32 *u32_buf = (int *)buf;

	*u32_buf = *fw;
}

static void gsl_load_fw(struct i2c_client *client)
{
	u8 buf[DMA_TRANS_LEN * 4 + 1] = {0};
	u8 send_flag = 1;
	u8 *cur = buf + 1;
	u32 source_line = 0;
	u32 source_len;
	struct fw_data *ptr_fw;

	pr_info("=============gsl_load_fw start==============\n");

	ptr_fw = GSLX680_FW;
	source_len = ARRAY_SIZE(GSLX680_FW);

	for (source_line = 0; source_line < source_len; source_line++) {
		/* init page trans, set the page val */
		if (GSL_PAGE_REG == ptr_fw[source_line].offset) {
			fw2buf(cur, &ptr_fw[source_line].val);
			gsl_write_interface(client, GSL_PAGE_REG, buf, 4);
			send_flag = 1;
		} else {
			if (1 ==
			    send_flag %
			    (DMA_TRANS_LEN < 0x20 ? DMA_TRANS_LEN : 0x20))
				buf[0] = (u8)ptr_fw[source_line].offset;

			fw2buf(cur, &ptr_fw[source_line].val);
			cur += 4;

			if (0 ==
			    send_flag %
			    (DMA_TRANS_LEN < 0x20 ? DMA_TRANS_LEN : 0x20)) {
				gsl_write_interface(client, buf[0], buf,
						    cur - buf - 1);
				cur = buf + 1;
			}

			send_flag++;
		}
	}

	pr_info("=============gsl_load_fw end==============\n");
}

static void startup_chip(struct i2c_client *client)
{
	u8 tmp = 0x00;

#ifdef GSL_NOID_VERSION
	gsl_DataInit(gsl_config_data_id);
#endif
	gsl_ts_write(client, 0xe0, &tmp, 1);
}

static void reset_chip(struct i2c_client *client)
{
	u8 tmp = 0x88;
	u8 buf[4] = {0x00};

	gsl_ts_write(client, 0xe0, &tmp, sizeof(tmp));
	msleep(20);
	tmp = 0x04;
	gsl_ts_write(client, 0xe4, &tmp, sizeof(tmp));
	msleep(10);
	gsl_ts_write(client, 0xbc, buf, sizeof(buf));
	msleep(10);
}

static void clr_reg(struct i2c_client *client)
{
	u8 write_buf[4] = {0};

	write_buf[0] = 0x88;
	gsl_ts_write(client, 0xe0, &write_buf[0], 1);
	msleep(20);
	write_buf[0] = 0x03;
	gsl_ts_write(client, 0x80, &write_buf[0], 1);
	msleep(5);
	write_buf[0] = 0x04;
	gsl_ts_write(client, 0xe4, &write_buf[0], 1);
	msleep(5);
	write_buf[0] = 0x00;
	gsl_ts_write(client, 0xe0, &write_buf[0], 1);
	msleep(20);
}

static void init_chip(struct i2c_client *client)
{
	clr_reg(client);
	reset_chip(client);
	gsl_load_fw(client);
	startup_chip(client);
	reset_chip(client);
	startup_chip(client);
}

#ifdef FILTER_POINT
static void filter_point(u16 x, u16 y, u8 id)
{
	u16 x_err = 0;
	u16 y_err = 0;
	u16 filter_step_x = 0, filter_step_y = 0;

	id_sign[id] = id_sign[id] + 1;
	if (id_sign[id] == 1) {
		x_old[id] = x;
		y_old[id] = y;
	}

	x_err = x > x_old[id] ? (x - x_old[id]) : (x_old[id] - x);
	y_err = y > y_old[id] ? (y - y_old[id]) : (y_old[id] - y);

	if ((x_err > FILTER_MAX && y_err > FILTER_MAX / 3) ||
	    (x_err > FILTER_MAX / 3 && y_err > FILTER_MAX)) {
		filter_step_x = x_err;
		filter_step_y = y_err;
	} else {
		if (x_err > FILTER_MAX)
			filter_step_x = x_err;
		if (y_err > FILTER_MAX)
			filter_step_y = y_err;
	}

	if (x_err <= 2 * FILTER_MAX && y_err <= 2 * FILTER_MAX) {
		filter_step_x >>= 2;
		filter_step_y >>= 2;
	} else if (x_err <= 3 * FILTER_MAX && y_err <= 3 * FILTER_MAX) {
		filter_step_x >>= 1;
		filter_step_y >>= 1;
	} else if (x_err <= 4 * FILTER_MAX && y_err <= 4 * FILTER_MAX) {
		filter_step_x = filter_step_x * 3 / 4;
		filter_step_y = filter_step_y * 3 / 4;
	}

	x_new = x > x_old[id] ? (x_old[id] + filter_step_x)
		: (x_old[id] - filter_step_x);
	y_new = y > y_old[id] ? (y_old[id] + filter_step_y)
		: (y_old[id] - filter_step_y);

	x_old[id] = x_new;
	y_old[id] = y_new;
}
#else
static void record_point(u16 x, u16 y, u8 id)
{
	u16 x_err = 0;
	u16 y_err = 0;

	id_sign[id] = id_sign[id] + 1;

	if (id_sign[id] == 1) {
		x_old[id] = x;
		y_old[id] = y;
	}

	x = (x_old[id] + x) / 2;
	y = (y_old[id] + y) / 2;

	if (x > x_old[id])
		x_err = x - x_old[id];
	else
		x_err = x_old[id] - x;

	if (y > y_old[id])
		y_err = y - y_old[id];
	else
		y_err = y_old[id] - y;

	if ((x_err > 3 && y_err > 1) || (x_err > 1 && y_err > 3)) {
		x_new = x;
		x_old[id] = x;
		y_new = y;
		y_old[id] = y;
	} else {
		if (x_err > 3) {
			x_new = x;
			x_old[id] = x;
		} else
			x_new = x_old[id];
		if (y_err > 3) {
			y_new = y;
			y_old[id] = y;
		} else
			y_new = y_old[id];
	}

	if (id_sign[id] == 1) {
		x_new = x_old[id];
		y_new = y_old[id];
	}
}
#endif

#ifdef HAVE_TOUCH_KEY
static void report_key(struct gsl_ts *ts, u16 x, u16 y)
{
	u16 i = 0;

	for (i = 0; i < MAX_KEY_NUM; i++) {
		if ((gsl_key_data[i].x_min < x) &&
		    (x < gsl_key_data[i].x_max) &&
		    (gsl_key_data[i].y_min < y) &&
		    (y < gsl_key_data[i].y_max)) {
			key = gsl_key_data[i].key;
			input_report_key(ts->input, key, 1);
			input_sync(ts->input);
			key_state_flag = 1;
			break;
		}
	}
}
#endif

static void report_data(struct gsl_ts *ts, u16 x, u16 y, u8 pressure, u8 id)
{
	swap(x, y);

	print_info("#####id=%d,x=%d,y=%d######\n", id, x, y);

	if (x > SCREEN_MAX_X || y > SCREEN_MAX_Y) {
#ifdef HAVE_TOUCH_KEY
		report_key(ts, x, y);
#endif
		return;
	}
	input_report_abs(ts->input, ABS_MT_PRESSURE, id);
	input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, 1);
	input_report_abs(ts->input, ABS_MT_POSITION_X, x);
	input_report_abs(ts->input, ABS_MT_POSITION_Y, y);

	input_mt_sync(ts->input);
}

static void gslX680_ts_worker(struct work_struct *work)
{
	struct gsl_ts *ts = container_of(work, struct gsl_ts, work);
	int rc, i;
	u8 id, touches;
	u16 x, y;

#ifdef GSL_NOID_VERSION
	u32 tmp1;
	u8 buf[4] = {0};
	struct gsl_touch_info cinfo;

	memset(&cinfo, 0, sizeof(struct gsl_touch_info));
#endif

	print_info("=====gslX680_ts_worker=====\n");

#ifdef GSL_MONITOR
	if (i2c_lock_flag != 0)
		goto i2c_lock_schedule;
	else
		i2c_lock_flag = 1;
#endif

	rc = gsl_ts_read(ts->client, 0x80, &ts->touch_data[0], 4);
	if (rc < 0) {
		dev_err(&ts->client->dev, "read failed\n");
		goto schedule;
	}
	touches = ts->touch_data[ts->dd->touch_index];

	if (touches > 0)
		gsl_ts_read(ts->client, 0x84, &ts->touch_data[4], 4);
	if (touches > 1)
		gsl_ts_read(ts->client, 0x88, &ts->touch_data[8], 4);
	if (touches > 2)
		gsl_ts_read(ts->client, 0x8c, &ts->touch_data[12], 4);
	if (touches > 3)
		gsl_ts_read(ts->client, 0x90, &ts->touch_data[16], 4);
	if (touches > 4)
		gsl_ts_read(ts->client, 0x94, &ts->touch_data[20], 4);
	if (touches > 5)
		gsl_ts_read(ts->client, 0x98, &ts->touch_data[24], 4);
	if (touches > 6)
		gsl_ts_read(ts->client, 0x9c, &ts->touch_data[28], 4);
	if (touches > 7)
		gsl_ts_read(ts->client, 0xa0, &ts->touch_data[32], 4);
	if (touches > 8)
		gsl_ts_read(ts->client, 0xa4, &ts->touch_data[36], 4);
	if (touches > 9)
		gsl_ts_read(ts->client, 0xa8, &ts->touch_data[40], 4);

	print_info("-----touches: %d -----\n", touches);
#ifdef GSL_NOID_VERSION
	cinfo.finger_num = touches;
	print_info("tp-gsl  finger_num = %d\n", cinfo.finger_num);
	for (i = 0; i < (touches < MAX_CONTACTS ? touches : MAX_CONTACTS);
	     i++) {
		cinfo.x[i] = join_bytes(
			(ts->touch_data[ts->dd->x_index + 4 * i + 1] & 0xf),
			ts->touch_data[ts->dd->x_index + 4 * i]);
		cinfo.y[i] = join_bytes(
			ts->touch_data[ts->dd->y_index + 4 * i + 1],
			ts->touch_data[ts->dd->y_index + 4 * i]);
		cinfo.id[i] = (
			(ts->touch_data[ts->dd->x_index + 4 * i + 1] & 0xf0)
			>> 4);
		print_info(
			"tp-gsl before: x[%d] = %d, y[%d] = %d, id[%d] = %d\n",
			i, cinfo.x[i], i, cinfo.y[i], i, cinfo.id[i]);
	}
	cinfo.finger_num = (ts->touch_data[3] << 24) |
		(ts->touch_data[2] << 16) |
		(ts->touch_data[1] << 8) | (ts->touch_data[0]);
	gsl_alg_id_main(&cinfo);
	tmp1 = gsl_mask_tiaoping();

	print_info("[tp-gsl] tmp1=%x\n", tmp1);

	if (tmp1 > 0 && tmp1 < 0xffffffff) {
		buf[0] = 0xa;
		buf[1] = 0;
		buf[2] = 0;
		buf[3] = 0;
		gsl_ts_write(ts->client, 0xf0, buf, 4);
		buf[0] = (u8)(tmp1 & 0xff);
		buf[1] = (u8)((tmp1 >> 8) & 0xff);
		buf[2] = (u8)((tmp1 >> 16) & 0xff);
		buf[3] = (u8)((tmp1 >> 24) & 0xff);
		print_info("tmp1=%08x, b0=%02x, b1=%02x, b2=%02x, b3=%02x\n",
			   tmp1, buf[0], buf[1], buf[2], buf[3]);
		gsl_ts_write(ts->client, 0x8, buf, 4);
	}
	touches = cinfo.finger_num;
#endif

	for (i = 1; i <= MAX_CONTACTS; i++) {
		if (touches == 0)
			id_sign[i] = 0;
		id_state_flag[i] = 0;
	}

	for (i = 0; i < (touches > MAX_FINGERS ? MAX_FINGERS : touches); i++) {
#ifdef GSL_NOID_VERSION
		id = cinfo.id[i];
		x = cinfo.x[i];
		y = cinfo.y[i];
#else
		x = join_bytes(
		       ts->touch_data[ts->dd->x_index + 4 * i + 1] & 0xf,
		       ts->touch_data[ts->dd->x_index + 4 * i]);
		y = join_bytes(ts->touch_data[ts->dd->y_index + 4 * i + 1],
		       ts->touch_data[ts->dd->y_index + 4 * i]);
		id = ts->touch_data[ts->dd->id_index + 4 * i] >> 4;
#endif

		if (1 <= id && id <= MAX_CONTACTS) {
#ifdef FILTER_POINT
			filter_point(x, y, id);
#else
			record_point(x, y, id);
#endif
			report_data(ts, x_new, y_new, 10, id);
			id_state_flag[id] = 1;
		}
	}

	for (i = 1; i <= MAX_CONTACTS; i++) {
		if ((0 == touches) ||
		    ((0 != id_state_old_flag[i]) && (0 == id_state_flag[i]))) {
#ifdef REPORT_DATA_ANDROID_4_0
			input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, 0);
			input_mt_sync(ts->input);
#endif
			id_sign[i] = 0;
		}
		id_state_old_flag[i] = id_state_flag[i];
	}
	if (0 == touches) {
#ifndef REPORT_DATA_ANDROID_4_0
		input_mt_sync(ts->input);
#endif
#ifdef HAVE_TOUCH_KEY
		if (key_state_flag) {
			input_report_key(ts->input, key, 0);
			input_sync(ts->input);
			key_state_flag = 0;
		}
#endif
	}
	input_sync(ts->input);

schedule:
#ifdef GSL_MONITOR
	i2c_lock_flag = 0;
i2c_lock_schedule:
#endif
	enable_irq(ts->irq);
}

#ifdef GSL_MONITOR
static void gsl_monitor_worker(void)
{
	u8 write_buf[4] = {0};
	u8 read_buf[4] = {0};
	char init_chip_flag = 0;

	print_info("----------------gsl_monitor_worker-----------------\n");

	if (i2c_lock_flag != 0)
		goto queue_monitor_work;
	else
		i2c_lock_flag = 1;

	gsl_ts_read(gsl_client, 0xb0, read_buf, 4);
	if (read_buf[3] != 0x5a || read_buf[2] != 0x5a || read_buf[1] != 0x5a ||
	    read_buf[0] != 0x5a)
		b0_counter++;
	else
		b0_counter = 0;

	if (b0_counter > 1) {
		pr_info("======read 0xb0: %x %x %x %x ======\n", read_buf[3],
		       read_buf[2], read_buf[1], read_buf[0]);
		init_chip_flag = 1;
		b0_counter = 0;
		goto queue_monitor_init_chip;
	}

	gsl_ts_read(gsl_client, 0xb4, read_buf, 4);
	int_2nd[3] = int_1st[3];
	int_2nd[2] = int_1st[2];
	int_2nd[1] = int_1st[1];
	int_2nd[0] = int_1st[0];
	int_1st[3] = read_buf[3];
	int_1st[2] = read_buf[2];
	int_1st[1] = read_buf[1];
	int_1st[0] = read_buf[0];

	if (int_1st[3] == int_2nd[3] && int_1st[2] == int_2nd[2] &&
	    int_1st[1] == int_2nd[1] && int_1st[0] == int_2nd[0]) {
		pr_info("====int_1st: %x %x %x %x , int_2nd: %x %x %x %x====\n",
		       int_1st[3], int_1st[2], int_1st[1], int_1st[0],
		       int_2nd[3], int_2nd[2], int_2nd[1], int_2nd[0]);
		init_chip_flag = 1;
		goto queue_monitor_init_chip;
	}

	gsl_ts_read(gsl_client, 0xbc, read_buf, 4);
	if (read_buf[3] != 0 || read_buf[2] != 0 || read_buf[1] != 0 ||
	    read_buf[0] != 0)
		bc_counter++;
	else
		bc_counter = 0;
	if (bc_counter > 1) {
		pr_info("======read 0xbc: %x %x %x %x======\n", read_buf[3],
		       read_buf[2], read_buf[1], read_buf[0]);
		init_chip_flag = 1;
		bc_counter = 0;
	}
queue_monitor_init_chip:
	if (init_chip_flag)
		init_chip(gsl_client);

	i2c_lock_flag = 0;

queue_monitor_work:
	queue_delayed_work(gsl_monitor_workqueue, &gsl_monitor_work, 100);
}
#endif

static irqreturn_t gsl_ts_irq(int irq, void *dev_id)
{
	struct gsl_ts *ts = dev_id;

	print_info("========gslX680 Interrupt=========\n");

	disable_irq_nosync(ts->irq);

	if (!work_pending(&ts->work))
		queue_work(ts->wq, &ts->work);

	return IRQ_HANDLED;
}

static int gslX680_ts_init(struct i2c_client *client, struct gsl_ts *ts)
{
	struct input_dev *input_device;
	int rc = 0;

	pr_info("[GSLX680] Enter %s\n", __func__);

	ts->dd = &devices[ts->device_id];

	if (ts->device_id == 0) {
		ts->dd->data_size = MAX_FINGERS * ts->dd->touch_bytes
			+ ts->dd->touch_meta_data;
		ts->dd->touch_index = 0;
	}

	ts->touch_data = kzalloc(ts->dd->data_size, GFP_KERNEL);
	if (!ts->touch_data)
		return -ENOMEM;

	input_device = input_allocate_device();
	if (!input_device) {
		rc = -ENOMEM;
		goto error_alloc_dev;
	}

	ts->input = input_device;
	input_device->name = GSLX680_I2C_NAME;
	input_device->id.bustype = BUS_I2C;
	input_device->dev.parent = &client->dev;
	input_set_drvdata(input_device, ts);

	set_bit(EV_ABS, input_device->evbit);

	input_set_abs_params(input_device, ABS_MT_POSITION_X, 0, SCREEN_MAX_X,
			     0, 0);
	input_set_abs_params(input_device, ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y,
			     0, 0);
	input_set_abs_params(input_device, ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0,
			     0);
	input_set_abs_params(input_device, ABS_MT_PRESSURE, 0, 255, 0, 0);
#ifdef HAVE_TOUCH_KEY
	input_device->evbit[0] = BIT_MASK(EV_KEY);
	for (i = 0; i < MAX_KEY_NUM; i++)
		set_bit(key_array[i], input_device->keybit);
#endif

	ts->irq = client->irq;

	ts->wq = create_singlethread_workqueue("kworkqueue_ts");
	if (!ts->wq) {
		dev_err(&client->dev, "Could not create workqueue\n");
		goto error_wq_create;
	}
	flush_workqueue(ts->wq);

	INIT_WORK(&ts->work, gslX680_ts_worker);

	rc = input_register_device(input_device);
	if (rc)
		goto error_unreg_device;

	return 0;

error_unreg_device:
	destroy_workqueue(ts->wq);
error_wq_create:
	input_free_device(input_device);
error_alloc_dev:
	kfree(ts->touch_data);
	return rc;
}

static int gsl_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct gsl_ts *ts = i2c_get_clientdata(client);

	pr_info("I'am in gsl_ts_suspend() start\n");

#ifdef GSL_MONITOR
	pr_info("gsl_ts_suspend () : cancel gsl_monitor_work\n");
	cancel_delayed_work_sync(&gsl_monitor_work);
#endif

	disable_irq_nosync(ts->irq);

	gslX680_shutdown_low();

#ifdef SLEEP_CLEAR_POINT
	msleep(10);
#ifdef REPORT_DATA_ANDROID_4_0
	for (i = 1; i <= MAX_CONTACTS; i++) {
		input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, 0);
		input_mt_sync(ts->input);
	}
#else
	input_mt_sync(ts->input);
#endif
	input_sync(ts->input);
	msleep(10);
	report_data(ts, 1, 1, 10, 1);
	input_sync(ts->input);
#endif

	return 0;
}

static void gs_ts_work_resume(struct work_struct *work)
{
	struct gsl_ts *ts = container_of(work, struct gsl_ts, resume_work);

	init_chip(ts->client);

#ifdef GSL_MONITOR
	pr_info("gsl_ts_resume () : queue gsl_monitor_work\n");
	queue_work(gsl_monitor_workqueue, &gsl_monitor_work.work);
#endif
	enable_irq(ts->irq);
}

static int gsl_ts_resume(struct i2c_client *client)
{
	struct gsl_ts *ts = i2c_get_clientdata(client);

	schedule_work(&ts->resume_work);
	return 0;
}
#ifdef CONFIG_OF
static int gsl_ts_parse_dt(struct i2c_client *client)
{
	struct device_node *np = client->dev.of_node;

	if (of_property_read_u32(np, "mtouch-irq", &client->irq)) {
		pr_err("incorrect irq number\n");
		return -EINVAL;
	}
	return 0;
}
#else
static int gsl_ts_parse_dt(struct i2c_client *client) { return 0; }
#endif
static int gsl_ts_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct gsl_ts *ts;
	int rc;
	int mirq;

	pr_info("GSLX680 Enter %s\n", __func__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C functionality not supported\n");
		return -ENODEV;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;
	pr_info("==kzalloc success=\n");

	if (gsl_ts_parse_dt(client)) {
		pr_info("parse dtb data fail!\n");
		return -EINVAL;
	}
	ts->client = client;
	i2c_set_clientdata(client, ts);
	ts->device_id = id->driver_data;

	rc = gslX680_ts_init(client, ts);
	if (rc < 0) {
		dev_err(&client->dev, "GSLX680 init failed\n");
		goto error_mutex_destroy;
	}

	gsl_client = client;

	gslX680_init();
	init_chip(ts->client);

	mirq = gpio_to_irq(client->irq);
	rc = request_irq(mirq, gsl_ts_irq, IRQF_TRIGGER_RISING, client->name,
			 ts);
	if (rc < 0) {
		pr_info("gsl_probe: request irq failed\n");
		goto error_req_irq_fail;
	}

	/* create debug attribute */
#ifdef GSL_MONITOR
	pr_info("gsl_ts_probe () : queue gsl_monitor_workqueue\n");

	INIT_DELAYED_WORK(&gsl_monitor_work, gsl_monitor_worker);
	gsl_monitor_workqueue =
		create_singlethread_workqueue("gsl_monitor_workqueue");
	queue_delayed_work(gsl_monitor_workqueue, &gsl_monitor_work, 1000);
#endif
	device_enable_async_suspend(&client->dev);
	INIT_WORK(&ts->resume_work, gs_ts_work_resume);

	pr_info("[GSLX680] End %s\n", __func__);

	return 0;

error_req_irq_fail:
	free_irq(ts->irq, ts);

error_mutex_destroy:
	input_free_device(ts->input);
	kfree(ts);
	return rc;
}

static int gsl_ts_remove(struct i2c_client *client)
{
	struct gsl_ts *ts = i2c_get_clientdata(client);

	pr_info("==gsl_ts_remove=\n");

#ifdef GSL_MONITOR
	cancel_delayed_work_sync(&gsl_monitor_work);
	destroy_workqueue(gsl_monitor_workqueue);
#endif

	device_init_wakeup(&client->dev, 0);
	cancel_work_sync(&ts->work);
	free_irq(ts->irq, ts);
	destroy_workqueue(ts->wq);
	input_unregister_device(ts->input);

	kfree(ts->touch_data);
	kfree(ts);

	return 0;
}

static const struct i2c_device_id gsl_ts_id[] = { {GSLX680_I2C_NAME, 0}, {} };
MODULE_DEVICE_TABLE(i2c, gsl_ts_id);
#ifdef CONFIG_OF
static const struct of_device_id gslX680_of_match[] = {
	{
		.compatible = "gslX680",
	},
	{/* sentinel */}
};
MODULE_DEVICE_TABLE(of, gslX680_of_match);
#endif

static struct i2c_driver gsl_ts_driver = {
	.driver = {
		.name = GSLX680_I2C_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = gslX680_of_match,
#endif
	},
	.probe = gsl_ts_probe,
	.remove = gsl_ts_remove,
	.id_table = gsl_ts_id,
};

static int __init gsl_ts_init(void)
{
	int ret;

	pr_info("==gsl_ts_init==\n");
	ret = i2c_add_driver(&gsl_ts_driver);
	pr_info("ret=%d\n", ret);
	return ret;
}
static void __exit gsl_ts_exit(void)
{
	pr_info("==gsl_ts_exit==\n");
	i2c_del_driver(&gsl_ts_driver);
}

module_init(gsl_ts_init);
module_exit(gsl_ts_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GSLX680 touchscreen controller driver");
MODULE_AUTHOR("Guan Yuwei, guanyuwei@basewin.com");
MODULE_ALIAS("platform:gsl_ts");
