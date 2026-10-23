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

#include "goodix_ts_core.h"
#include "goodix_ts_replay_type.h"

#define MAX_PACKAGE_SIZE 4096

struct replay_item_head {
	uint32_t type;
	uint32_t len;
};

struct replay_package {
	uint32_t size;
	uint32_t offset;
	uint8_t *data;
};

static DEFINE_MUTEX(replay_mutex);
static bool rep_flag;
static bool is_first_reading;
static u8 frame_buffer[4096];
static u8 replay_buffer[MAX_PACKAGE_SIZE + sizeof(struct replay_package)];
static struct replay_package *replay_pkg = (struct replay_package *)replay_buffer;

static ssize_t replay_status_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", rep_flag ? "1" : "0");
}

static ssize_t replay_status_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct goodix_ts_core *cd = dev_get_drvdata(dev);
	struct kobject *replay_kobj = &cd->pdev->dev.kobj;
	struct device *device = cd->bus->dev;
	struct goodix_ts_cmd tmp_cmd;
	int ret;

	tmp_cmd.len = 5;
	tmp_cmd.cmd = 0x90;

	if ((buf[0] == 0 || buf[0] == '0') && !rep_flag)
		return count;

	if ((buf[0] == 1 || buf[0] == '1') && rep_flag)
		return count;

	if (buf[0] == 0 || buf[0] == '0') {
		rep_flag = false;
		tmp_cmd.data[0] = 0x00;
		sysfs_notify(replay_kobj, "replay", "replay_data");
	} else {
		rep_flag = true;
		is_first_reading = true;
		tmp_cmd.data[0] = 0x83; //ref data
	}

	ts_info(device, "set replay status %d", rep_flag ? 1 : 0);

	ret = cd->hw_ops->send_cmd(cd, &tmp_cmd);
	if (ret < 0)
		ts_err(device, "send cmd [90 %02x] failed", tmp_cmd.data[0]);

	return count;
}

static ssize_t replay_data_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct goodix_ts_core *cd = dev_get_drvdata(dev);
	struct goodix_ts_cmd tmp_cmd;
	int real_size;
	static int discard_cnt;

	if (!rep_flag)
		return 0;

	if (discard_cnt > 0) {
		discard_cnt--;
		return 0;
	}

	mutex_lock(&replay_mutex);
	if (is_first_reading) {
		is_first_reading = false;
		discard_cnt = 2;
		tmp_cmd.len = 5;
		tmp_cmd.cmd = 0x90;
		tmp_cmd.data[0] = 0x81;
		cd->hw_ops->send_cmd(cd, &tmp_cmd);
	}

	real_size = replay_pkg->offset;
	memcpy(buf, replay_pkg->data, replay_pkg->offset);
	mutex_unlock(&replay_mutex);
	return real_size;
}

static DEVICE_ATTR_RW(replay_status);
static DEVICE_ATTR_RO(replay_data);

static struct attribute *replay_attrs[] = {
	&dev_attr_replay_status.attr,
	&dev_attr_replay_data.attr,
	NULL,
};

static const struct attribute_group replay_attr_group = {
	.name = "replay",
	.attrs = replay_attrs,
};

static int append_replay_item(struct replay_package *pkg, uint32_t type,
	uint32_t len, const uint8_t *data)
{
	if (pkg->offset + sizeof(struct replay_item_head) + len > pkg->size)
		return -1;

	memcpy(pkg->data + pkg->offset, &type, sizeof(type));
	pkg->offset += sizeof(type);

	memcpy(pkg->data + pkg->offset, &len, sizeof(len));
	pkg->offset += sizeof(len);

	memcpy(pkg->data + pkg->offset, data, len);
	pkg->offset += len;
	return 0;
}

static void save_replay_head(struct goodix_ts_core *core_data, u8 *frame)
{
	struct goodix_ic_info *ic_info = &core_data->ic_info;
	struct goodix_fw_version *fw_ver = &core_data->fw_version;
	struct replay_package *pkg = replay_pkg;
	uint32_t type_group;
	u8 tmp_buf[64] = {0};
	u16 x_resolution = core_data->board_data->panel_max_x;
	u16 y_resolution = core_data->board_data->panel_max_y;
	u16 tx = ic_info->parm.drv_num;
	u16 rx = ic_info->parm.sen_num;
	int offset = 0;
	u8 *mutual_data;
	u8 *self_data;

	pkg->size = MAX_PACKAGE_SIZE;
	pkg->offset = 0;
	pkg->data = (u8 *)pkg + sizeof(*pkg);

	type_group = GROUP_VERSION;
	append_replay_item(pkg, MISC_GROUP_BEGIN, MISC_GROUP_BEGIN_SIZE, (u8 *)&type_group);
	append_replay_item(pkg, VERSION_FW_VER, 4, fw_ver->patch_vid);
	append_replay_item(pkg, MISC_GROUP_END, MISC_GROUP_END_SIZE, (u8 *)&type_group);

	type_group = GROUP_AFECAPS;
	append_replay_item(pkg, MISC_GROUP_BEGIN, MISC_GROUP_BEGIN_SIZE, (u8 *)&type_group);
	append_replay_item(pkg, AFECAPS_TX_NUM, AFECAPS_TX_NUM_SIZE, (u8 *)&tx);
	append_replay_item(pkg, AFECAPS_RX_NUM, AFECAPS_RX_NUM_SIZE, (u8 *)&rx);
	append_replay_item(pkg, AFECAPS_BUTTON_NUM, AFECAPS_BUTTON_NUM_SIZE,
			(u8 *)&ic_info->parm.button_num);
	append_replay_item(pkg, AFECAPS_FORCE_NUM, AFECAPS_FORCE_NUM_SIZE,
			(u8 *)&ic_info->parm.force_num);
	append_replay_item(pkg, AFECAPS_X_RESOLUTION, AFECAPS_X_RESOLUTION_SIZE,
			(u8 *)&x_resolution);
	append_replay_item(pkg, AFECAPS_Y_RESOLUTION, AFECAPS_Y_RESOLUTION_SIZE,
			(u8 *)&y_resolution);
	append_replay_item(pkg, AFECAPS_X_REVERSAL, AFECAPS_X_REVERSAL_SIZE, tmp_buf);
	append_replay_item(pkg, AFECAPS_Y_REVERSAL, AFECAPS_Y_REVERSAL_SIZE, tmp_buf);
	append_replay_item(pkg, AFECAPS_XY_SWAP, AFECAPS_XY_SWAP_SIZE, tmp_buf);
	append_replay_item(pkg, AFECAPS_SCAN_RATE_NUM, AFECAPS_SCAN_RATE_NUM_SIZE,
			(u8 *)&ic_info->parm.active_scan_rate_num);
	append_replay_item(pkg, AFECAPS_SCAN_RATE, ic_info->parm.active_scan_rate_num * 2,
			(u8 *)ic_info->parm.active_scan_rate);
	append_replay_item(pkg, AFECAPS_MUTUAL_FREQ_NUM, AFECAPS_MUTUAL_FREQ_NUM_SIZE,
			(u8 *)&ic_info->parm.mutual_freq_num);
	append_replay_item(pkg, AFECAPS_MUTUAL_FREQ, ic_info->parm.mutual_freq_num * 2,
			(u8 *)ic_info->parm.mutual_freq);
	append_replay_item(pkg, AFECAPS_SELF_TX_FREQ_NUM, AFECAPS_SELF_TX_FREQ_NUM_SIZE,
			(u8 *)&ic_info->parm.self_tx_freq_num);
	append_replay_item(pkg, AFECAPS_SELF_TX_FREQ, ic_info->parm.self_tx_freq_num * 2,
			(u8 *)ic_info->parm.self_tx_freq);
	append_replay_item(pkg, AFECAPS_SELF_RX_FREQ_NUM, AFECAPS_SELF_RX_FREQ_NUM_SIZE,
			(u8 *)&ic_info->parm.self_rx_freq_num);
	append_replay_item(pkg, AFECAPS_SELF_RX_FREQ, ic_info->parm.self_rx_freq_num * 2,
			(u8 *)ic_info->parm.self_rx_freq);
	append_replay_item(pkg, AFECAPS_STYLUS_FREQ_NUM, AFECAPS_STYLUS_FREQ_NUM_SIZE,
			(u8 *)&ic_info->parm.stylus_freq_num);
	append_replay_item(pkg, AFECAPS_STYLUS_FREQ, ic_info->parm.stylus_freq_num * 2,
			(u8 *)ic_info->parm.stylus_freq);
	append_replay_item(pkg, AFECAPS_FREQHOP_FEATURE, AFECAPS_FREQHOP_FEATURE_SIZE,
			(u8 *)&ic_info->feature.freqhop_feature);
	append_replay_item(pkg, AFECAPS_CALIB_FEATURE, AFECAPS_CALIB_FEATURE_SIZE,
			(u8 *)&ic_info->feature.calibration_feature);
	append_replay_item(pkg, AFECAPS_GESTURE_FEATURE, AFECAPS_GESTURE_FEATURE_SIZE,
			(u8 *)&ic_info->feature.gesture_feature);
	append_replay_item(pkg, AFECAPS_STYLUS_FEATURE, AFECAPS_STYLUS_FEATURE_SIZE,
			(u8 *)&ic_info->feature.stylus_feature);
	append_replay_item(pkg, MISC_GROUP_END, MISC_GROUP_END_SIZE, (u8 *)&type_group);

	offset += core_data->ic_info.misc.frame_data_head_len;
	offset += core_data->ic_info.misc.fw_attr_len;
	offset += core_data->ic_info.misc.fw_log_len;
	mutual_data = frame + offset + 8;
	offset += core_data->ic_info.misc.mutual_struct_len;
	self_data = frame + offset + 10;

	type_group = GROUP_INITIAL;
	append_replay_item(pkg, MISC_GROUP_BEGIN, MISC_GROUP_BEGIN_SIZE, (u8 *)&type_group);
	append_replay_item(pkg, INITIAL_INITIAL_MUTUAL_REF, tx * rx * 2, mutual_data);
	append_replay_item(pkg, INITIAL_INITIAL_SELF_REF, (tx + rx) * 2, self_data);
	append_replay_item(pkg, MISC_GROUP_END, MISC_GROUP_END_SIZE, (u8 *)&type_group);
}

static void save_replay_body(struct goodix_ts_core *core_data, u8 *frame,
		struct goodix_ts_event *ts_event)
{
	struct goodix_touch_data *finger = &ts_event->touch_data;
	struct replay_package *pkg = replay_pkg;
	int tx = core_data->ic_info.parm.drv_num;
	int rx = core_data->ic_info.parm.sen_num;
	int offset = 0;
	u32 type_group;
	u16 frame_index = le16_to_cpup((__le16 *)(frame + 1));
	u16 scan_rate = 0;
	u16 mutual_duration;
	u16 mutual_freq_a;
	u16 mutual_freq_b;
	u8 *mutual_data;
	u16 self_tx_duration;
	u16 self_rx_duration;
	u16 self_tx_freq;
	u16 self_rx_freq;
	u8 *self_data;
	struct timespec64 real_ts;
	u64 time_stamp;
	int i;
	u8 index;
	u8 type = 0;
	u16 tmp_val;

	offset += core_data->ic_info.misc.frame_data_head_len;
	offset += core_data->ic_info.misc.fw_attr_len;
	offset += core_data->ic_info.misc.fw_log_len;
	mutual_duration = le16_to_cpup((__le16 *)(frame + offset));
	mutual_freq_a = le16_to_cpup((__le16 *)(frame + offset + 2));
	mutual_freq_b = le16_to_cpup((__le16 *)(frame + offset + 4));
	mutual_data = frame + offset + 8;

	offset += core_data->ic_info.misc.mutual_struct_len;
	self_tx_duration = le16_to_cpup((__le16 *)(frame + offset));
	self_rx_duration = le16_to_cpup((__le16 *)(frame + offset + 2));
	self_tx_freq = le16_to_cpup((__le16 *)(frame + offset + 4));
	self_rx_freq = le16_to_cpup((__le16 *)(frame + offset + 6));
	self_data = frame + offset + 10;

	pkg->size = MAX_PACKAGE_SIZE;
	pkg->offset = 0;
	pkg->data = (u8 *)pkg + sizeof(*pkg);

	type_group = GROUP_RUNNING;
	append_replay_item(pkg, MISC_GROUP_BEGIN, MISC_GROUP_BEGIN_SIZE, (u8 *)&type_group);
	type_group = GROUP_FRAME;
	append_replay_item(pkg, MISC_GROUP_BEGIN, MISC_GROUP_BEGIN_SIZE, (u8 *)&type_group);
	append_replay_item(pkg, FRAME_FRAME_INDEX, FRAME_FRAME_INDEX_SIZE, (u8 *)&frame_index);
	append_replay_item(pkg, FRAME_SCAN_RATE, FRAME_SCAN_RATE_SIZE, (u8 *)&scan_rate);
	append_replay_item(pkg, FRAME_MUTUAL_DURATION, FRAME_MUTUAL_DURATION_SIZE,
			(u8 *)&mutual_duration);
	append_replay_item(pkg, FRAME_MUTUAL_FREQ_A, FRAME_MUTUAL_FREQ_A_SIZE,
			(u8 *)&mutual_freq_a);
	append_replay_item(pkg, FRAME_MUTUAL_FREQ_B, FRAME_MUTUAL_FREQ_B_SIZE,
			(u8 *)&mutual_freq_b);
	append_replay_item(pkg, FRAME_MUTUAL_DATA, tx * rx * 2, mutual_data);
	append_replay_item(pkg, FRAME_SELF_TX_DURATION, FRAME_SELF_TX_DURATION_SIZE,
			(u8 *)&self_tx_duration);
	append_replay_item(pkg, FRAME_SELF_RX_DURATION, FRAME_SELF_RX_DURATION_SIZE,
			(u8 *)&self_rx_duration);
	append_replay_item(pkg, FRAME_SELF_TX_FREQ, FRAME_SELF_TX_FREQ_SIZE,
			(u8 *)&self_tx_freq);
	append_replay_item(pkg, FRAME_SELF_RX_FREQ, FRAME_SELF_RX_FREQ_SIZE,
			(u8 *)&self_rx_freq);
	append_replay_item(pkg, FRAME_SELF_DATA, (tx + rx) * 2, self_data);
	type_group = GROUP_FRAME;
	append_replay_item(pkg, MISC_GROUP_END, MISC_GROUP_END_SIZE, (u8 *)&type_group);
	type_group = GROUP_RUNNING;
	append_replay_item(pkg, MISC_GROUP_END, MISC_GROUP_END_SIZE, (u8 *)&type_group);

	ktime_get_real_ts64(&real_ts);
	time_stamp = (u64)real_ts.tv_sec * 1000000ULL + real_ts.tv_nsec / 1000;

	type_group = GROUP_REPORT;
	append_replay_item(pkg, MISC_GROUP_BEGIN, MISC_GROUP_BEGIN_SIZE, (u8 *)&type_group);
	append_replay_item(pkg, REPORT_TIME_STAMP, REPORT_TIME_STAMP_SIZE, (u8 *)&time_stamp);
	append_replay_item(pkg, REPORT_TOUCH_NUM, REPORT_TOUCH_NUM_SIZE, (u8 *)&finger->touch_num);
	if (finger->touch_num > 0) {
		for (i = 0, index = 0; i < GOODIX_MAX_TOUCH; i++) {
			if (finger->coords[i].status == TS_TOUCH) {
				append_replay_item(pkg, MISC_POINT_INDEX, MISC_POINT_INDEX_SIZE,
						(u8 *)&index);
				append_replay_item(pkg, REPORT_TRACKING_ID, REPORT_TRACKING_ID_SIZE,
						(u8 *)&i);
				append_replay_item(pkg, REPORT_TYPE, REPORT_TYPE_SIZE, (u8 *)&type);
				append_replay_item(pkg, REPORT_X_POS, REPORT_X_POS_SIZE,
						(u8 *)&finger->coords[i].x);
				append_replay_item(pkg, REPORT_Y_POS, REPORT_Y_POS_SIZE,
						(u8 *)&finger->coords[i].y);
				append_replay_item(pkg, REPORT_TOUCH_MAJOR, REPORT_TOUCH_MAJOR_SIZE,
						(u8 *)&finger->coords[i].w);
				append_replay_item(pkg, REPORT_TOUCH_MINOR, REPORT_TOUCH_MINOR_SIZE,
						(u8 *)&finger->coords[i].w);
				index++;
			}
		}
	}

	append_replay_item(pkg, REPORT_COVER_FP, REPORT_COVER_FP_SIZE, (u8 *)&ts_event->fp_flag);
	tmp_val = 0;
	append_replay_item(pkg, REPORT_IS_FULL_HOVER, REPORT_IS_FULL_HOVER_SIZE, (u8 *)&tmp_val);
	append_replay_item(pkg, REPORT_IS_LARGE_TOUCH, REPORT_IS_LARGE_TOUCH_SIZE,
			(u8 *)&finger->palm_flag);
	append_replay_item(pkg, MISC_GROUP_END, MISC_GROUP_END_SIZE, (u8 *)&type_group);
}

int goodix_ts_replay_record(struct goodix_ts_core *core_data, struct goodix_ts_event *ts_event)
{
	struct kobject *replay_kobj = &core_data->pdev->dev.kobj;
	u32 frame_addr = core_data->ic_info.misc.frame_data_addr;
	u16 frame_head_len = core_data->ic_info.misc.frame_data_head_len;
	u16 frame_len;
	u8 val = 0;

	if (!rep_flag)
		return 0;

	core_data->hw_ops->read(core_data, frame_addr, frame_buffer, frame_head_len);
	if (frame_buffer[0] != 0x80)
		return 0;

	if (checksum_cmp(frame_buffer, frame_head_len, CHECKSUM_MODE_U8_LE)) {
		core_data->hw_ops->write(core_data, frame_addr, &val, 1);
		return 0;
	}

	frame_len = le16_to_cpup((__le16 *)&frame_buffer[3]);
	if (frame_len > sizeof(frame_buffer) || frame_len == 0) {
		core_data->hw_ops->write(core_data, frame_addr, &val, 1);
		return 0;
	}

	core_data->hw_ops->read(core_data, frame_addr, frame_buffer, frame_len);
	core_data->hw_ops->write(core_data, frame_addr, &val, 1);

	mutex_lock(&replay_mutex);
	if (is_first_reading)
		save_replay_head(core_data, frame_buffer);
	else
		save_replay_body(core_data, frame_buffer, ts_event);
	sysfs_notify(replay_kobj, "replay", "replay_data");
	mutex_unlock(&replay_mutex);
	return 0;
}

int goodix_ts_replay_init(struct goodix_ts_core *core_data)
{
	struct device *dev = core_data->bus->dev;
	int ret;

	ret = sysfs_create_group(&core_data->pdev->dev.kobj, &replay_attr_group);
	if (ret) {
		ts_err(dev, "Failed to create replay sysfs group: %d", ret);
		return ret;
	}

	ts_info(dev, "Replay sysfs interface initialized successfully");
	return 0;
}

void goodix_ts_replay_exit(struct goodix_ts_core *core_data)
{
	struct device *dev = core_data->bus->dev;

	sysfs_remove_group(&core_data->pdev->dev.kobj, &replay_attr_group);

	ts_info(dev, "Replay sysfs interface removed");
}
