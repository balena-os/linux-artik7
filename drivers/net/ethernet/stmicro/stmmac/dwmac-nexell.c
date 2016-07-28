/*
 * Copyright (C) 2016  Nexell Co., Ltd.
 * Author: Bon-gyu, KOO <freestyle@nexell.co.kr>
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

#include <linux/stmmac.h>
#include <linux/clk.h>
#include <linux/phy.h>
#include <linux/of_net.h>

#include "stmmac_platform.h"

struct nexell_priv_data {
	int clk_enabled;
	struct clk *tx_clk;
};

static void *nexell_gmac_setup(struct platform_device *pdev)
{
	struct nexell_priv_data *gmac;
	struct device *dev = &pdev->dev;

	gmac = devm_kzalloc(dev, sizeof(*gmac), GFP_KERNEL);
	if (!gmac)
		return ERR_PTR(-ENOMEM);

	gmac->tx_clk = devm_clk_get(dev, "nexell_gmac_tx");
	if (IS_ERR(gmac->tx_clk)) {
		dev_err(dev, "could not get tx clock\n");
		return gmac->tx_clk;
	}

	return gmac;
}

#define GMAC_GMII_RGMII_RATE	125000000

static int nexell_gmac_init(struct platform_device *pdev, void *priv)
{
	struct nexell_priv_data *gmac = priv;

	clk_set_rate(gmac->tx_clk, GMAC_GMII_RGMII_RATE);
	clk_prepare_enable(gmac->tx_clk);
	gmac->clk_enabled = 1;

	return 0;
}

static void nexell_gmac_exit(struct platform_device *pdev, void *priv)
{
	struct nexell_priv_data *gmac = priv;

	if (gmac->clk_enabled) {
		clk_disable(gmac->tx_clk);
		gmac->clk_enabled = 0;
	}
	clk_unprepare(gmac->tx_clk);
}

const struct stmmac_of_data nexell_gmac_data = {
	.has_gmac = 1,
	.setup = nexell_gmac_setup,
	.init = nexell_gmac_init,
	.exit = nexell_gmac_exit,
};
