// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Low Power Subsystem clocks.
 *
 * Copyright (C) 2013, Intel Corporation
 * Authors: Mika Westerberg <mika.westerberg@linux.intel.com>
 *	    Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/units.h>

#include <linux/platform_data/x86/clk-lpss.h>

static int lpss_atom_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lpss_clk_data *drvdata;
	struct clk *clk;
	u32 rate;
	int ret;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	if (device_property_present(dev, "clock-frequency")) {
		ret = device_property_read_u32(dev, "clock-frequency", &rate);
		if (ret)
			return ret;
	} else {
		/* Default frequency is 100MHz */
		rate = 100 * HZ_PER_MHZ;
	}

	/* LPSS free running clock */
	drvdata->name = "lpss_clk";
	clk = clk_register_fixed_rate(dev, drvdata->name, NULL, 0, rate);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	drvdata->clk = clk;
	platform_set_drvdata(pdev, drvdata);
	return 0;
}

static struct platform_driver lpss_atom_clk_driver = {
	.driver = {
		.name = "clk-lpss-atom",
	},
	.probe = lpss_atom_clk_probe,
};

int __init lpss_atom_clk_init(void)
{
	return platform_driver_register(&lpss_atom_clk_driver);
}
