// SPDX-License-Identifier: GPL-2.0-only
/*
 * Goodix Touchscreen Driver
 * Copyright (C) 2020 - 2021 Goodix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the GOODiX's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/fs.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#if KERNEL_VERSION(2, 6, 38) <= LINUX_VERSION_CODE
#include <linux/input/mt.h>
#define INPUT_TYPE_B_PROTOCOL
#endif

#include "goodix_ts_core.h"

#define GOODIX_DEFAULT_CFG_NAME "goodix_cfg_group.cfg"

int g_pdev_id;

static int goodix_send_ic_config(struct goodix_ts_core *cd, int type);

/* show driver information */
static ssize_t driver_info_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "DriverVersion:%s\n",
			GOODIX_DRIVER_VERSION);
}

/* show chip infoamtion */
static ssize_t chip_info_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct goodix_ts_core *cd = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_fw_version chip_ver;
	struct goodix_ic_info ic_info;
	u8 temp_pid[8] = { 0 };
	int ret;
	int cnt = -EINVAL;

	if (hw_ops->read_version) {
		ret = hw_ops->read_version(cd, &chip_ver);
		if (!ret) {
			memcpy(temp_pid, chip_ver.rom_pid,
			       sizeof(chip_ver.rom_pid));
			cnt = scnprintf(&buf[0], PAGE_SIZE,
				       "rom_pid:%s\nrom_vid:%02x%02x%02x\n",
				       temp_pid, chip_ver.rom_vid[0],
				       chip_ver.rom_vid[1],
				       chip_ver.rom_vid[2]);
			cnt += scnprintf(
				&buf[cnt], PAGE_SIZE,
				"patch_pid:%s\npatch_vid:%02x%02x%02x%02x\n",
				chip_ver.patch_pid, chip_ver.patch_vid[0],
				chip_ver.patch_vid[1], chip_ver.patch_vid[2],
				chip_ver.patch_vid[3]);
			cnt += scnprintf(&buf[cnt], PAGE_SIZE, "sensorid:%d\n",
					chip_ver.sensor_id);
		}
	}

	if (hw_ops->get_ic_info) {
		ret = hw_ops->get_ic_info(cd, &ic_info);
		if (!ret) {
			cnt += scnprintf(&buf[cnt], PAGE_SIZE, "config_id:%x\n",
					ic_info.version.config_id);
			cnt += scnprintf(&buf[cnt], PAGE_SIZE,
					"config_version:%x\n",
					ic_info.version.config_version);
		}
	}

	return cnt;
}

/* reset chip */
static ssize_t reset_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;

	if (!buf || count <= 0)
		return -EINVAL;
	if (buf[0] != '0')
		hw_ops->reset(core_data, GOODIX_NORMAL_RESET_DELAY_MS);
	return count;
}

/* read config */
static ssize_t read_cfg_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret;
	int i;
	int offset;
	char *cfg_buf = NULL;

	cfg_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!cfg_buf)
		return -ENOMEM;

	if (hw_ops->read_config)
		ret = hw_ops->read_config(core_data, cfg_buf, PAGE_SIZE);
	else
		ret = -EINVAL;

	if (ret > 0) {
		offset = 0;
		for (i = 0; i < 200; i++) { // only print 200 bytes
			offset += scnprintf(&buf[offset], PAGE_SIZE - offset,
					   "%02x,", cfg_buf[i]);
			if ((i + 1) % 20 == 0)
				buf[offset++] = '\n';
		}
	}

	kfree(cfg_buf);
	if (ret <= 0)
		return ret;

	return offset;
}

static u8 ascii2hex(u8 a)
{
	s8 value = 0;

	if (a >= '0' && a <= '9')
		value = a - '0';
	else if (a >= 'A' && a <= 'F')
		value = a - 'A' + 0x0A;
	else if (a >= 'a' && a <= 'f')
		value = a - 'a' + 0x0A;
	else
		value = 0xff;

	return value;
}

static int goodix_ts_convert_0x_data(const u8 *buf, int buf_size, u8 *out_buf,
				     int *out_buf_len)
{
	int i, m_size = 0;
	int temp_index = 0;
	u8 high, low;

	for (i = 0; i < buf_size; i++) {
		if (buf[i] == 'x' || buf[i] == 'X')
			m_size++;
	}

	if (m_size <= 1)
		return -EINVAL;

	*out_buf_len = m_size;

	for (i = 0; i < buf_size; i++) {
		if (buf[i] != 'x' && buf[i] != 'X')
			continue;

		if (temp_index >= m_size)
			return -EINVAL;

		high = ascii2hex(buf[i + 1]);
		low = ascii2hex(buf[i + 2]);
		if (high == 0xff || low == 0xff)
			return -EINVAL;

		out_buf[temp_index++] = (high << 4) + low;
	}
	return 0;
}

/* send config */
static ssize_t send_cfg_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct goodix_ts_core *cd = dev_get_drvdata(dev);
	struct device *device = cd->bus->dev;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_ic_config *config = NULL;
	const struct firmware *cfg_img = NULL;
	int ret;

	if (buf[0] != '1')
		return -EINVAL;

	hw_ops->irq_enable(cd, false);

	ret = request_firmware(&cfg_img, GOODIX_DEFAULT_CFG_NAME, dev);
	if (ret < 0) {
		ts_err(device, "cfg file [%s] not available,errno:%d",
		       GOODIX_DEFAULT_CFG_NAME, ret);
		goto exit;
	} else {
		ts_info(device, "cfg file [%s] is ready", GOODIX_DEFAULT_CFG_NAME);
	}

	config = kzalloc(sizeof(*config), GFP_KERNEL);
	if (!config)
		goto exit;

	if (goodix_ts_convert_0x_data(cfg_img->data, cfg_img->size,
				      config->data, &config->len)) {
		ts_err(device, "convert config data FAILED");
		goto exit;
	}

	if (hw_ops->send_config) {
		ret = hw_ops->send_config(cd, config->data, config->len);
		if (ret < 0)
			ts_err(device, "send config failed");
	}

exit:
	hw_ops->irq_enable(cd, true);
	kfree(config);
	if (cfg_img)
		release_firmware(cfg_img);

	return count;
}

/* reg read/write */
static u32 rw_addr;
static u32 rw_len;
static u8 rw_flag;
static u8 store_buf[32];
static u8 show_buf[PAGE_SIZE];
static ssize_t reg_rw_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct goodix_ts_core *cd = dev_get_drvdata(dev);
	struct device *device = cd->bus->dev;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	int ret;

	if (!rw_addr || !rw_len) {
		ts_err(device, "address(0x%x) and length(%d) can't be null", rw_addr,
		       rw_len);
		return -EINVAL;
	}

	if (rw_flag != 1) {
		ts_err(device, "invalid rw flag %d, only support [1/2]", rw_flag);
		return -EINVAL;
	}

	ret = hw_ops->read(cd, rw_addr, show_buf, rw_len);
	if (ret < 0) {
		ts_err(device, "failed read addr(%x) length(%d)", rw_addr, rw_len);
		return scnprintf(buf, PAGE_SIZE,
				"failed read addr(%x), len(%d)\n", rw_addr,
				rw_len);
	}

	return scnprintf(buf, PAGE_SIZE, "0x%x,%d {%*ph}\n", rw_addr, rw_len,
			rw_len, show_buf);
}

static ssize_t reg_rw_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct goodix_ts_core *cd = dev_get_drvdata(dev);
	struct device *device = cd->bus->dev;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	char *pos = NULL;
	char *token = NULL;
	long result = 0;
	int ret;
	int i;

	if (!buf || !count) {
		ts_err(device, "invalid parame");
		goto err_out;
	}

	if (buf[0] == 'r') {
		rw_flag = 1;
	} else if (buf[0] == 'w') {
		rw_flag = 2;
	} else {
		ts_err(device, "string must start with 'r/w'");
		goto err_out;
	}

	/* get addr */
	pos = (char *)buf;
	pos += 2;
	token = strsep(&pos, ":");
	if (!token) {
		ts_err(device, "invalid address info");
		goto err_out;
	} else {
		if (kstrtol(token, 16, &result)) {
			ts_err(device, "failed get addr info");
			goto err_out;
		}
		rw_addr = (u32)result;
		ts_info(device, "rw addr is 0x%x", rw_addr);
	}

	/* get length */
	token = strsep(&pos, ":");
	if (!token) {
		ts_err(device, "invalid length info");
		goto err_out;
	} else {
		if (kstrtol(token, 0, &result)) {
			ts_err(device, "failed get length info");
			goto err_out;
		}
		rw_len = (u32)result;
		ts_info(device, "rw length info is %d", rw_len);
		if (rw_len > sizeof(store_buf)) {
			ts_err(device, "data len > %lu", sizeof(store_buf));
			goto err_out;
		}
	}

	if (rw_flag == 1)
		return count;

	for (i = 0; i < rw_len; i++) {
		token = strsep(&pos, ":");
		if (!token) {
			ts_err(device, "invalid data info");
			goto err_out;
		} else {
			if (kstrtol(token, 16, &result)) {
				ts_err(device, "failed get data[%d] info", i);
				goto err_out;
			}
			store_buf[i] = (u8)result;
			ts_info(device, "get data[%d]=0x%x", i, store_buf[i]);
		}
	}
	ret = hw_ops->write(cd, rw_addr, store_buf, rw_len);
	if (ret < 0) {
		ts_err(device, "failed write addr(%x) data %*ph", rw_addr, rw_len,
		       store_buf);
		goto err_out;
	}

	ts_info(device, "%s write to addr (%x) with data %*ph", "success", rw_addr,
		rw_len, store_buf);

	return count;
err_out:
	scnprintf(show_buf, PAGE_SIZE, "%s\n",
		 "invalid params, format{r/w:4100:length:[41:21:31]}");
	return -EINVAL;
}

/* show irq information */
static ssize_t irq_info_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct irq_desc *desc;
	size_t offset = 0;
	int r;

	r = scnprintf(&buf[offset], PAGE_SIZE, "irq:%u\n", core_data->irq);
	if (r < 0)
		return -EINVAL;

	offset += r;
	r = scnprintf(&buf[offset], PAGE_SIZE - offset, "state:%s\n",
		     atomic_read(&core_data->irq_enabled) ? "enabled" :
							    "disabled");
	if (r < 0)
		return -EINVAL;

	desc = irq_to_desc(core_data->irq);
	offset += r;
	r = scnprintf(&buf[offset], PAGE_SIZE - offset, "disable-depth:%d\n",
		     desc->depth);
	if (r < 0)
		return -EINVAL;

	offset += r;
	r = scnprintf(&buf[offset], PAGE_SIZE - offset, "trigger-count:%zu\n",
		     core_data->irq_trig_cnt);
	if (r < 0)
		return -EINVAL;

	offset += r;
	r = scnprintf(&buf[offset], PAGE_SIZE - offset,
		     "echo 0/1 > irq_info to disable/enable irq\n");
	if (r < 0)
		return -EINVAL;

	offset += r;
	return offset;
}

/* enable/disable irq */
static ssize_t irq_info_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;

	if (!buf || count <= 0)
		return -EINVAL;

	if (buf[0] != '0')
		hw_ops->irq_enable(core_data, true);
	else
		hw_ops->irq_enable(core_data, false);
	return count;
}

/* show esd status */
static ssize_t esd_info_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_esd *ts_esd = &core_data->ts_esd;
	int r = 0;

	r = scnprintf(buf, PAGE_SIZE, "state:%s\n",
		     atomic_read(&ts_esd->esd_on) ? "enabled" : "disabled");

	return r;
}

/* enable/disable esd */
static ssize_t esd_info_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct goodix_ts_core *cd = dev_get_drvdata(dev);

	if (!buf || count <= 0)
		return -EINVAL;

	if (buf[0] != '0')
		goodix_ts_esd_on(cd);
	else
		goodix_ts_esd_off(cd);
	return count;
}

/* debug level show */
static ssize_t debug_log_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int r = 0;

	r = scnprintf(buf, PAGE_SIZE, "state:%s\n",
		     debug_log_flag ? "enabled" : "disabled");

	return r;
}

/* debug level store */
static ssize_t debug_log_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	if (!buf || count <= 0)
		return -EINVAL;

	if (buf[0] != '0')
		debug_log_flag = true;
	else
		debug_log_flag = false;
	return count;
}

/* show die package site and mcu fabs */
#define DIE_INFO_START_FLASH_ADDR 0x1F300
static ssize_t die_info_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct goodix_ts_core *cd = dev_get_drvdata(dev);
	struct device *device = cd->bus->dev;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	u8 temp_buf[21];
	u8 pkg_site;
	u8 mcu_fab;
	int ret;

	ret = hw_ops->read_flash(cd, DIE_INFO_START_FLASH_ADDR, temp_buf,
				 sizeof(temp_buf));
	if (ret < 0) {
		ts_err(device, "read flash failed");
		return 0;
	}

	ts_info(device, "die info:%*ph", (int)sizeof(temp_buf), temp_buf);

	pkg_site = temp_buf[1];
	mcu_fab = temp_buf[20];
	ret = scnprintf(buf, PAGE_SIZE, "package_id:0x%02X mcu_fab:0x%02X\n",
		       pkg_site, mcu_fab);

	return ret;
}

static DEVICE_ATTR_RO(driver_info);
static DEVICE_ATTR_RO(chip_info);
static DEVICE_ATTR_WO(reset);
static DEVICE_ATTR_WO(send_cfg);
static DEVICE_ATTR_RO(read_cfg);
static DEVICE_ATTR_RW(reg_rw);
static DEVICE_ATTR_RW(irq_info);
static DEVICE_ATTR_RW(esd_info);
static DEVICE_ATTR_RW(debug_log);
static DEVICE_ATTR_RO(die_info);

static struct attribute *sysfs_attrs[] = {
	&dev_attr_driver_info.attr,
	&dev_attr_chip_info.attr,
	&dev_attr_reset.attr,
	&dev_attr_send_cfg.attr,
	&dev_attr_read_cfg.attr,
	&dev_attr_reg_rw.attr,
	&dev_attr_irq_info.attr,
	&dev_attr_esd_info.attr,
	&dev_attr_debug_log.attr,
	&dev_attr_die_info.attr,
	NULL,
};

static const struct attribute_group sysfs_group = {
	.attrs = sysfs_attrs,
};

static int goodix_ts_sysfs_init(struct goodix_ts_core *cd)
{
	struct device *dev = cd->bus->dev;
	int ret;

	ret = sysfs_create_group(&cd->pdev->dev.kobj, &sysfs_group);
	if (ret) {
		ts_err(dev, "failed create core sysfs group");
		return ret;
	}

	goodix_ts_replay_init(cd);

	return ret;
}

static void goodix_ts_sysfs_exit(struct goodix_ts_core *cd)
{
	goodix_ts_replay_exit(cd);
	sysfs_remove_group(&cd->pdev->dev.kobj, &sysfs_group);
}

static char *output_buf;
static uint32_t output_offset;
static uint32_t cap_count = 10;
static int get_raw_data(struct goodix_ts_core *cd, u8 type)
{
	struct device *dev = cd->bus->dev;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_ts_cmd temp_cmd;
	int tx = cd->ic_info.parm.drv_num;
	int rx = cd->ic_info.parm.sen_num;
	static u8 frame_buf[10000];
	u8 *frame_ptr = frame_buf;
	s16 *raw_ptr;
	u32 flag_addr = 0;
	u32 raw_addr = 0;
	u32 offset = 0;
	int frame_len;
	u8 pkg_num;
	u16 pkg_type;
	u16 pkg_len;
	int i, j;
	int retry;
	bool is_frame_arch = false;

	if (cd->bus->ic_type == IC_TYPE_BERLIN_D ||
	    cd->bus->ic_type == IC_TYPE_NOTTINGHAM ||
	    cd->bus->ic_type == IC_TYPE_MARSEILLE ||
	    cd->bus->ic_type == IC_TYPE_ATB ||
	    !memcmp(cd->fw_version.patch_pid, "9966", 4)) {
		is_frame_arch = true;
		flag_addr = cd->ic_info.misc.frame_data_addr;
		if (type == 0) {
			temp_cmd.cmd = 0x90;
			temp_cmd.data[0] = 0x81;
			temp_cmd.data[1] = 0x01;
			temp_cmd.len = 6;
		} else {
			temp_cmd.cmd = 0x90;
			temp_cmd.data[0] = 0x82;
			temp_cmd.data[1] = 0x01;
			temp_cmd.len = 6;
		}
	} else {
		temp_cmd.cmd = 0x01;
		temp_cmd.len = 4;
		flag_addr = cd->ic_info.misc.touch_data_addr;
		if (type == 0)
			raw_addr = cd->ic_info.misc.mutual_rawdata_addr;
		else
			raw_addr = cd->ic_info.misc.mutual_diffdata_addr;
	}

	output_buf = vmalloc(cap_count * 8192);
	if (!output_buf)
		return -ENOMEM;

	/* disable irq & close esd */
	hw_ops->irq_enable(cd, false);
	goodix_ts_esd_off(cd);

	/* switch rawdata mode */
	hw_ops->send_cmd(cd, &temp_cmd);

	for (i = 0; i < cap_count; i++) {
		/* clean touch event flag */
		frame_buf[0] = 0;
		hw_ops->write(cd, flag_addr, frame_buf, 1);
		retry = 20;
		while (retry--) {
			usleep_range(5000, 5500);
			hw_ops->read(cd, flag_addr, frame_buf, 16);
			if (frame_buf[0] == 0x80)
				break;
		}
		if (retry < 0) {
			ts_err(dev, "rawdata is not ready val:0x%02x, exit!", frame_buf[0]);
			goto exit;
		}

		if (is_frame_arch) {
			if (checksum_cmp(frame_buf, 16, CHECKSUM_MODE_U8_LE)) {
				ts_err(dev, "frame head checksum error, %*ph", 16, frame_buf);
				goto exit;
			}

			frame_len = le16_to_cpup((__le16 *)&frame_buf[3]);
			hw_ops->read(cd, flag_addr, (u8 *)frame_buf, frame_len);
			// if (checksum_cmp(frame_buf, frame_len, CHECKSUM_MODE_U16_LE)) {
			//	ts_err(dev, "frame body checksum error");
			//	goto exit;
			// }

			if (frame_buf[13] > 0) { //new framedata format
				pkg_num = frame_buf[10];
				frame_ptr += 16;
				for (j = 0; j < pkg_num; j++) {
					pkg_len = le16_to_cpup((__le16 *)&frame_ptr[0]);
					pkg_type = le16_to_cpup((__le16 *)&frame_ptr[2]);
					if (pkg_type == 10 || pkg_type == 11) {
						raw_ptr = (s16 *)&frame_ptr[4 + 8];
						break;
					}
					frame_ptr += pkg_len;
				}
			} else {
				offset = cd->ic_info.misc.frame_data_head_len +
					cd->ic_info.misc.fw_attr_len +
					cd->ic_info.misc.fw_log_len + 8;
				raw_ptr = (s16 *)(frame_ptr + offset);
			}
		} else {
			hw_ops->read(cd, raw_addr, (u8 *)frame_buf, tx * rx * sizeof(s16));
			raw_ptr = (s16 *)frame_buf;
		}

		goodix_rotate_abcd2cbad(tx, rx, raw_ptr);
		output_offset += scnprintf(&output_buf[output_offset],
			cap_count * 8192 - output_offset, "count:%d\n", i);
		for (j = 0; j < tx * rx; j++) {
			output_offset += scnprintf(&output_buf[output_offset],
				cap_count * 8192 - output_offset, "%5d,", raw_ptr[j]);
			if ((j + 1) % tx == 0)
				output_offset += scnprintf(&output_buf[output_offset],
					cap_count * 8192 - output_offset, "\n");
		}
	}

exit:
	/* switch coor mode */
	if (cd->bus->ic_type == IC_TYPE_BERLIN_D ||
	    cd->bus->ic_type == IC_TYPE_NOTTINGHAM ||
	    cd->bus->ic_type == IC_TYPE_MARSEILLE ||
	    cd->bus->ic_type == IC_TYPE_ATB ||
	    !memcmp(cd->fw_version.patch_pid, "9966", 4)) {
		temp_cmd.cmd = 0x90;
		temp_cmd.data[0] = 0x00;
		temp_cmd.len = 5;
	} else {
		temp_cmd.cmd = 0x00;
		temp_cmd.len = 4;
	}

	hw_ops->send_cmd(cd, &temp_cmd);
	/* clean touch event flag */
	frame_buf[0] = 0;
	hw_ops->write(cd, flag_addr, frame_buf, 1);
	/* enable irq & esd */
	hw_ops->irq_enable(cd, true);
	goodix_ts_esd_on(cd);
	return 0;
}

static int tp_rawdata_open(struct inode *inode, struct file *file)
{
	struct goodix_ts_core *cd =
#if (KERNEL_VERSION(5, 17, 0) <= LINUX_VERSION_CODE)
	(struct goodix_ts_core *)pde_data(inode);
#else
	(struct goodix_ts_core *)PDE_DATA(inode);
#endif

	return get_raw_data(cd, 0);
}

static int tp_diffdata_open(struct inode *inode, struct file *file)
{
	struct goodix_ts_core *cd =
#if (KERNEL_VERSION(5, 17, 0) <= LINUX_VERSION_CODE)
	(struct goodix_ts_core *)pde_data(inode);
#else
	(struct goodix_ts_core *)PDE_DATA(inode);
#endif

	return get_raw_data(cd, 1);
}

static ssize_t tp_data_read(struct file *file, char __user *user_buffer,
			      size_t count, loff_t *offset)
{
	ssize_t ret;
	size_t remaining_size = output_offset - (size_t)(*offset);
	size_t readable_size = remaining_size < count ? remaining_size : count;

	if (readable_size == 0)
		return 0;

	if (copy_to_user(user_buffer, output_buf + *offset, readable_size)) {
		ts_err(NULL, "copy to user failed, len: %zu", readable_size);
		return -EFAULT;
	}

	*offset += readable_size;
	ret = readable_size;
	return ret;
}

static int tp_data_release(struct inode *inode, struct file *file)
{
	vfree(output_buf);
	output_offset = 0;
	return 0;
}

#if (KERNEL_VERSION(5, 6, 0) <= LINUX_VERSION_CODE)
static const struct proc_ops raw_proc_fops = {
	.proc_open = tp_rawdata_open,
	.proc_read = tp_data_read,
	.proc_release = tp_data_release,
};

static const struct proc_ops diff_proc_fops = {
	.proc_open = tp_diffdata_open,
	.proc_read = tp_data_read,
	.proc_release = tp_data_release,
};
#else
static const struct file_operations raw_proc_fops = {
	.open = tp_rawdata_open,
	.read = tp_data_read,
	.release = tp_data_release,
};

static const struct file_operations diff_proc_fops = {
	.open = tp_diffdata_open,
	.read = tp_data_read,
	.release = tp_data_release,
};
#endif

static void goodix_ts_procfs_init(struct goodix_ts_core *cd)
{
	int dev_id = cd->pdev->id;
	struct device *dev = cd->bus->dev;
	char proc_node[32] = { 0 };

	if (dev_id == 0)
		snprintf(proc_node, sizeof(proc_node), "goodix_ts");
	else
		snprintf(proc_node, sizeof(proc_node), "goodix_ts.%d", dev_id);
	cd->proc_dir_entry = proc_mkdir(proc_node, NULL);
	if (!cd->proc_dir_entry)
		return;

	if (proc_create_data("tp_raw", 0664,
			     cd->proc_dir_entry, &raw_proc_fops,
			     cd) == NULL) {
		ts_err(dev, "failed to create raw proc entry");
	}

	if (proc_create_data("tp_diff", 0664,
			     cd->proc_dir_entry, &diff_proc_fops,
			     cd) == NULL) {
		ts_err(dev, "failed to create diff proc entry");
	}
}

static void goodix_ts_procfs_exit(struct goodix_ts_core *cd)
{
	remove_proc_entry("tp_raw", cd->proc_dir_entry);
	remove_proc_entry("tp_diff", cd->proc_dir_entry);
	proc_remove(cd->proc_dir_entry);
}

#if IS_ENABLED(CONFIG_OF)

#ifdef CONFIG_DRM_PANEL_NOTIFY
static int goodix_check_panel_dt(struct device_node *np,
				      struct goodix_ts_board_data *board_data)
{
	int i;
	int count;
	struct device_node *node;
	struct drm_panel *panel;

	count = of_count_phandle_with_args(np, "panel", NULL);
	if (count <= 0)
		return 0;

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "panel", i);
		panel = of_drm_find_panel(node);
		of_node_put(node);
		if (!IS_ERR(panel)) {
			board_data->active_panel = panel;
			return 0;
		}
	}

	return -ENODEV;
}
#endif
/**
 * goodix_parse_dt_resolution - parse resolution from dt
 * @node: devicetree node
 * @board_data: pointer to board data structure
 * return: 0 - no error, <0 error
 */
static int goodix_parse_dt_resolution(struct device *dev,
				      struct goodix_ts_board_data *board_data)
{
	struct device_node *node = dev->of_node;
	int ret;

	ret = of_property_read_u32(node, "goodix,panel-max-x",
				   &board_data->panel_max_x);
	if (ret) {
		ts_err(dev, "failed get panel-max-x");
		return ret;
	}

	ret = of_property_read_u32(node, "goodix,panel-max-y",
				   &board_data->panel_max_y);
	if (ret) {
		ts_err(dev, "failed get panel-max-y");
		return ret;
	}

	ret = of_property_read_u32(node, "goodix,panel-max-w",
				   &board_data->panel_max_w);
	if (ret) {
		ts_err(dev, "failed get panel-max-w");
		return ret;
	}

	ret = of_property_read_u32(node, "goodix,panel-max-p",
				   &board_data->panel_max_p);
	if (ret) {
		ts_err(dev, "failed get panel-max-p, use default");
		board_data->panel_max_p = GOODIX_PEN_MAX_PRESSURE;
	}

	return 0;
}

/**
 * goodix_parse_dt - parse board data from dt
 * @dev: pointer to device
 * @board_data: pointer to board data structure
 * return: 0 - no error, <0 error
 */
static int goodix_parse_dt(struct device *dev,
			   struct goodix_ts_board_data *board_data)
{
	struct device_node *node = dev->of_node;
	const char *name_tmp;
	int r;

	if (!board_data) {
		ts_err(dev, "invalid board data");
		return -EINVAL;
	}

	r = of_get_named_gpio(node, "goodix,avdd-gpio", 0);
	if (r < 0) {
		ts_info(dev, "can't find avdd-gpio, use other power supply");
		board_data->avdd_gpio = 0;
	} else {
		ts_info(dev, "get avdd-gpio[%d] from dt", r);
		board_data->avdd_gpio = r;
	}

	r = of_get_named_gpio(node, "goodix,iovdd-gpio", 0);
	if (r < 0) {
		ts_info(dev, "can't find iovdd-gpio, use other power supply");
		board_data->iovdd_gpio = 0;
	} else {
		ts_info(dev, "get iovdd-gpio[%d] from dt", r);
		board_data->iovdd_gpio = r;
	}

	r = of_get_named_gpio(node, "goodix,reset-gpio", 0);
	if (r < 0) {
		ts_err(dev, "invalid reset-gpio in dt: %d", r);
		return -EINVAL;
	}
	ts_info(dev, "get reset-gpio[%d] from dt", r);
	board_data->reset_gpio = r;

	r = of_get_named_gpio(node, "goodix,irq-gpio", 0);
	if (r < 0) {
		ts_err(dev, "invalid irq-gpio in dt: %d", r);
		return -EINVAL;
	}
	ts_info(dev, "get irq-gpio[%d] from dt", r);
	board_data->irq_gpio = r;

	r = of_property_read_u32(node, "goodix,irq-flags",
				 &board_data->irq_flags);
	if (r) {
		ts_err(dev, "invalid irq-flags");
		return -EINVAL;
	}

	memset(board_data->avdd_name, 0, sizeof(board_data->avdd_name));
	r = of_property_read_string(node, "goodix,avdd-name", &name_tmp);
	if (!r) {
		ts_info(dev, "avdd name from dt: %s", name_tmp);
		if (strlen(name_tmp) < sizeof(board_data->avdd_name))
			strscpy(board_data->avdd_name, name_tmp,
				sizeof(board_data->avdd_name));
		else
			ts_info(dev, "invalied avdd name length: %ld > %ld",
				strlen(name_tmp),
				sizeof(board_data->avdd_name));
	}

	memset(board_data->iovdd_name, 0, sizeof(board_data->iovdd_name));
	r = of_property_read_string(node, "goodix,iovdd-name", &name_tmp);
	if (!r) {
		ts_info(dev, "iovdd name from dt: %s", name_tmp);
		if (strlen(name_tmp) < sizeof(board_data->iovdd_name))
			strscpy(board_data->iovdd_name, name_tmp,
				sizeof(board_data->iovdd_name));
		else
			ts_info(dev, "invalied iovdd name length: %ld > %ld",
				strlen(name_tmp),
				sizeof(board_data->iovdd_name));
	}

	/* get firmware file name */
	r = of_property_read_string(node, "goodix,firmware-name", &name_tmp);
	if (!r) {
		ts_info(dev, "firmware name from dt: %s", name_tmp);
		strscpy(board_data->fw_name, name_tmp,
			sizeof(board_data->fw_name));
	} else {
		ts_info(dev, "can't find firmware name, use default: %s",
			TS_DEFAULT_FIRMWARE);
		strscpy(board_data->fw_name, TS_DEFAULT_FIRMWARE,
			sizeof(board_data->fw_name));
	}

	/* get config file name */
	r = of_property_read_string(node, "goodix,config-name", &name_tmp);
	if (!r) {
		ts_info(dev, "config name from dt: %s", name_tmp);
		strscpy(board_data->cfg_bin_name, name_tmp,
			sizeof(board_data->cfg_bin_name));
	} else {
		ts_info(dev, "can't find config name, use default: %s",
			TS_DEFAULT_CFG_BIN);
		strscpy(board_data->cfg_bin_name, TS_DEFAULT_CFG_BIN,
			sizeof(board_data->cfg_bin_name));
	}

#ifdef CONFIG_DRM_PANEL_NOTIFY
	r = goodix_check_panel_dt(node, board_data);
	if (r)
		ts_err(dev, "check panel dt failed");
#endif

	/* get xyz resolutions */
	r = goodix_parse_dt_resolution(dev, board_data);
	if (r) {
		ts_err(dev, "Failed to parse resolutions:%d", r);
		return r;
	}

	/* get sleep mode flag */
	board_data->sleep_enable =
		of_property_read_bool(node, "goodix,sleep-enable");

	/*get pen-enable switch and pen keys, must after "key map"*/
	board_data->pen_enable =
		of_property_read_bool(node, "goodix,pen-enable");

	/* get esd enable flag */
	board_data->esd_enable =
		of_property_read_bool(node, "goodix,esd-enable");

	ts_info(dev, "[DT]x:%d, y:%d, w:%d, p:%d sleep_enable:%d pen_enable:%d esd_enable:%d",
		board_data->panel_max_x, board_data->panel_max_y,
		board_data->panel_max_w, board_data->panel_max_p,
		board_data->sleep_enable, board_data->pen_enable,
		board_data->esd_enable);
	return 0;
}
#endif

static void goodix_ts_report_pen(struct goodix_ts_core *cd,
				 struct goodix_pen_data *pen_data)
{
	struct device *device = cd->bus->dev;
	struct input_dev *dev = cd->pen_dev;
	struct goodix_pen_data *pre_pen = &cd->ts_event.pre_pen;
	int i;

	if (memcmp(pre_pen, pen_data, sizeof(*pre_pen)) == 0)
		return;
	memcpy(pre_pen, pen_data, sizeof(*pre_pen));

	mutex_lock(&dev->mutex);

	if (pen_data->coords.status == TS_TOUCH) {
		input_report_abs(dev, ABS_X, pen_data->coords.x);
		input_report_abs(dev, ABS_Y, pen_data->coords.y);
		input_report_abs(dev, ABS_PRESSURE, pen_data->coords.p);
		if (pen_data->coords.p == 0)
			input_report_abs(dev, ABS_DISTANCE, 1);
		else
			input_report_abs(dev, ABS_DISTANCE, 0);
		input_report_abs(dev, ABS_TILT_X, pen_data->coords.tilt_x);
		input_report_abs(dev, ABS_TILT_Y, pen_data->coords.tilt_y);
		input_report_key(dev, BTN_TOOL_PEN, 1);
		input_report_key(dev, BTN_TOUCH, pen_data->is_hover ? 0 : 1);
		ts_debug(device,
			"pen_data:x %d, y %d, p %d, tilt_x %d tilt_y %d key[%d %d]",
			pen_data->coords.x, pen_data->coords.y,
			pen_data->coords.p, pen_data->coords.tilt_x,
			pen_data->coords.tilt_y,
			pen_data->keys[0].status == TS_TOUCH ? 1 : 0,
			pen_data->keys[1].status == TS_TOUCH ? 1 : 0);
	} else {
		input_report_key(dev, BTN_TOUCH, 0);
		input_report_key(dev, BTN_TOOL_PEN, 0);
	}
	/* report pen button */
	for (i = 0; i < GOODIX_MAX_PEN_KEY; i++) {
		if (pen_data->keys[i].status == TS_TOUCH)
			input_report_key(dev, pen_data->keys[i].code, 1);
		else
			input_report_key(dev, pen_data->keys[i].code, 0);
	}

	input_sync(dev);
	mutex_unlock(&dev->mutex);
}

#ifdef INPUT_TYPE_B_PROTOCOL
static void goodix_ts_report_finger(struct goodix_ts_core *cd,
				    struct goodix_touch_data *touch_data)
{
	struct device *device = cd->bus->dev;
	struct input_dev *dev = cd->input_dev;
	unsigned int touch_num = touch_data->touch_num;
	struct goodix_touch_data *pre_touch = &cd->ts_event.pre_touch;
	int i;

	if (memcmp(pre_touch, touch_data, sizeof(*pre_touch)) == 0)
		return;
	memcpy(pre_touch, touch_data, sizeof(*pre_touch));

	mutex_lock(&dev->mutex);

	for (i = 0; i < GOODIX_MAX_TOUCH; i++) {
		if (touch_data->coords[i].status == TS_TOUCH) {
			ts_debug(device, "report: id[%d], x %d, y %d, w %d", i,
				 touch_data->coords[i].x,
				 touch_data->coords[i].y,
				 touch_data->coords[i].w);
			input_mt_slot(dev, i);
			input_mt_report_slot_state(dev, MT_TOOL_FINGER, true);
			input_report_abs(dev, ABS_MT_POSITION_X,
					 touch_data->coords[i].x);
			input_report_abs(dev, ABS_MT_POSITION_Y,
					 touch_data->coords[i].y);
			input_report_abs(dev, ABS_MT_TOUCH_MAJOR,
					 touch_data->coords[i].w);
		} else {
			input_mt_slot(dev, i);
			input_mt_report_slot_state(dev, MT_TOOL_FINGER, false);
		}
	}

	for (i = 0; i < GOODIX_MAX_KEY; i++) {
		if (touch_data->keys[i].status == TS_TOUCH)
			input_report_key(dev, touch_data->keys[i].code, 1);
		else
			input_report_key(dev, touch_data->keys[i].code, 0);
	}

	input_report_key(dev, BTN_TOUCH, touch_num > 0 ? 1 : 0);
	input_report_key(dev, BTN_TOOL_FINGER, touch_num > 0 ? 1 : 0);
	input_sync(dev);

	mutex_unlock(&dev->mutex);
}
#else
static void goodix_ts_report_a(struct goodix_ts_core *cd,
		struct goodix_touch_data *touch_data)
{
	struct input_dev *dev = cd->input_dev;
	unsigned int touch_num = touch_data->touch_num;
	int i;
	int abs = -1;

	for (i = 0; i < GOODIX_MAX_TOUCH; i++) {
		if (touch_data->coords[i].status == TS_TOUCH) {
			input_report_abs(dev, ABS_MT_POSITION_X,
					 touch_data->coords[i].x);
			input_report_abs(dev, ABS_MT_POSITION_Y,
					 touch_data->coords[i].y);
			input_report_abs(dev, ABS_MT_TOUCH_MAJOR,
					 touch_data->coords[i].w);
			input_mt_sync(dev);
			if (abs == -1)
				abs = i;
		}
	}

	input_report_key(dev, BTN_TOUCH, touch_num > 0 ? 1 : 0);
	input_report_key(dev, BTN_TOOL_FINGER, touch_num > 0 ? 1 : 0);

	if (abs >= 0) {
		input_report_abs(dev, ABS_X, touch_data->coords[abs].x);
		input_report_abs(dev, ABS_Y, touch_data->coords[abs].y);
	}

	input_sync(dev);
}
#endif

static int goodix_ts_request_handle(struct goodix_ts_core *cd,
				    struct goodix_ts_event *ts_event)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct device *dev = cd->bus->dev;
	int ret = -1;

	if (ts_event->request_code == REQUEST_TYPE_CONFIG)
		ret = goodix_send_ic_config(cd, CONFIG_TYPE_NORMAL);
	else if (ts_event->request_code == REQUEST_TYPE_RESET)
		ret = hw_ops->reset(cd, GOODIX_NORMAL_RESET_DELAY_MS);
	else
		ts_info(dev, "can not handle request type 0x%x",
			ts_event->request_code);
	if (ret)
		ts_err(dev, "failed handle request 0x%x", ts_event->request_code);
	else
		ts_info(dev, "success handle ic request 0x%x",
			ts_event->request_code);
	return ret;
}

/**
 * goodix_ts_threadirq_func - Bottom half of interrupt
 * This functions is excuted in thread context,
 * sleep in this function is permit.
 *
 * @data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static irqreturn_t goodix_ts_threadirq_func(int irq, void *data)
{
	struct goodix_ts_core *core_data = data;
	struct device *dev = core_data->bus->dev;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	struct goodix_ts_event *ts_event = &core_data->ts_event;
	struct goodix_ts_esd *ts_esd = &core_data->ts_esd;
	int ret;

	disable_irq_nosync(core_data->irq);
	ts_esd->irq_status = true;
	core_data->irq_trig_cnt++;

	/* read touch data from touch device */
	ret = hw_ops->event_handler(core_data, ts_event);
	if (likely(!ret)) {
		if (!atomic_read(&core_data->suspended)) { // coor mode
			if (ts_event->event_type & EVENT_TOUCH) {
				/* report touch */
#ifdef INPUT_TYPE_B_PROTOCOL
				goodix_ts_report_finger(core_data, &ts_event->touch_data);
#else
				goodix_ts_report_a(core_data, &ts_event->touch_data);
#endif
			}
			if (core_data->board_data->pen_enable &&
				ts_event->event_type & EVENT_PEN) {
				goodix_ts_report_pen(core_data, &ts_event->pen_data);
			}
		} else if (core_data->gesture_type > 0) { // gesture mode
			if (ts_event->event_type & EVENT_GESTURE) {
				goodix_ts_report_gesture(core_data, ts_event);
			} else {
				/* resend gesture cmd */
				ts_info(dev, "can't recv gesture event, resend gesture cmd");
				hw_ops->gesture(core_data, core_data->gesture_type);
			}
		}

		if (ts_event->event_type & EVENT_REQUEST)
			goodix_ts_request_handle(core_data, ts_event);
	}
#if (GOODIX_ENABLE_DUMP_DEV)
	goodix_get_dump_frame(core_data);
#endif
	goodix_ts_replay_record(core_data, ts_event);
	enable_irq(core_data->irq);
	return IRQ_HANDLED;
}

/**
 * goodix_ts_init_irq - Requset interrput line from system
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_irq_setup(struct goodix_ts_core *core_data)
{
	const struct goodix_ts_board_data *ts_bdata = core_data->board_data;
	struct device *dev = core_data->bus->dev;
	int ret;

	/* if ts_bdata-> irq is invalid */
	core_data->irq = gpio_to_irq(ts_bdata->irq_gpio);
	if (core_data->irq < 0) {
		ts_err(dev, "failed get irq num %d", core_data->irq);
		return -EINVAL;
	}

	ts_info(dev, "IRQ:%u,flags:%d", core_data->irq, (int)ts_bdata->irq_flags);
	ret = devm_request_threaded_irq(&core_data->pdev->dev, core_data->irq,
					NULL, goodix_ts_threadirq_func,
					ts_bdata->irq_flags | IRQF_ONESHOT,
					GOODIX_CORE_DRIVER_NAME, core_data);
	if (ret < 0)
		ts_err(dev, "Failed to requeset threaded irq:%d", ret);
	else
		atomic_set(&core_data->irq_enabled, 1);

	return ret;
}

/**
 * goodix_ts_power_init - Get regulator for touch device
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_power_init(struct goodix_ts_core *core_data)
{
	struct goodix_ts_board_data *ts_bdata = core_data->board_data;
	struct device *dev = core_data->bus->dev;
	int ret = 0;

	ts_info(dev, "Power init");
	if (strlen(ts_bdata->avdd_name)) {
		core_data->avdd = devm_regulator_get(dev, ts_bdata->avdd_name);
		if (IS_ERR_OR_NULL(core_data->avdd)) {
			ret = PTR_ERR(core_data->avdd);
			ts_err(dev, "Failed to get regulator avdd:%d", ret);
			core_data->avdd = NULL;
			return ret;
		}
	} else {
		ts_info(dev, "Avdd name is NULL");
	}

	if (strlen(ts_bdata->iovdd_name)) {
		core_data->iovdd =
			devm_regulator_get(dev, ts_bdata->iovdd_name);
		if (IS_ERR_OR_NULL(core_data->iovdd)) {
			ret = PTR_ERR(core_data->iovdd);
			ts_err(dev, "Failed to get regulator iovdd:%d", ret);
			core_data->iovdd = NULL;
		}
	} else {
		ts_info(dev, "iovdd name is NULL");
	}

	return ret;
}

/**
 * goodix_ts_power_on - Turn on power to the touch device
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_power_on(struct goodix_ts_core *cd)
{
	struct device *dev = cd->bus->dev;
	int ret = 0;

	ts_info(dev, "Device power on");
	if (cd->power_on)
		return 0;

	ret = cd->hw_ops->power_on(cd, true);
	if (!ret)
		cd->power_on = 1;
	else
		ts_err(dev, "failed power on, %d", ret);
	return ret;
}

/**
 * goodix_ts_power_off - Turn off power to the touch device
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_power_off(struct goodix_ts_core *cd)
{
	struct device *dev = cd->bus->dev;
	int ret;

	if (!cd->power_on)
		return 0;

	ts_info(dev, "Device power off");
	ret = cd->hw_ops->power_on(cd, false);
	if (!ret)
		cd->power_on = 0;
	else
		ts_err(dev, "failed power off, %d", ret);

	return ret;
}

/**
 * goodix_ts_gpio_setup - Request gpio resources from GPIO subsysten
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_gpio_setup(struct goodix_ts_core *core_data)
{
	struct device *dev = core_data->bus->dev;
	struct goodix_ts_board_data *ts_bdata = core_data->board_data;
	int r = 0;

	ts_info(dev, "GPIO setup,reset-gpio:%d, irq-gpio:%d", ts_bdata->reset_gpio,
		ts_bdata->irq_gpio);
	/*
	 * after kenerl3.13, gpio_ api is deprecated, new
	 * driver should use gpiod_ api.
	 */
	r = devm_gpio_request_one(&core_data->pdev->dev, ts_bdata->reset_gpio,
				  GPIOF_OUT_INIT_LOW, "ts_reset_gpio");
	if (r < 0) {
		ts_err(dev, "Failed to request reset gpio, r:%d", r);
		return r;
	}

	r = devm_gpio_request_one(&core_data->pdev->dev, ts_bdata->irq_gpio,
				  GPIOF_IN, "ts_irq_gpio");
	if (r < 0) {
		ts_err(dev, "Failed to request irq gpio, r:%d", r);
		return r;
	}

	if (ts_bdata->avdd_gpio > 0) {
		r = devm_gpio_request_one(&core_data->pdev->dev,
					  ts_bdata->avdd_gpio,
					  GPIOF_OUT_INIT_LOW, "ts_avdd_gpio");
		if (r < 0) {
			ts_err(dev, "Failed to request avdd-gpio, r:%d", r);
			return r;
		}
	}

	if (ts_bdata->iovdd_gpio > 0) {
		r = devm_gpio_request_one(&core_data->pdev->dev,
					  ts_bdata->iovdd_gpio,
					  GPIOF_OUT_INIT_LOW, "ts_iovdd_gpio");
		if (r < 0) {
			ts_err(dev, "Failed to request iovdd-gpio, r:%d", r);
			return r;
		}
	}

	return 0;
}

/**
 * goodix_ts_input_dev_config - Requset and config a input device
 *  then register it to input sybsystem.
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_input_dev_config(struct goodix_ts_core *core_data)
{
	struct goodix_ts_board_data *ts_bdata = core_data->board_data;
	struct device *dev = core_data->bus->dev;
	struct input_dev *input_dev = NULL;
	int dev_id = core_data->pdev->id;
	int r;

	input_dev = input_allocate_device();
	if (!input_dev) {
		ts_err(dev, "Failed to allocated input device");
		return -ENOMEM;
	}

	core_data->input_dev = input_dev;
	input_set_drvdata(input_dev, core_data);

	snprintf(core_data->input_name, sizeof(core_data->input_name),
		"%s.%d", GOODIX_CORE_DRIVER_NAME, dev_id);

	input_dev->name = core_data->input_name;
	input_dev->id.bustype = core_data->bus->bus_type;
	input_dev->id.product = 0x0100 + dev_id;
	input_dev->id.vendor = 0x27C6;
	input_dev->id.version = 0x0100;

	set_bit(EV_SYN, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);
	set_bit(EV_ABS, input_dev->evbit);
	set_bit(BTN_TOUCH, input_dev->keybit);
	set_bit(BTN_TOOL_FINGER, input_dev->keybit);
	set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

	/* set input parameters */
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0,
			     ts_bdata->panel_max_x - 1, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0,
			     ts_bdata->panel_max_y - 1, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0,
			     ts_bdata->panel_max_w - 1, 0, 0);
#ifdef INPUT_TYPE_B_PROTOCOL
#if (KERNEL_VERSION(3, 7, 0) < LINUX_VERSION_CODE)
	input_mt_init_slots(input_dev, GOODIX_MAX_TOUCH, INPUT_MT_DIRECT);
#else
	input_mt_init_slots(input_dev, GOODIX_MAX_TOUCH);
#endif
#else
	/* for single touch */
	input_set_abs_params(input_dev, ABS_X, 0, ts_bdata->panel_max_x - 1, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, ts_bdata->panel_max_y - 1, 0, 0);
#endif

	input_set_capability(input_dev, EV_KEY, KEY_POWER);
	input_set_capability(input_dev, EV_KEY, KEY_WAKEUP);
	input_set_capability(input_dev, EV_KEY, KEY_GOTO);

	r = input_register_device(input_dev);
	if (r < 0) {
		ts_err(dev, "Unable to register input device");
		input_free_device(input_dev);
		return r;
	}

	return 0;
}

static int goodix_ts_pen_dev_config(struct goodix_ts_core *core_data)
{
	struct goodix_ts_board_data *ts_bdata = core_data->board_data;
	struct device *dev = core_data->bus->dev;
	struct input_dev *pen_dev = NULL;
	int dev_id = core_data->pdev->id;
	int r;

	pen_dev = input_allocate_device();
	if (!pen_dev) {
		ts_err(dev, "Failed to allocated pen device");
		return -ENOMEM;
	}

	core_data->pen_dev = pen_dev;
	input_set_drvdata(pen_dev, core_data);

	snprintf(core_data->input_pen_name, sizeof(core_data->input_pen_name),
		"%s.%d,%s", GOODIX_CORE_DRIVER_NAME, dev_id, "pen");
	pen_dev->name = core_data->input_pen_name;
	pen_dev->id.bustype = core_data->bus->bus_type;
	pen_dev->id.product = 0x0200 + dev_id;
	pen_dev->id.vendor = 0x27C6;
	pen_dev->id.version = 0x0100;

	set_bit(EV_SYN, pen_dev->evbit);
	set_bit(EV_ABS, pen_dev->evbit);
	set_bit(EV_KEY, pen_dev->evbit);
	set_bit(ABS_X, pen_dev->absbit);
	set_bit(ABS_Y, pen_dev->absbit);
	set_bit(ABS_TILT_X, pen_dev->absbit);
	set_bit(ABS_TILT_Y, pen_dev->absbit);
	set_bit(BTN_STYLUS, pen_dev->keybit);
	set_bit(BTN_STYLUS2, pen_dev->keybit);
	set_bit(BTN_TOOL_RUBBER, pen_dev->keybit);
	set_bit(BTN_TOUCH, pen_dev->keybit);
	set_bit(BTN_TOOL_PEN, pen_dev->keybit);
	set_bit(INPUT_PROP_DIRECT, pen_dev->propbit);
	input_set_abs_params(pen_dev, ABS_X, 0, ts_bdata->panel_max_x - 1, 0,
			     0);
	input_set_abs_params(pen_dev, ABS_Y, 0, ts_bdata->panel_max_y - 1, 0,
			     0);
	input_set_abs_params(pen_dev, ABS_PRESSURE, 0,
			     ts_bdata->panel_max_p - 1, 0, 0);
	input_set_abs_params(pen_dev, ABS_DISTANCE, 0, 255, 0, 0);
	input_set_abs_params(pen_dev, ABS_TILT_X, -GOODIX_PEN_MAX_TILT,
			     GOODIX_PEN_MAX_TILT, 0, 0);
	input_set_abs_params(pen_dev, ABS_TILT_Y, -GOODIX_PEN_MAX_TILT,
			     GOODIX_PEN_MAX_TILT, 0, 0);

	r = input_register_device(pen_dev);
	if (r < 0) {
		ts_err(dev, "Unable to register pen device");
		input_free_device(pen_dev);
		return r;
	}

	return 0;
}

static void goodix_ts_input_dev_remove(struct goodix_ts_core *core_data)
{
	if (!core_data->input_dev)
		return;
	input_unregister_device(core_data->input_dev);
	core_data->input_dev = NULL;
}

static void goodix_ts_pen_dev_remove(struct goodix_ts_core *core_data)
{
	if (!core_data->pen_dev)
		return;
	input_unregister_device(core_data->pen_dev);
	core_data->pen_dev = NULL;
}

/**
 * goodix_ts_esd_work - check hardware status and recovery
 *  the hardware if needed.
 */
static void goodix_ts_esd_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct goodix_ts_esd *ts_esd =
		container_of(dwork, struct goodix_ts_esd, esd_work);
	struct goodix_ts_core *cd =
		container_of(ts_esd, struct goodix_ts_core, ts_esd);
	struct device *dev = cd->bus->dev;
	const struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	int ret = 0;

	if (ts_esd->irq_status)
		goto exit;

	if (!atomic_read(&ts_esd->esd_on))
		return;

	if (!hw_ops->esd_check)
		return;

	ret = hw_ops->esd_check(cd);
	if (ret) {
		ts_err(dev, "esd check failed");
		goodix_ts_power_off(cd);
		msleep(200);
		goodix_ts_power_on(cd);
	}

exit:
	ts_esd->irq_status = false;
	if (atomic_read(&ts_esd->esd_on))
		schedule_delayed_work(&ts_esd->esd_work, 2 * HZ);
}

/**
 * goodix_ts_esd_on - turn on esd protection
 */
void goodix_ts_esd_on(struct goodix_ts_core *cd)
{
	struct device *dev = cd->bus->dev;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_ts_esd *ts_esd = &cd->ts_esd;

	if (!cd->board_data->esd_enable ||
			misc->esd_addr == 0 ||
			atomic_read(&ts_esd->esd_on))
		return;

	atomic_set(&ts_esd->esd_on, 1);
	if (!schedule_delayed_work(&ts_esd->esd_work, 2 * HZ))
		ts_info(dev, "esd work already in workqueue");

	ts_info(dev, "esd on");
}

/**
 * goodix_ts_esd_off - turn off esd protection
 */
void goodix_ts_esd_off(struct goodix_ts_core *cd)
{
	struct device *dev = cd->bus->dev;
	struct goodix_ts_esd *ts_esd = &cd->ts_esd;

	if (!cd->board_data->esd_enable || !atomic_read(&ts_esd->esd_on))
		return;

	atomic_set(&ts_esd->esd_on, 0);
	cancel_delayed_work_sync(&ts_esd->esd_work);
	ts_info(dev, "esd off");
}

/**
 * goodix_ts_esd_init - initialize esd protection
 */
static int goodix_ts_esd_init(struct goodix_ts_core *cd)
{
	struct device *dev = cd->bus->dev;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_ts_esd *ts_esd = &cd->ts_esd;

	if (!cd->hw_ops->esd_check || !misc->esd_addr) {
		ts_info(dev, "missing key info for esd check");
		return 0;
	}

	INIT_DELAYED_WORK(&ts_esd->esd_work, goodix_ts_esd_work);
	ts_esd->ts_core = cd;
	atomic_set(&ts_esd->esd_on, 0);
	goodix_ts_esd_on(cd);

	return 0;
}

static void goodix_ts_release_connects(struct goodix_ts_core *core_data)
{
	struct input_dev *input_dev = core_data->input_dev;
	struct input_dev *pen_dev = core_data->pen_dev;
	struct goodix_touch_data *pre_touch = &core_data->ts_event.pre_touch;
	struct goodix_pen_data *pre_pen = &core_data->ts_event.pre_pen;
	int i;

	mutex_lock(&input_dev->mutex);
	for (i = 0; i < GOODIX_MAX_TOUCH; i++) {
		input_mt_slot(input_dev, i);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false);
	}
	input_report_key(input_dev, BTN_TOUCH, 0);
	input_report_key(input_dev, BTN_TOOL_FINGER, 0);
	input_sync(input_dev);
	mutex_unlock(&input_dev->mutex);
	memset(pre_touch, 0, sizeof(*pre_touch));

	if (core_data->board_data->pen_enable) {
		mutex_lock(&pen_dev->mutex);
		input_report_key(pen_dev, BTN_TOUCH, 0);
		input_report_key(pen_dev, BTN_TOOL_PEN, 0);
		input_sync(pen_dev);
		mutex_unlock(&pen_dev->mutex);
		memset(pre_pen, 0, sizeof(*pre_pen));
	}
}

/**
 * goodix_ts_suspend - Touchscreen suspend function
 * Called by PM/FB/EARLYSUSPEN module to put the device to sleep
 */
static int goodix_ts_suspend(struct goodix_ts_core *core_data)
{
	struct device *dev = core_data->bus->dev;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;

	if (core_data->init_stage < CORE_INIT_STAGE2 ||
	    atomic_read(&core_data->suspended))
		return 0;

	ts_info(dev, "Suspend start");
	atomic_set(&core_data->suspended, 1);
	/* disable irq */
	hw_ops->irq_enable(core_data, false);
	goodix_ts_esd_off(core_data);
	/* release all points */
	goodix_ts_release_connects(core_data);

	if (core_data->gesture_type) {
		/* enter gesture mode */
		ts_info(dev, "enter gesture mode[0x%x]", core_data->gesture_type);
		hw_ops->gesture(core_data, core_data->gesture_type);
		hw_ops->irq_enable(core_data, true);
		enable_irq_wake(core_data->irq);
	} else {
		/* enter sleep mode or power off */
		if (core_data->board_data->sleep_enable)
			hw_ops->suspend(core_data);
		else
			goodix_ts_power_off(core_data);
	}

	ts_info(dev, "Suspend end");
	return 0;
}

/**
 * goodix_ts_resume - Touchscreen resume function
 * Called by PM/FB/EARLYSUSPEN module to wakeup device
 */
static int goodix_ts_resume(struct goodix_ts_core *core_data)
{
	struct device *dev = core_data->bus->dev;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;

	if (core_data->init_stage < CORE_INIT_STAGE2 ||
	    !atomic_read(&core_data->suspended))
		return 0;

	ts_info(dev, "Resume start");
	atomic_set(&core_data->suspended, 0);
	hw_ops->irq_enable(core_data, false);

	if (core_data->gesture_type) {
		disable_irq_wake(core_data->irq);
		hw_ops->resume(core_data);
	} else {
		/* reset device or power on*/
		if (core_data->board_data->sleep_enable)
			hw_ops->resume(core_data);
		else
			goodix_ts_power_on(core_data);
	}

	/* enable irq */
	hw_ops->irq_enable(core_data, true);
	/* open esd */
	goodix_ts_esd_on(core_data);
	ts_info(dev, "Resume end");
	return 0;
}

static void goodix_ts_resume_work(struct work_struct *work)
{
	struct goodix_ts_core *cd =
		container_of(work, struct goodix_ts_core, resume_work);
	goodix_ts_resume(cd);
}

#ifdef CONFIG_DRM_MEDIATEK
static int goodix_ts_mtk_drm_notifier_callback(struct notifier_block *self,
				unsigned long event, void *v)
{
	struct goodix_ts_core *core_data =
		container_of(self, struct goodix_ts_core, pm_notifier);
	struct device *dev = core_data->bus->dev;
	int *data = (int *)v;

	if (!core_data || !v) {
		ts_err(dev, "invalid parameters");
		return -1;
	}

	if (event == MTK_DISP_EVENT_BLANK) {
		ts_info(dev, "MTK_DISP_EVENT_BLANK received");
		if (*data == MTK_DISP_BLANK_UNBLANK)
			schedule_work(&core_data->resume_work);
	} else if (event == MTK_DISP_EARLY_EVENT_BLANK) {
		ts_info(dev, "MTK_DISP_EARLY_EVENT_BLANK received");
		if (*data == MTK_DISP_BLANK_POWERDOWN) {
			cancel_work_sync(&core_data->resume_work);
			goodix_ts_suspend(core_data);
		}
	} else {
		ts_info(dev, "%s ignore disp event %d, data %d", __func__, event, *data);
	}

	return 0;
}
#elif defined(CONFIG_DRM_PANEL_NOTIFY)
static void goodix_ts_drm_notifier_callback(enum panel_event_notifier_tag tag,
				struct panel_event_notification *notification, void *client_data)
{
	struct goodix_ts_core *core_data = (struct goodix_ts_core *)client_data;
	struct device *dev = core_data->bus->dev;

	if (!notification) {
		ts_err(dev, "Invalid notification");
		return;
	}

	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_BLANK:
		if (notification->notif_data.early_trigger)
			ts_info(dev, "resume notification pre commit");
		else {
			cancel_work_sync(&core_data->resume_work);
			goodix_ts_suspend(core_data);
		}
		break;
	case DRM_PANEL_EVENT_UNBLANK:
		if (notification->notif_data.early_trigger)
			ts_info(dev, "suspend notification pre commit");
		else
			schedule_work(&core_data->resume_work);
		break;
	default:
		ts_info(dev, "notification serviced :%d", notification->notif_type);
		break;
	}
}
#elif defined(CONFIG_FB)
/**
 * goodix_ts_fb_notifier_callback - Framebuffer notifier callback
 * Called by kernel during framebuffer blanck/unblank phrase
 */
int goodix_ts_fb_notifier_callback(struct notifier_block *self,
				   unsigned long event, void *data)
{
	struct goodix_ts_core *core_data =
		container_of(self, struct goodix_ts_core, pm_notifier);
	struct fb_event *fb_event = data;
	int *blank;

	blank = fb_event->data;
	if (fb_event && fb_event->data && core_data) {
		if (event == FB_EVENT_BLANK) {
			if (*blank == FB_BLANK_UNBLANK) {
				schedule_work(&core_data->resume_work);
			} else if (*blank == FB_BLANK_POWERDOWN) {
				cancel_work_sync(&core_data->resume_work);
				goodix_ts_suspend(core_data);
			}
		}
	}

	return 0;
}
#elif defined(CONFIG_PM)
/**
 * goodix_ts_pm_suspend - PM suspend function
 * Called by kernel during system suspend phrase
 */
static int goodix_ts_pm_suspend(struct device *dev)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);

	return goodix_ts_suspend(core_data);
}
/**
 * goodix_ts_pm_resume - PM resume function
 * Called by kernel during system wakeup
 */
static int goodix_ts_pm_resume(struct device *dev)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);

	return goodix_ts_resume(core_data);
}

static const struct dev_pm_ops dev_pm_ops = {
	.suspend = goodix_ts_pm_suspend,
	.resume = goodix_ts_pm_resume,
};
#endif

static void goodix_self_check(struct work_struct *work)
{
	struct goodix_ts_core *cd =
		container_of(work, struct goodix_ts_core, self_check_work);
	struct device *dev = cd->bus->dev;
	u32 fw_state_addr = cd->ic_info.misc.fw_state_addr;
	int update_flag = UPDATE_MODE_BLOCK | UPDATE_MODE_SRC_REQUEST |
			  UPDATE_MODE_FORCE;
	u8 cur_cycle_cnt = 0;
	u8 pre_cycle_cnt = 0;
	int err_cnt = 0;
	int retry = 10;

	if (fw_state_addr == 0) {
		ts_err(dev, "no fw state addr, skip self check");
		return;
	}

	while (retry--) {
		cd->hw_ops->read(cd, fw_state_addr, &cur_cycle_cnt, 1);
		if (cur_cycle_cnt == pre_cycle_cnt)
			err_cnt++;
		pre_cycle_cnt = cur_cycle_cnt;
		msleep(20);
	}
	if (err_cnt > 5) {
		ts_err(dev, "Warning! The firmware maybe running abnormal, need upgrade.");
		goodix_do_fw_update(cd, update_flag);
	}
}

static int goodix_ts_stage2_init(struct goodix_ts_core *cd)
{
	struct device *dev = cd->bus->dev;
	int ret;

	/* alloc/config/register input device */
	ret = goodix_ts_input_dev_config(cd);
	if (ret < 0) {
		ts_err(dev, "failed set input device");
		return ret;
	}

	if (cd->board_data->pen_enable) {
		ret = goodix_ts_pen_dev_config(cd);
		if (ret < 0) {
			ts_err(dev, "failed set pen device");
			goto err_finger;
		}
	}
	/* request irq line */
	ret = goodix_ts_irq_setup(cd);
	if (ret < 0) {
		ts_info(dev, "failed set irq");
		goto exit;
	}
	ts_info(dev, "success register irq");

#ifdef CONFIG_DRM_MEDIATEK
	cd->pm_notifier.notifier_call = goodix_ts_mtk_drm_notifier_callback;
	if (mtk_disp_notifier_register(GOODIX_CORE_DRIVER_NAME, &cd->pm_notifier))
		ts_err(dev, "Failed to register disp notifier");
#elif defined(CONFIG_DRM_PANEL_NOTIFY)
	if (cd->board_data->active_panel) {
		cd->cookie = panel_event_notifier_register(
					PANEL_EVENT_NOTIFICATION_PRIMARY,
					PANEL_EVENT_NOTIFIER_CLIENT_PRIMARY_TOUCH,
					cd->board_data->active_panel,
					&goodix_ts_drm_notifier_callback,
					cd);
		if (!cd->cookie)
			ts_err(dev, "Failed to register panel event notifier");
	}
#elif defined(CONFIG_FB)
	cd->pm_notifier.notifier_call = goodix_ts_fb_notifier_callback;
	if (fb_register_client(&cd->pm_notifier))
		ts_err(dev, "Failed to register fb notifier client:%d", ret);
#endif
	/* create sysfs files */
	goodix_ts_sysfs_init(cd);

	/* create procfs files */
	goodix_ts_procfs_init(cd);

	/* esd protector */
	if (cd->board_data->esd_enable)
		goodix_ts_esd_init(cd);

	/* gesture init */
	gesture_module_init(cd);

	/* inspect init */
	inspect_module_init(cd);

	INIT_WORK(&cd->resume_work, goodix_ts_resume_work);

	/* Do self check on first boot */
	INIT_WORK(&cd->self_check_work, goodix_self_check);
	schedule_work(&cd->self_check_work);
#if (GOODIX_ENABLE_DUMP_DEV)
	goodix_dump_dev_init(cd);
#endif
	return 0;
exit:
	goodix_ts_pen_dev_remove(cd);
err_finger:
	goodix_ts_input_dev_remove(cd);
	return ret;
}

/* try send the config specified with type */
static int goodix_send_ic_config(struct goodix_ts_core *cd, int type)
{
	struct device *dev = cd->bus->dev;
	u32 config_id;
	struct goodix_ic_config *cfg;

	if (type >= GOODIX_MAX_CONFIG_GROUP) {
		ts_err(dev, "unsupproted config type %d", type);
		return -EINVAL;
	}

	cfg = &cd->ic_configs[type];
	if (cfg->len <= 0) {
		ts_info(dev, "no valid normal config found");
		return -EINVAL;
	}

	config_id = goodix_get_file_config_id(cfg->data);
	if (cd->ic_info.version.config_id == config_id) {
		ts_info(dev, "config id is equal 0x%x, skiped", config_id);
		return 0;
	}

	ts_info(dev, "try send config, id=0x%x", config_id);
	return cd->hw_ops->send_config(cd, cfg->data, cfg->len);
}

/**
 * goodix_later_init_thread - init IC fw and config
 * @data: point to goodix_ts_core
 *
 * This function respond for get fw version and try upgrade fw and config.
 * Note: when init encounter error, need release all resource allocated here.
 */
static int goodix_later_init_thread(void *data)
{
	int ret;
	int update_flag = UPDATE_MODE_BLOCK | UPDATE_MODE_SRC_REQUEST;
	struct goodix_ts_core *cd = data;
	struct device *dev = cd->bus->dev;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;

	/* step 1: read version */
	ret = hw_ops->read_version(cd, &cd->fw_version);
	if (ret < 0) {
		ts_err(dev, "failed to get version info, try to upgrade");
		update_flag |= UPDATE_MODE_FORCE;
	}

	if (cd->bus->ic_type == IC_TYPE_BERLIN_B &&
			cd->fw_version.rom_vid[0] == 0x02) {
		ts_info(dev, "select berlinB MCU2");
		cd->bus->sub_ic_type = IC_TYPE_SUB_MCU2;
		if (cd->update_ctrl.update_info) {
			cd->update_ctrl.update_info->isp_ram_reg = 0x3E800;
			cd->update_ctrl.update_info->flash_cmd_reg = 0x14800;
			cd->update_ctrl.update_info->isp_buffer_reg = 0x14810;
			cd->update_ctrl.update_info->config_data_reg = 0x5C000;
		}
	}

	/* step 2: read ic info */
	ret = hw_ops->get_ic_info(cd, &cd->ic_info);
	if (ret < 0) {
		ts_err(dev, "failed to get ic info, try to upgrade");
		update_flag |= UPDATE_MODE_FORCE;
	}

	/* step 3: do upgrade */
	ts_info(dev, "update flag: 0x%X", update_flag);
	goodix_do_fw_update(cd, update_flag);

	print_ic_info(cd, &cd->ic_info);

	/* the recomend way to update ic config is throuth ISP,
	 * if not we will send config with interactive mode
	 */
	goodix_send_ic_config(cd, CONFIG_TYPE_NORMAL);

	/* init other resources */
	ret = goodix_ts_stage2_init(cd);
	if (ret) {
		ts_err(dev, "stage2 init failed");
		goto err_out;
	}

	cd->init_stage = CORE_INIT_STAGE2;
	return 0;
err_out:
	cd->init_stage = CORE_INIT_FAIL;
	return ret;
}

static int goodix_start_later_init(struct goodix_ts_core *cd)
{
	struct device *dev = cd->bus->dev;
	struct task_struct *init_thrd;
	/* create and run update thread */
	init_thrd = kthread_run(goodix_later_init_thread, cd,
				"goodix_init_thread");
	if (IS_ERR_OR_NULL(init_thrd)) {
		ts_err(dev, "Failed to create update thread:%ld",
		       PTR_ERR(init_thrd));
		return -EFAULT;
	}
	return 0;
}

/**
 * goodix_ts_probe - called by kernel when Goodix touch
 *  platform driver is added.
 */
static int goodix_ts_probe(struct platform_device *pdev)
{
	struct goodix_ts_device *ts_dev =
		container_of(pdev, struct goodix_ts_device, pdev);
	struct goodix_bus_interface *bus_interface = &ts_dev->bus;
	struct goodix_ts_board_data *board_data = &ts_dev->board_data;
	struct goodix_ts_core *core_data = NULL;
	int ret;


	if (IS_ENABLED(CONFIG_OF) && bus_interface->dev->of_node) {
		/* parse devicetree property */
		ret = goodix_parse_dt(bus_interface->dev, board_data);
		if (ret) {
			ts_err(bus_interface->dev, "failed parse device info form dts, %d", ret);
			return -EINVAL;
		}
	} else {
		ts_err(bus_interface->dev, "no valid device tree node found");
		return -ENODEV;
	}

	core_data = devm_kzalloc(&pdev->dev, sizeof(*core_data), GFP_KERNEL);
	if (!core_data)
		return -ENOMEM;

	core_data->hw_ops = goodix_get_hw_ops();
	if (!core_data->hw_ops) {
		ts_err(bus_interface->dev, "hw ops is NULL");
		return -EINVAL;
	}

	/* touch core layer is a platform driver */
	core_data->pdev = pdev;
	core_data->bus = bus_interface;
	core_data->board_data = board_data;
	platform_set_drvdata(pdev, core_data);

	/* get GPIO resource */
	ret = goodix_ts_gpio_setup(core_data);
	if (ret) {
		ts_err(bus_interface->dev, "failed init gpio");
		goto err_out;
	}

	ret = goodix_ts_power_init(core_data);
	if (ret) {
		ts_err(bus_interface->dev, "failed init power");
		goto err_out;
	}

	ret = goodix_ts_power_on(core_data);
	if (ret) {
		ts_err(bus_interface->dev, "failed power on");
		goto err_out;
	}

	/* update modue init */
	ret = goodix_fw_update_init(core_data);
	if (ret) {
		ts_err(bus_interface->dev, "firmware update module init failed");
		goto err_out;
	}

	/* debug node init */
	ret = goodix_tools_init(core_data);
	if (ret) {
		ts_err(bus_interface->dev, "debug tools init failed");
		goto err_out;
	}

	core_data->init_stage = CORE_INIT_STAGE1;

	/* Try start a thread to get config-bin info */
	goodix_start_later_init(core_data);

	mutex_init(&core_data->cmd_lock);
	ts_info(bus_interface->dev, "goodix_ts_core probe success");
	return 0;

err_out:
	goodix_fw_update_uninit(core_data);
	goodix_ts_power_off(core_data);
	core_data->init_stage = CORE_INIT_FAIL;
	ts_err(bus_interface->dev, "goodix_ts_core failed, ret:%d", ret);
	return ret;
}

static int goodix_ts_remove(struct platform_device *pdev)
{
	struct goodix_ts_core *core_data = platform_get_drvdata(pdev);
	struct device *dev = core_data->bus->dev;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	struct goodix_ts_esd *ts_esd = &core_data->ts_esd;

	ts_info(dev, "platform device[%d] remove", pdev->id);
#if (GOODIX_ENABLE_DUMP_DEV)
	goodix_dump_dev_exit();
#endif
	goodix_tools_exit(core_data);
	goodix_fw_update_uninit(core_data);

	if (core_data->init_stage >= CORE_INIT_STAGE2) {
		gesture_module_exit(core_data);
		inspect_module_exit(core_data);
		hw_ops->irq_enable(core_data, false);
#ifdef CONFIG_DRM_MEDIATEK
		mtk_disp_notifier_unregister(&core_data->pm_notifier);
#elif defined(CONFIG_DRM_PANEL_NOTIFY)
		if (core_data->cookie)
			panel_event_notifier_unregister(core_data->cookie);
#elif defined(CONFIG_FB)
		fb_unregister_client(&core_data->pm_notifier);
#endif
		if (atomic_read(&ts_esd->esd_on))
			goodix_ts_esd_off(core_data);

		goodix_ts_input_dev_remove(core_data);
		goodix_ts_pen_dev_remove(core_data);
		goodix_ts_sysfs_exit(core_data);
		goodix_ts_procfs_exit(core_data);
		goodix_ts_power_off(core_data);
	}

	return 0;
}

#if (KERNEL_VERSION(6, 11, 0) <= LINUX_VERSION_CODE)
static void platform_remove(struct platform_device *pdev)
{
	goodix_ts_remove(pdev);
}
#else
static int platform_remove(struct platform_device *pdev)
{
	return goodix_ts_remove(pdev);
}
#endif

static void platform_shutdown(struct platform_device *pdev)
{
	struct goodix_ts_core *core_data = platform_get_drvdata(pdev);
	struct device *dev = core_data->bus->dev;

	ts_info(dev, "platform device[%d] shutdown", pdev->id);
	goodix_ts_remove(pdev);
}

static const struct platform_device_id ts_core_ids[] = {
	{ .name = GOODIX_CORE_DRIVER_NAME },
	{}
};
MODULE_DEVICE_TABLE(platform, ts_core_ids);

static struct platform_driver goodix_ts_driver = {
	.driver = {
		.name = GOODIX_CORE_DRIVER_NAME,
#if defined(CONFIG_PM) && !defined(CONFIG_DRM_MEDIATEK) && \
	!defined(CONFIG_DRM_PANEL_NOTIFY) && !defined(CONFIG_FB)
		.pm = &dev_pm_ops,
#endif
	},
	.probe = goodix_ts_probe,
	.remove = platform_remove,
	.shutdown = platform_shutdown,
	.id_table = ts_core_ids,
};

static int __init goodix_ts_core_init(void)
{
	int ret;

	ts_info(NULL, "Core layer init:%s", GOODIX_DRIVER_VERSION);
	ret = platform_driver_register(&goodix_ts_driver);
	if (ret < 0) {
		ts_err(NULL, "platform driver register failed, exit");
		return ret;
	}

#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_SPI
	ret = goodix_spi_bus_init();
	if (ret < 0) {
		ts_err(NULL, "failed to add spi bus driver");
		return ret;
	}
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_I2C
	ret = goodix_i2c_bus_init();
	if (ret < 0) {
		ts_err(NULL, "failed to add i2c bus driver");
		return ret;
	}
#endif
	return 0;
}

static void __exit goodix_ts_core_exit(void)
{
	ts_info(NULL, "Core layer exit");

#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_SPI
	goodix_spi_bus_exit();
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_I2C
	goodix_i2c_bus_exit();
#endif
	platform_driver_unregister(&goodix_ts_driver);
}

late_initcall(goodix_ts_core_init);
module_exit(goodix_ts_core_exit);

MODULE_DESCRIPTION("Goodix Touchscreen Driver Module");
MODULE_LICENSE("GPL");
