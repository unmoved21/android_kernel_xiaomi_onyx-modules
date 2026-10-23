// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include "btfm_i2s.h"
#include "btfm_i2s_hw_interface.h"

/* Global driver state - accessed by DAI ops in btfm_i2s_hw_interface.c */
struct btfmi2s *pbtfmi2s;

static int btfm_i2s_probe(struct platform_device *pdev)
{
	int ret;

	BTFMI2S_INFO("probe: pdev=%p", pdev);

	pbtfmi2s = devm_kzalloc(&pdev->dev, sizeof(*pbtfmi2s), GFP_KERNEL);
	if (!pbtfmi2s)
		return -ENOMEM;

	pbtfmi2s->dev = &pdev->dev;
	pbtfmi2s->initialized = false;
	platform_set_drvdata(pdev, pbtfmi2s);

	ret = btfm_i2s_register_hw_ep(pbtfmi2s);
	if (ret) {
		BTFMI2S_ERR("ALSA registration FAILED ret=%d", ret);
		pbtfmi2s = NULL;
		return ret;
	}

	BTFMI2S_INFO("ALSA registration SUCCESS");
	return 0;
}

static int btfm_i2s_remove(struct platform_device *pdev)
{
	struct btfmi2s *btfmi2s = platform_get_drvdata(pdev);

	BTFMI2S_INFO("remove: pdev=%p btfmi2s=%p", pdev, btfmi2s);

	if (btfmi2s) {
		btfm_i2s_unregister_hwep();
		pbtfmi2s = NULL;
	}
	return 0;
}

static const struct of_device_id btfm_i2s_of_match[] = {
	{ .compatible = "qcom,btfmi2s" },
	{},
};
MODULE_DEVICE_TABLE(of, btfm_i2s_of_match);

static struct platform_driver btfm_i2s_driver = {
	.probe	= btfm_i2s_probe,
	.remove	= btfm_i2s_remove,
	.driver	= {
		.name		= "btfm_i2s",
		.of_match_table	= btfm_i2s_of_match,
	},
};

module_platform_driver(btfm_i2s_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("BTFM I2S driver");
