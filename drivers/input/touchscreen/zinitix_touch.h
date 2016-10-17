/*
 *
 * Zinitix touch driver
 *
 * Copyright (C) 2009 Zinitix, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef ZINITIX_HEADER
#define ZINITIX_HEADER

#define TS_DRVIER_VERSION		"2.0.8"

#define USE_THREADED_IRQ		1
#define USE_GPIO_INT_CHECK		1

#define USE_USB_INFO			1
#define USE_SVIEW_INFO			1
#define USE_SENSITIVE_MODE_INFO		1

#define	USE_WAKEUP_GESTURE		0
#define SUPPORTED_PALM_TOUCH		0

#define	PALM_REPORT_WIDTH		200
#define	PALM_REJECT_WIDTH		255

#define TOUCH_POINT_MODE		0
#define TOUCH_I2C_REGISTER_HERE		0

/* max 10 */
#define MAX_SUPPORTED_FINGER_NUM	5

/* max 8 */
#define MAX_SUPPORTED_BUTTON_NUM	4
#define SUPPORTED_BUTTON_NUM		0

/* Upgrade Method*/
#define TOUCH_ONESHOT_UPGRADE		1
/* if you use isp mode, you must add i2c device :
name = "zinitix_isp" , addr 0x50*/

/* resolution offset */
#define ABS_PT_OFFSET			(-1)

#define TOUCH_FORCE_UPGRADE		1
#define USE_CHECKSUM			1
#define CHECK_HWID			1

enum _zinitix_power_control {
	POWER_OFF,
	POWER_ON,
	POWER_ON_SEQUENCE,
};

/* Button Enum */
enum _zinitix_button_event {
	ICON_BUTTON_UNCHANGE,
	ICON_BUTTON_DOWN,
	ICON_BUTTON_UP,
};

/* ESD Protection */
/*second : if 0, no use. if you have to use, 3 is recommended*/
#define	ZINITIX_ESD_TIMER_INTERVAL	0
#define ZINITIX_SCAN_RATE_HZ		100
#define ZINITIX_CHECK_ESD_TIMER		3

/*Test Mode (Monitoring Raw Data) */
#define SEC_TSP_FACTORY_TEST		1	//for samsung

#define	SEC_DND_N_COUNT			10
#define	SEC_DND_FREQUENCY		110	//300khz

#define MAX_FW_PATH			255
#define TSP_FW_FILENAME			"zinitix_fw.bin"

/* preriod raw data interval*/
#define RAWDATA_DELAY_FOR_HOST		100

struct _raw_ioctl {
	int	sz;
	u8	*buf;
};

struct _reg_ioctl{
	int	addr;
	int	*val;
} ;

#define TOUCH_SEC_NORMAL_MODE	48

#define TOUCH_REF_MODE		10
#define TOUCH_NORMAL_MODE	5
#define TOUCH_DELTA_MODE	3
#define TOUCH_DND_MODE		6

/*  Other Things */
#define ZINITIX_INIT_RETRY_CNT	5
#define I2C_SUCCESS		0
#define I2C_FAIL		1

#define true	1
#define false	0

/*---------------------------------------------------------------------*/

/* Register Map*/
#define ZINITIX_SWRESET_CMD		0x0000
#define ZINITIX_WAKEUP_CMD		0x0001

#define ZINITIX_IDLE_CMD		0x0004
#define ZINITIX_SLEEP_CMD		0x0005

#define ZINITIX_CLEAR_INT_STATUS_CMD	0x0003
#define ZINITIX_CALIBRATE_CMD		0x0006
#define ZINITIX_SAVE_STATUS_CMD		0x0007
#define ZINITIX_SAVE_CALIBRATION_CMD	0x0008
#define ZINITIX_RECALL_FACTORY_CMD	0x000f

#define ZINITIX_SENSITIVITY		0x0020

#define ZINITIX_MODE_STATUS		0x116

#define	ZINITIX_I2C_CHECKSUM_WCNT	0x016a
#define	ZINITIX_I2C_CHECKSUM_RESULT	0x016c

#define ZINITIX_DEBUG_REG		0x0115	//0~7

#define ZINITIX_TOUCH_MODE		0x0010
#define ZINITIX_CHIP_REVISION		0x0011
#define ZINITIX_FIRMWARE_VERSION	0x0012

#define ZINITIX_MINOR_FW_VERSION	0x0121

#define ZINITIX_DATA_VERSION_REG	0x0013
#define ZINITIX_HW_ID			0x0014
#define ZINITIX_SUPPORTED_FINGER_NUM	0x0015
#define ZINITIX_EEPROM_INFO		0x0018
#define ZINITIX_INITIAL_TOUCH_MODE	0x0019

#define ZINITIX_TOTAL_NUMBER_OF_X	0x0060
#define ZINITIX_TOTAL_NUMBER_OF_Y	0x0061

#define ZINITIX_DELAY_RAW_FOR_HOST	0x007f

#define ZINITIX_BUTTON_SUPPORTED_NUM	0x00B0
#define ZINITIX_BUTTON_SENSITIVITY	0x00B2

#define ZINITIX_X_RESOLUTION		0x00C0
#define ZINITIX_Y_RESOLUTION		0x00C1

#define ZINITIX_POINT_STATUS_REG	0x0080
#define ZINITIX_ICON_STATUS_REG		0x00AA

#define ZINITIX_AFE_FREQUENCY		0x0100
#define ZINITIX_DND_N_COUNT		0x0122

#define ZINITIX_RAWDATA_REG		0x0200

#define ZINITIX_EEPROM_INFO_REG		0x0018

#define ZINITIX_INT_ENABLE_FLAG		0x00f0
#define ZINITIX_PERIODICAL_INTERRUPT_INTERVAL	0x00f1

#define ZINITIX_BTN_WIDTH		0x016d

#define ZINITIX_CHECKSUM_RESULT		0x012c

#define ZINITIX_INIT_FLASH		0x01d0
#define ZINITIX_WRITE_FLASH		0x01d1
#define ZINITIX_READ_FLASH		0x01d2

#define	ZINITIX_INTERNAL_FLAG_02	0x011e

/* Interrupt & status register flag bit
-------------------------------------------------
*/
#define BIT_PT_CNT_CHANGE	0
#define BIT_DOWN		1
#define BIT_MOVE		2
#define BIT_UP			3
#define BIT_PALM		4
#define BIT_PALM_REJECT		5
#define RESERVED_0		6
#define RESERVED_1		7
#define BIT_WEIGHT_CHANGE	8
#define BIT_PT_NO_CHANGE	9
#define BIT_REJECT		10
#define BIT_PT_EXIST		11
#define RESERVED_2		12
#define BIT_MUST_ZERO		13
#define BIT_DEBUG		14
#define BIT_ICON_EVENT		15

/* button */
#define BIT_O_ICON0_DOWN	0
#define BIT_O_ICON1_DOWN	1
#define BIT_O_ICON2_DOWN	2
#define BIT_O_ICON3_DOWN	3
#define BIT_O_ICON4_DOWN	4
#define BIT_O_ICON5_DOWN	5
#define BIT_O_ICON6_DOWN	6
#define BIT_O_ICON7_DOWN	7

#define BIT_O_ICON0_UP		8
#define BIT_O_ICON1_UP		9
#define BIT_O_ICON2_UP		10
#define BIT_O_ICON3_UP		11
#define BIT_O_ICON4_UP		12
#define BIT_O_ICON5_UP		13
#define BIT_O_ICON6_UP		14
#define BIT_O_ICON7_UP		15

#define SUB_BIT_EXIST		0
#define SUB_BIT_DOWN		1
#define SUB_BIT_MOVE		2
#define SUB_BIT_UP		3
#define SUB_BIT_UPDATE		4
#define SUB_BIT_WAIT		5

#define zinitix_bit_set(val, n)		((val) &= ~(1<<(n)), (val) |= (1<<(n)))
#define zinitix_bit_clr(val, n)		((val) &= ~(1<<(n)))
#define zinitix_bit_test(val, n)	((val) & (1<<(n)))
#define zinitix_swap_v(a, b, t)		((t) = (a), (a) = (b), (b) = (t))
#define zinitix_swap_16(s)		(((((s) & 0xff) << 8) | (((s) >> 8) & 0xff)))

#endif /*ZINITIX_HEADER*/
