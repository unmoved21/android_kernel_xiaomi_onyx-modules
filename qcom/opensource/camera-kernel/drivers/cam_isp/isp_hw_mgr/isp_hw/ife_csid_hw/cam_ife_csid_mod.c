// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include "cam_ife_csid_dev.h"
#include "camera_main.h"
#include "cam_ife_csid_common.h"
#include "cam_ife_csid_hw_ver1.h"
#include "cam_ife_csid_hw_ver2.h"

#if IS_ENABLED(CONFIG_ARCH_CANOE)
#include "cam_ife_csid1080.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_NAPALI)
#include "cam_ife_csid170.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_TALOS)
#include "cam_ife_csid170_200.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_NILE)
#include "cam_ife_csid175.h"
#include "cam_ife_csid175_200.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_KONA)
#include "cam_ife_csid480.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_SHIMA)
#include "cam_ife_csid570.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_LAHAINA)
#include "cam_ife_csid580.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_WAIPIO)
#include "cam_ife_csid680.h"
#include "cam_ife_csid680_110.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_KALAMA)
#include "cam_ife_csid780.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_PINEAPPLE)
#include "cam_ife_csid880.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_SUN)
#include "cam_ife_csid980.h"
#include "cam_ife_csid975.h"
#include "cam_ife_csid970.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_CHORA)
#include "cam_ife_csid662.h"
#endif

#define CAM_CSID_DRV_NAME                    "csid"

#if IS_ENABLED(CONFIG_ARCH_NAPALI)
static struct cam_ife_csid_core_info cam_ife_csid170_hw_info = {
	.csid_reg = &cam_ife_csid_170_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_1_0,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_TALOS)
static struct cam_ife_csid_core_info cam_ife_csid170_200_hw_info = {
	.csid_reg = &cam_ife_csid_170_200_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_1_0,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_NILE)
static struct cam_ife_csid_core_info cam_ife_csid175_hw_info = {
	.csid_reg = &cam_ife_csid_175_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_1_0,
};

static struct cam_ife_csid_core_info cam_ife_csid175_200_hw_info = {
	.csid_reg = &cam_ife_csid_175_200_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_1_0,
};

static struct cam_ife_csid_core_info cam_ife_csid165_204_hw_info = {
	.csid_reg = &cam_ife_csid_175_200_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_1_0,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_KONA)
static struct cam_ife_csid_core_info cam_ife_csid480_hw_info = {
	.csid_reg = &cam_ife_csid_480_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_1_0,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_SHIMA)
static struct cam_ife_csid_core_info cam_ife_csid570_hw_info = {
	.csid_reg = &cam_ife_csid_570_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_1_0,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_LAHAINA)
static struct cam_ife_csid_core_info cam_ife_csid580_hw_info = {
	.csid_reg = &cam_ife_csid_580_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_1_0,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_CHORA)
static struct cam_ife_csid_core_info cam_ife_csid662_hw_info = {
	.csid_reg = &cam_ife_csid_662_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_1_0,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_WAIPIO)
static struct cam_ife_csid_core_info cam_ife_csid680_hw_info = {
	.csid_reg = &cam_ife_csid_680_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_2_0,
};

static struct cam_ife_csid_core_info cam_ife_csid680_110_hw_info = {
	.csid_reg = &cam_ife_csid_680_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_2_0,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_KALAMA)
static struct cam_ife_csid_core_info cam_ife_csid780_hw_info = {
	.csid_reg = &cam_ife_csid_780_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_2_0,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_PINEAPPLE)
static struct cam_ife_csid_core_info cam_ife_csid880_hw_info = {
	.csid_reg = &cam_ife_csid_880_reg_info,
	.sw_version  = CAM_IFE_CSID_VER_2_0,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_SUN)
static struct cam_ife_csid_core_info cam_ife_csid980_hw_info = {
	.csid_reg = &cam_ife_csid_980_reg_info,
	.sw_version = CAM_IFE_CSID_VER_2_0,
};

static struct cam_ife_csid_core_info cam_ife_csid970_hw_info = {
	.csid_reg = &cam_ife_csid_970_reg_info,
	.sw_version = CAM_IFE_CSID_VER_2_0,
};

static struct cam_ife_csid_core_info cam_ife_csid975_hw_info = {
	.csid_reg = &cam_ife_csid_975_reg_info,
	.sw_version = CAM_IFE_CSID_VER_2_0,
};
#endif

#if IS_ENABLED(CONFIG_ARCH_CANOE)
static struct cam_ife_csid_core_info cam_ife_csid1080_hw_info = {
	.csid_reg = &cam_ife_csid_1080_reg_info,
	.sw_version = CAM_IFE_CSID_VER_2_0,
};
#endif

static const struct of_device_id cam_ife_csid_dt_match[] = {

#if IS_ENABLED(CONFIG_ARCH_NAPALI)
	{
		.compatible = "qcom,csid170",
		.data = &cam_ife_csid170_hw_info,
	},
#endif
#if IS_ENABLED(CONFIG_ARCH_TALOS)
	{
		.compatible = "qcom,csid170_200",
		.data = &cam_ife_csid170_200_hw_info,
	},
#endif
#if IS_ENABLED(CONFIG_ARCH_NILE)
	{
		.compatible = "qcom,csid175",
		.data = &cam_ife_csid175_hw_info,
	},
	{
		.compatible = "qcom,csid175_200",
		.data = &cam_ife_csid175_200_hw_info,
	},
	{
		.compatible = "qcom,csid165_204",
		.data = &cam_ife_csid165_204_hw_info,
	},
#endif
#if IS_ENABLED(CONFIG_ARCH_KONA)
	{
		.compatible = "qcom,csid480",
		.data = &cam_ife_csid480_hw_info,
	},
#endif
#if IS_ENABLED(CONFIG_ARCH_SHIMA)
	{
		.compatible = "qcom,csid570",
		.data = &cam_ife_csid570_hw_info,
	},
#endif
#if IS_ENABLED(CONFIG_ARCH_LAHAINA)
	{
		.compatible = "qcom,csid580",
		.data = &cam_ife_csid580_hw_info,
	},
#endif
#if IS_ENABLED(CONFIG_ARCH_WAIPIO)
	{
		.compatible = "qcom,csid680",
		.data = &cam_ife_csid680_hw_info,
	},
	{
		.compatible = "qcom,csid680_110",
		.data = &cam_ife_csid680_110_hw_info,
	},
#endif
#if IS_ENABLED(CONFIG_ARCH_KALAMA)
	{
		.compatible = "qcom,csid780",
		.data = &cam_ife_csid780_hw_info,
	},
#endif
#if IS_ENABLED(CONFIG_ARCH_PINEAPPLE)
	{
		.compatible = "qcom,csid880",
		.data = &cam_ife_csid880_hw_info,
	},
#endif
#if IS_ENABLED(CONFIG_ARCH_SUN)
	{
		.compatible = "qcom,csid980",
		.data = &cam_ife_csid980_hw_info,
	},
	{
		.compatible = "qcom,csid970",
		.data = &cam_ife_csid970_hw_info,
	},
	{
		.compatible = "qcom,csid975",
		.data = &cam_ife_csid975_hw_info,
	},
#endif
#if IS_ENABLED(CONFIG_ARCH_CANOE)
	{
		.compatible = "qcom,csid1080",
		.data = &cam_ife_csid1080_hw_info,
	},
#endif
#if IS_ENABLED(CONFIG_ARCH_CHORA)
	{
		.compatible = "qcom,csid662",
		.data = &cam_ife_csid662_hw_info,
	},
#endif
	{},
};

MODULE_DEVICE_TABLE(of, cam_ife_csid_dt_match);

struct platform_driver cam_ife_csid_driver = {
	.probe = cam_ife_csid_probe,
	.remove = cam_ife_csid_remove,
	.driver = {
		.name = CAM_CSID_DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = cam_ife_csid_dt_match,
		.suppress_bind_attrs = true,
	},
};

int cam_ife_csid_init_module(void)
{
	return platform_driver_register(&cam_ife_csid_driver);
}

void cam_ife_csid_exit_module(void)
{
	platform_driver_unregister(&cam_ife_csid_driver);
}

MODULE_DESCRIPTION("CAM IFE_CSID driver");
MODULE_LICENSE("GPL v2");
