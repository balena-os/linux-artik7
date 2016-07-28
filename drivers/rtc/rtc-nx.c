/*
 * Copyright (C) 2016  Nexell Co., Ltd.
 * Author: Hyunseok, Jung <hsjung@nexell.co.kr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/clk.h>
#include <linux/log2.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <asm/irq.h>

#define PWRCONT				0x24

static void __iomem *clkpwr_reg_for_rtc;

void nx_clkpwr_set_rtc_wakeup_enable(bool enable)
{
	const u32 rtc_wk_enb_pit_pos = 1;
	const u32 rtc_wk_enb_mask = 1 << rtc_wk_enb_pit_pos;

	register u32 readvalue;

	readvalue = readl((void *)(clkpwr_reg_for_rtc + PWRCONT));
	readvalue &= ~rtc_wk_enb_mask;
	readvalue |= (u32)enable << rtc_wk_enb_pit_pos;

	writel(readvalue, (void *)(clkpwr_reg_for_rtc + PWRCONT));
}

/*------------------------------------------------------------------------------
 * local data and macro
 */

#define	RTC_TIME_YEAR			(1970)		/* 1970.01.01 00:00:00
							 */
#define RTC_TIME_MAX			0x69546780	/* 2025.12.31 00:00:00
							 */
#define RTC_TIME_MIN			0x52c35a80	/* 2014.01.01 00:00:00
							 */
#define RTC_TIME_DFT			0x4a9c6400	/* 2009.09.01 00:00:00
							 */

#define	RTC_COUNT_BIT			(0)
#define	RTC_ALARM_BIT			(1)

#define RTC_CNT_WRITE			0x00
#define RTC_CNT_READ			0x04
#define RTC_ALARM			0x08
#define RTC_CTRL			0x0C
#define RTC_INT_ENB			0x10
#define RTC_INT_PND			0x14
#define RTC_CORE_RST_TIME_SEL		0x18
#define RTC_SCRATCH			0x1C

struct nx_rtc {
	struct device *dev;
	struct rtc_device *rtc;

	void __iomem *base;

	int irq_rtc;

	spinlock_t rtc_lock;

	int rtc_enable_irq;
	int alm_enable_irq;
};

static unsigned long	rtc_time_offs;

enum {
	NX_RTC_INT_COUNTER = 0,
	NX_RTC_INT_ALARM = 1
};

enum nx_rtc_resetdelay {
	NX_RTC_RESETDELAY_BYPASS = 1ul << 0,
	NX_RTC_RESETDELAY_31MS	 = 1ul << 1,
	NX_RTC_RESETDELAY_62MS	 = 1ul << 2,
	NX_RTC_RESETDELAY_93MS	 = 1ul << 3,
	NX_RTC_RESETDELAY_124MS	 = 1ul << 4,
	NX_RTC_RESETDELAY_155MS	 = 1ul << 5,
	NX_RTC_RESETDELAY_186MS	 = 1ul << 6,
	NX_RTC_RESETDELAY_210MS	 = 0
};

enum nx_rtc_oscsel {
	NX_RTC_OSCSEL_1HZ	= 0,
	NX_RTC_OSCSEL_32HZ	= 1,
};

void nx_rtc_set_interrupt_enable(struct nx_rtc *info, int32_t int_num,
				 bool enable)
{
	u32 regvalue;

	regvalue = readl(info->base + RTC_INT_ENB);
	regvalue &= ~(1ul << int_num);
	regvalue |= (u32)enable << int_num;
	writel(regvalue, info->base + RTC_INT_ENB);
}

int nx_rtc_get_interrupt_enable(struct nx_rtc *info, int32_t int_num)
{
	u32 regvalue;

	regvalue = (int)((readl(info->base + RTC_INT_ENB) >> int_num) & 0x01);

	return regvalue;
}

void nx_rtc_set_interrupt_enable32(struct nx_rtc *info, u32 enable_flag)
{
	const u32 enb_mask = 0x03;

	writel(enable_flag & enb_mask, info->base + RTC_INT_ENB);
}

u32 nx_rtc_get_interrupt_enable32(struct nx_rtc *info)
{
	u32 regvalue;
	const u32 enb_mask = 0x03;

	regvalue = (u32)(readl(info->base + RTC_INT_ENB) & enb_mask);
	return regvalue;
}

int nx_rtc_get_interrupt_pending(struct nx_rtc *info, int32_t int_num)
{
	u32 regvalue;

	regvalue = (int)((readl(info->base + RTC_INT_PND) >> int_num) & 0x01);
	return regvalue;
}

void nx_rtc_clear_interrupt_pending(struct nx_rtc *info, int32_t int_num)
{
	writel((u32)(1 << int_num),  info->base + RTC_INT_PND);
}

void nx_rtc_clear_interrupt_pending_all(struct nx_rtc *info)

{
	writel(0x03, info->base + RTC_INT_PND);
}

void nx_rtc_set_interrupt_enable_all(struct nx_rtc *info, bool enable)
{
	if (enable)
		writel(0x03, info->base + RTC_INT_ENB);
	else
		writel(0x00, info->base + RTC_INT_ENB);
}

u32 nx_rtc_get_rtc_counter(struct nx_rtc *info)
{
	u32 rtc_cnt;

	rtc_cnt = readl(info->base + RTC_CNT_READ);
	return rtc_cnt;
}

void nx_rtc_set_rtc_counter_write_enable(struct nx_rtc *info, int enable)
{
	const u32 write_enb_pos = 0;
	const u32 write_enb = 1ul << 0;
	u32 regvalue;

	regvalue = readl(info->base + RTC_CTRL);
	regvalue &= ~(write_enb);
	regvalue |= (enable) <<	write_enb_pos;
	writel(regvalue, info->base + RTC_CTRL);
}

bool nx_rtc_is_busy_rtc_counter(struct nx_rtc *info)
{
	bool is_busy;
	const u32 rtc_cnt_wait = (1ul << 4);

	is_busy = (readl(info->base + RTC_CTRL) & rtc_cnt_wait) ? true : false;
	return is_busy;
}

void nx_rtc_set_rtc_counter(struct nx_rtc *info, u32 rtc_counter)
{
	bool ret;

	do {
		ret = nx_rtc_is_busy_rtc_counter(info);
	} while (ret);
	writel(rtc_counter, info->base + RTC_CNT_WRITE);
}

void nx_rtc_set_alarm_counter(struct nx_rtc *info, u32 alarm_counter)
{
	writel(alarm_counter, info->base + RTC_ALARM);
}

u32 nx_rtc_get_alarm_counter(struct nx_rtc *info)
{
	u32 regvalue;

	regvalue = readl(info->base + RTC_ALARM);
	return regvalue;
}

int nx_rtc_is_busy_alarm_counter(struct nx_rtc *info)
{
	u32 regvalue;
	const u32 alarm_cnt_wait = (1ul << 3);

	regvalue = (readl(info->base + RTC_CTRL) & alarm_cnt_wait) ? true :
		false;
	return regvalue;
}

static void nx_rtc_setup(struct nx_rtc *info)
{
	unsigned long rtc, curr;
	struct rtc_time rtc_tm;
	bool ret;

	dev_dbg(info->dev, "%s\n", __func__);

	nx_rtc_clear_interrupt_pending_all(info);
	nx_rtc_set_interrupt_enable_all(info, false);

	rtc_time_offs = mktime(RTC_TIME_YEAR, 1, 1, 0, 0, 0);

	rtc = nx_rtc_get_rtc_counter(info);

	curr = rtc + rtc_time_offs;

	if ((curr > RTC_TIME_MAX) || (curr < RTC_TIME_MIN)) {
		/* set hw rtc */
		nx_rtc_set_rtc_counter_write_enable(info, true);
		nx_rtc_set_rtc_counter(info, RTC_TIME_DFT - rtc_time_offs);
		do {
			ret = nx_rtc_is_busy_rtc_counter(info);
		} while (ret);

		nx_rtc_set_rtc_counter_write_enable(info, false);

		/* Confirm the write value. */
		do {
			ret = nx_rtc_is_busy_rtc_counter(info);
		} while (ret);

		rtc = nx_rtc_get_rtc_counter(info);
	}

	rtc_time_to_tm(rtc + rtc_time_offs, &rtc_tm);
	dev_info(info->dev, "[RTC] day=%04d.%02d.%02d time=%02d:%02d:%02d\n",
		 rtc_tm.tm_year + 1900, rtc_tm.tm_mon + 1, rtc_tm.tm_mday,
		 rtc_tm.tm_hour, rtc_tm.tm_min, rtc_tm.tm_sec);
}

static int nx_rtc_irq_enable(int alarm, struct device *dev,
			      unsigned int enable)
{
	struct nx_rtc *info = dev_get_drvdata(dev);
	int bit = alarm ? RTC_ALARM_BIT : RTC_COUNT_BIT;

	dev_dbg(info->dev, "%s %s (enb:%d)\n",
		__func__, alarm?"alarm":"count", enable);

	if (enable)
		nx_rtc_set_interrupt_enable(info, bit, true);
	else
		nx_rtc_set_interrupt_enable(info, bit, false);

	if (alarm)
		info->alm_enable_irq = enable;
	else
		info->rtc_enable_irq = enable;

	return 0;
}

static irqreturn_t nx_rtc_interrupt(int irq, void *id)
{
	struct nx_rtc *info = (struct nx_rtc *)id;
	int pend = nx_rtc_get_interrupt_enable32(info);

	if (info->rtc_enable_irq && (pend & (1 << RTC_COUNT_BIT))) {
		rtc_update_irq(info->rtc, 1, RTC_PF | RTC_UF | RTC_IRQF);
		nx_rtc_clear_interrupt_pending(info, RTC_COUNT_BIT);
		dev_dbg(info->dev, "IRQ: RTC Count (PND:0x%x, ENB:%d)\n",
			pend, nx_rtc_get_interrupt_enable(info, RTC_COUNT_BIT));
		return IRQ_HANDLED;
	}

	if (info->alm_enable_irq && (pend & (1 << RTC_ALARM_BIT))) {
		rtc_update_irq(info->rtc, 1, RTC_AF | RTC_IRQF);
		nx_rtc_clear_interrupt_pending(info, RTC_ALARM_BIT);
		dev_dbg(info->dev, "IRQ: RTC Alarm (PND:0x%x, ENB:%d)\n",
			pend, nx_rtc_get_interrupt_enable(info, RTC_ALARM_BIT));
		return IRQ_HANDLED;
	}

	dev_info(info->dev, "IRQ: RTC Unknown (PND:0x%x, RTC ENB:%d, ALARM ENB:%d)\n",
		 pend, nx_rtc_get_interrupt_enable(info, RTC_COUNT_BIT),
		 nx_rtc_get_interrupt_enable(info, RTC_ALARM_BIT));

	nx_rtc_clear_interrupt_pending_all(info);
	return IRQ_NONE;
}

/*
 * RTC OPS
 */
static int nx_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct nx_rtc *info = dev_get_drvdata(dev);
	unsigned long rtc;

	spin_lock_irq(&info->rtc_lock);

	rtc = nx_rtc_get_rtc_counter(info);
	rtc_time_to_tm(rtc + rtc_time_offs, tm);
	dev_dbg(info->dev, "read time day=%04d.%02d.%02d time=%02d:%02d:%02d, rtc 0x%x\n",
		tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec, (uint)rtc);

	spin_unlock_irq(&info->rtc_lock);

	return 0;
}

static int nx_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct nx_rtc *info = dev_get_drvdata(dev);
	unsigned long rtc, curr_sec;
	bool ret;

	spin_lock_irq(&info->rtc_lock);

	curr_sec = mktime(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			  tm->tm_hour, tm->tm_min, tm->tm_sec);

	rtc = curr_sec - rtc_time_offs;

	dev_dbg(info->dev, "set time day=%02d.%02d.%02d time=%02d:%02d:%02d, rtc 0x%x\n",
		 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		 tm->tm_hour, tm->tm_min, tm->tm_sec, (uint)rtc);

	/* set hw rtc */
	nx_rtc_set_rtc_counter_write_enable(info, true);
	nx_rtc_set_rtc_counter(info, rtc);
	do {
		ret = nx_rtc_is_busy_rtc_counter(info);
	} while (ret);

	nx_rtc_set_rtc_counter_write_enable(info, false);

	/* Confirm the write value. */
	do {
		ret = nx_rtc_is_busy_rtc_counter(info);
	} while (ret);

	spin_unlock_irq(&info->rtc_lock);
	return 0;
}

static int nx_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct nx_rtc *info = dev_get_drvdata(dev);
	struct rtc_time *tm = &alrm->time;
	unsigned long count;

	spin_lock_irq(&info->rtc_lock);

	count = nx_rtc_get_alarm_counter(info);

	rtc_time_to_tm(count + rtc_time_offs, tm);

	alrm->enabled =
		(true == nx_rtc_get_interrupt_enable(info, RTC_ALARM_BIT)
		 ? 1 : 0);
	alrm->pending =
		(true == nx_rtc_get_interrupt_pending(info, RTC_ALARM_BIT)
		 ? 1 : 0);

	dev_dbg(info->dev, "read alarm day=%04d.%02d.%02d time=%02d:%02d:%02d,",
		 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		 tm->tm_hour, tm->tm_min, tm->tm_sec);
	dev_dbg(info->dev, "alarm 0x%08x\n", (uint)count);


	spin_unlock_irq(&info->rtc_lock);

	return 0;
}

static int nx_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct nx_rtc *info = dev_get_drvdata(dev);
	struct rtc_time *tm = &alrm->time;
	unsigned long count, seconds;
	int ret;

	spin_lock_irq(&info->rtc_lock);

	seconds = mktime(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
				tm->tm_hour, tm->tm_min, tm->tm_sec);

	count = seconds - rtc_time_offs;

	dev_dbg(info->dev, "set alarm day=%04d.%02d.%02d time=%02d:%02d:%02d, ",
		 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		 tm->tm_hour, tm->tm_min, tm->tm_sec);
	dev_dbg(info->dev, "alarm %lu, %s\n", count, alrm->enabled?"ON":"OFF");

	/* set hw rtc */
	do {
		ret = nx_rtc_is_busy_alarm_counter(info);
	} while (ret > 0);

	nx_rtc_set_alarm_counter(info, count);

	/* Confirm the write value. */
	do {
		ret = nx_rtc_is_busy_alarm_counter(info);
	} while (ret > 0);

	nx_rtc_clear_interrupt_pending(info, RTC_ALARM_BIT);

	/* 0: RTC Counter, 1: RTC Alarm */
	nx_rtc_set_interrupt_enable(info, RTC_ALARM_BIT,
				  alrm->enabled ? true : false);
	/* set wakeup source form stop mode */
	nx_clkpwr_set_rtc_wakeup_enable(alrm->enabled ? true : false);

	info->alm_enable_irq = alrm->enabled;

	spin_unlock_irq(&info->rtc_lock);
	return 0;
}

static int nx_rtc_open(struct device *dev)
{
	struct nx_rtc *info = dev_get_drvdata(dev);

	dev_dbg(info->dev, "%s\n", __func__);

	return 0;
}

static void nx_rtc_release(struct device *dev)
{
	struct nx_rtc *info = dev_get_drvdata(dev);

	dev_dbg(info->dev, "%s\n", __func__);
}

static int nx_rtc_alarm_irq_enable(struct device *dev, unsigned int enable)
{
	struct nx_rtc *info = dev_get_drvdata(dev);

	dev_dbg(info->dev, "%s (enb:%d)\n", __func__, enable);
	return nx_rtc_irq_enable(1, dev, enable);
}

static int nx_rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct nx_rtc *info = dev_get_drvdata(dev);
	int ret = 0;

	dev_dbg(info->dev, "%s cmd=%08x, arg=%08lx\n", __func__, cmd, arg);

	spin_lock_irq(&info->rtc_lock);

	switch (cmd) {
	case RTC_AIE_OFF:
		nx_rtc_irq_enable(1, dev, 0);
		break;
	case RTC_AIE_ON:
		nx_rtc_irq_enable(1, dev, 1);
		break;
	case RTC_UIE_OFF:
		nx_rtc_irq_enable(0, dev, 0);
		break;
	case RTC_UIE_ON:
		nx_rtc_irq_enable(0, dev, 1);
		break;
	case RTC_IRQP_SET:
		ret = ENOTTY;
		break;
	default:
		ret = -ENOIOCTLCMD;
	}

	spin_unlock_irq(&info->rtc_lock);
	return ret;
}

/*
 * Provide additional RTC information in /proc/driver/rtc
 */
static int nx_rtc_proc(struct device *dev, struct seq_file *seq)
{
	struct nx_rtc *info = dev_get_drvdata(dev);
	bool irq = nx_rtc_get_interrupt_enable(info, RTC_COUNT_BIT);

	seq_printf(seq, "update_IRQ\t: %s\n", irq ? "yes" : "no");
	seq_printf(seq, "periodic_IRQ\t: %s\n", irq ? "yes" : "no");
	return 0;
}

static const struct rtc_class_ops nx_rtc_ops = {
	.open			= nx_rtc_open,
	.release		= nx_rtc_release,
	.ioctl			= nx_rtc_ioctl,
	.read_time		= nx_rtc_read_time,
	.set_time		= nx_rtc_set_time,
	.read_alarm		= nx_rtc_read_alarm,
	.set_alarm		= nx_rtc_set_alarm,
	.proc			= nx_rtc_proc,
	.alarm_irq_enable	= nx_rtc_alarm_irq_enable,
};

/*
 * RTC platform driver functions
 */
static int nx_rtc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct nx_rtc *info = platform_get_drvdata(pdev);

	dev_dbg(info->dev, "+%s (rtc irq:%s, alarm irq:%s, wakeup=%d)\n",
		__func__, info->rtc_enable_irq?"on":"off",
		info->alm_enable_irq?"on":"off", device_may_wakeup(&pdev->dev));

	if (info->rtc_enable_irq) {
		nx_rtc_clear_interrupt_pending(info, RTC_COUNT_BIT);
		nx_rtc_set_interrupt_enable(info, RTC_COUNT_BIT, false);
	}

	if (info->alm_enable_irq) {
		unsigned long count = nx_rtc_get_alarm_counter(info);
		struct rtc_time tm;

		rtc_time_to_tm(count, &tm);
		dev_dbg(info->dev, "%s (alarm day=%04d.%02d.%02d time=%02d:%02d:%02d, ",
			__func__, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec);
		dev_dbg(info->dev, "alarm %lu)\n", count);
	}

	dev_dbg(info->dev, "-%s\n", __func__);
	return 0;
}

static int nx_rtc_resume(struct platform_device *pdev)
{
	struct nx_rtc *info = platform_get_drvdata(pdev);

	dev_dbg(info->dev, "+%s (rtc irq:%s, alarm irq:%s)\n",
		__func__, info->rtc_enable_irq?"on":"off",
		info->alm_enable_irq?"on":"off");

	if (info->rtc_enable_irq) {
		nx_rtc_clear_interrupt_pending(info, RTC_COUNT_BIT);
		nx_rtc_set_interrupt_enable(info, RTC_COUNT_BIT, true);
	}
	dev_dbg(info->dev, "-%s\n", __func__);
	return 0;
}

static int nx_rtc_probe(struct platform_device *pdev)
{
	struct nx_rtc *info = NULL;
	struct resource *res;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	/* find the IRQs */
	info->irq_rtc = platform_get_irq(pdev, 0);
	if (info->irq_rtc < 0) {
		dev_err(&pdev->dev, "no irq for rtc\n");
		return info->irq_rtc;
	}

	info->dev = &pdev->dev;
	spin_lock_init(&info->rtc_lock);

	platform_set_drvdata(pdev, info);

	dev_dbg(info->dev, "%s: rtc irq %d\n", __func__, info->irq_rtc);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(info->base))
		return PTR_ERR(info->base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	clkpwr_reg_for_rtc = ioremap_nocache(res->start, resource_size(res));
	if (IS_ERR(clkpwr_reg_for_rtc))
		return PTR_ERR(clkpwr_reg_for_rtc);

	nx_rtc_setup(info);

	/* cpu init code should really have flagged this device as
	 * being wake-capable; if it didn't, do that here.
	 */
	if (!device_can_wakeup(&pdev->dev))
		device_init_wakeup(&pdev->dev, 1);

	/* register RTC and exit */
	info->rtc = devm_rtc_device_register(&pdev->dev, "nx", &nx_rtc_ops,
				  THIS_MODULE);
	if (IS_ERR(info->rtc)) {
		dev_err(&pdev->dev, "cannot attach rtc\n");
		ret = PTR_ERR(info->rtc);
		return ret;
	}

	/* register disabled irq */
	ret = devm_request_irq(&pdev->dev, info->irq_rtc,
					 nx_rtc_interrupt, 0, "rtc 1hz", info);
	if (ret) {
		dev_err(&pdev->dev, "IRQ%d error %d\n", info->irq_rtc, ret);
		return ret;
	}

	/* set rtc frequency value */
	info->rtc->irq_freq	   = 1;
	info->rtc->max_user_freq = 1;

	dev_dbg(info->dev, "done: rtc probe ...\n");

	return 0;
}

static int nx_rtc_remove(struct platform_device *pdev)
{
	struct nx_rtc *info = platform_get_drvdata(pdev);

	dev_dbg(info->dev, "%s\n", __func__);

	free_irq(info->irq_rtc, info->rtc);

	devm_rtc_device_unregister(&pdev->dev, info->rtc);
	platform_set_drvdata(pdev, NULL);

	kfree(info->rtc);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id nx_rtc_dt_match[] = {
	{ .compatible = "nexell,nx-rtc"},
	{ },
};
MODULE_DEVICE_TABLE(of, nx_rtc_dt_match);
#else
#define nx_rtc_dt_match NULL
#endif

static struct platform_driver nx_rtc_driver = {
	.probe		= nx_rtc_probe,
	.remove		= nx_rtc_remove,
	.suspend	= nx_rtc_suspend,
	.resume		= nx_rtc_resume,
	.driver		= {
		.name	= "nx-rtc",
		.owner	= THIS_MODULE,
		.of_match_table	= of_match_ptr(nx_rtc_dt_match),
	},
};
module_platform_driver(nx_rtc_driver);

MODULE_DESCRIPTION("RTC driver for the Nexell");
MODULE_AUTHOR("Hyunseok Jung <hsjung@nexell.co.kr>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rtc");
