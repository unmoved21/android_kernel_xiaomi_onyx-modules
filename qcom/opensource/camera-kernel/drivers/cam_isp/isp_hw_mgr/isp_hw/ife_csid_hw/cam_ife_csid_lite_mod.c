// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include "camera_main.h"
#include "cam_ife_csid_dev.h"
#include "cam_ife_csid_common.h"
#include "cam_ife_csid_hw_ver1.h"
#include "cam_ife_csid_hw_ver2.h"
#if IS_ENABLED(CONFIG_ARCH_CANOE)
#include "cam_ife_csid_lite1080.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_NAPALI) || IS_ENABLED(CONFIG_ARCH_NILE)
#include "cam_ife_csid_lite17x.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_KONA) || IS_ENABLED(CONFIG_ARCH_SHIMA) || IS_ENABLED(CONFIG_ARCH_LAHAINA)
#include "cam_ife_csid_lite480.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_WAIPIO)
#include "cam_ife_csid_lite680.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_KALAMA)
#include "cam_ife_csid_lite780.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_PINEAPPLE)
#include "cam_ife_csid_lite880.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_SUN)
#include "cam_ife_csid_lite980.h"
#include "cam_ife_csid_lite975.h"
#include "cam_ife_csid_lite970.h"
#endif

#define CAM_CSID_LITE_DRV_NAME                    "csid_lite"

#if IS_ENABLED(CONFIG_ARCH_NAPALI) || IS_ENABLED(CONFIG_ARCH_NILE)
static struct cam_ife_csid_core_info cam_ife_csid_lite_17x_hw_info = {
	.csid_reg = &cam_ife_csid_lite_17x_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_1_0,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_KONA) || IS_ENABLED(CONFIG_ARCH_SHIMA) || IS_ENABLED(CONFIG_ARCH_LAHAINA)
static struct cam_ife_csid_core_info cam_ife_csid_lite_480_hw_info = {
	.csid_reg = &cam_ife_csid_lite_480_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_1_0,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_WAIPIO)
static struct cam_ife_csid_core_info cam_ife_csid_lite_680_hw_info = {
	.csid_reg = &cam_ife_csid_lite_680_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_2_0,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_KALAMA)
static struct cam_ife_csid_core_info cam_ife_csid_lite_780_hw_info = {
	.csid_reg = &cam_ife_csid_lite_780_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_2_0,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_PINEAPPLE)
static struct cam_ife_csid_core_info cam_ife_csid_lite_880_hw_info = {
	.csid_reg = &cam_ife_csid_lite_880_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_2_0,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_SUN)
static struct cam_ife_csid_core_info cam_ife_csid_lite_970_hw_info = {
	.csid_reg = &cam_ife_csid_lite_970_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_2_0,
};

static struct cam_ife_csid_core_info cam_ife_csid_lite_975_hw_info = {
	.csid_reg = &cam_ife_csid_lite_975_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_2_0,
};

static struct cam_ife_csid_core_info cam_ife_csid_lite_980_hw_info = {
	.csid_reg = &cam_ife_csid_lite_980_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_2_0,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_CANOE)
static struct cam_ife_csid_core_info cam_ife_csid_lite_1080_hw_info = {
	.csid_reg = &cam_ife_csid_lite_1080_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_2_0,
};
#endif

static const struct of_device_id cam_ife_csid_lite_dt_match[] = {
#if IS_ENABLED(CONFIG_ARCH_NAPALI) || IS_ENABLED(CONFIG_ARCH_NILE)
	{
		.compatible = "qcom,csid-lite170",
		.data = &cam_ife_csid_lite_17x_hw_info,
	},
	{
		.compatible = "qcom,csid-lite175",
		.data = &cam_ife_csid_lite_17x_hw_info,
	},
	{
		.compatible = "qcom,csid-lite165",
		.data = &cam_ife_csid_lite_17x_hw_info,
	},
#endif
#if IS_ENABLED(CONFIG_ARCH_KONA) || IS_ENABLED(CONFIG_ARCH_SHIMA) || IS_ENABLED(CONFIG_ARCH_LAHAINA)
	{
		.compatible = "qcom,csid-lite480",
		.data = &cam_ife_csid_lite_480_hw_info,
	},
	{
		.compatible = "qcom,csid-lite570",
		.data = &cam_ife_csid_lite_480_hw_info,
	},
	{
		.compatible = "qcom,csid-lite580",
		.data = &cam_ife_csid_lite_480_hw_info,
	},
#endif
#if IS_ENABLED(CONFIG_ARCH_WAIPIO)
	{
		.compatible = "qcom,csid-lite680",
		.data = &cam_ife_csid_lite_680_hw_info,
	},
	{
		.compatible = "qcom,csid-lite680_110",
		.data = &cam_ife_csid_lite_680_hw_info,
	},
#endif
#if IS_ENABLED(CONFIG_ARCH_KALAMA)
	{
		.compatible = "qcom,csid-lite780",
		.data = &cam_ife_csid_lite_780_hw_info,
	},
#endif
#if IS_ENABLED(CONFIG_ARCH_PINEAPPLE)
	{
		.compatible = "qcom,csid-lite880",
		.data = &cam_ife_csid_lite_880_hw_info,
	},
#endif
#if IS_ENABLED(CONFIG_ARCH_SUN)
	{
		.compatible = "qcom,csid-lite980",
		.data = &cam_ife_csid_lite_980_hw_info,
	},
	{
		.compatible = "qcom,csid-lite970",
		.data = &cam_ife_csid_lite_970_hw_info,
	},
	{
		.compatible = "qcom,csid-lite975",
		.data = &cam_ife_csid_lite_975_hw_info,
	},
#endif
#if IS_ENABLED(CONFIG_ARCH_CANOE)
	{
		.compatible = "qcom,csid-lite1080",
		.data = &cam_ife_csid_lite_1080_hw_info,
	},
#endif
	{}
};

MODULE_DEVICE_TABLE(of, cam_ife_csid_lite_dt_match);

struct platform_driver cam_ife_csid_lite_driver = {
	.probe = cam_ife_csid_probe,
	.remove = cam_ife_csid_remove,
	.driver = {
		.name = CAM_CSID_LITE_DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = cam_ife_csid_lite_dt_match,
		.suppress_bind_attrs = true,
	},
};

int cam_ife_csid_lite_init_module(void)
{
	return platform_driver_register(&cam_ife_csid_lite_driver);
}

void cam_ife_csid_lite_exit_module(void)
{
	platform_driver_unregister(&cam_ife_csid_lite_driver);
}

MODULE_DESCRIPTION("CAM IFE_CSID_LITE driver");
MODULE_LICENSE("GPL v2");
