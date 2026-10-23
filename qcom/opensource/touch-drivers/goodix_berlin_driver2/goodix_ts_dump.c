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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/ioctl.h>
#include <linux/compat.h>
#include <linux/mm.h>
#include <linux/delay.h>

#include "goodix_ts_core.h"

#define DEVICE_NAME "goodix_dump"
#define GOODIX_DUMP_DEV_VER_MAJOR 1
#define GOODIX_DUMP_DEV_VER_MINOR 1
static const u16 goodix_dump_dev_ver =
	((GOODIX_DUMP_DEV_VER_MAJOR << 8) + (GOODIX_DUMP_DEV_VER_MINOR));

#define GOODIX_TS_IOC_MAGIC 'G'
#define NEGLECT_SIZE_MASK (~(_IOC_SIZEMASK << _IOC_SIZESHIFT))

#define GTP_DUMP_DEV_VER (_IOR(GOODIX_TS_IOC_MAGIC, 0, u8) & NEGLECT_SIZE_MASK)
#define GTP_DUMP_PARAMS_GET \
	(_IOR(GOODIX_TS_IOC_MAGIC, 1, u32) & NEGLECT_SIZE_MASK)
#define GTP_DUMP_PARAMS_SET \
	(_IOW(GOODIX_TS_IOC_MAGIC, 2, u32) & NEGLECT_SIZE_MASK)

#define GTP_DUMP_START (_IOW(GOODIX_TS_IOC_MAGIC, 3, u8) & NEGLECT_SIZE_MASK)
#define GTP_DUMP_STOP (_IOW(GOODIX_TS_IOC_MAGIC, 4, u8) & NEGLECT_SIZE_MASK)
#define GTP_DUMP_FRAME_GET \
	(_IOR(GOODIX_TS_IOC_MAGIC, 5, u32) & NEGLECT_SIZE_MASK)

#define GOODIX_DUMP_HEADER_SIZE 800
#define GOODIX_MAX_DUMP_FRAME_SIZE 8192
#define GOODIX_MAX_DUMP_FRAME_NUM 20
#define GOODIX_DEFAULT_DUMP_INTERVAL 1

#define GOODIX_DUMP_CMD 0xF2

enum DUMP_MODE {
	DUMP_MODE_OFF,
	DUMP_MODE_ON,
};

struct dump_params {
	u32 address;
	u32 frame_size;
	u32 max_frame_num;
	u32 sample_interval;
};

/* dump_buf_header用于记录frame buf的读写偏移位置，
 * 申请mmap的内存时保留头部的字节用于存储header信息，
 * 用户空间和内核空间共用该头部区域。
 * 驱动从head开始写入数据，写入后修改head的值，
 * 用户程序从tail获取数据，读取后修改tail的值。
 * 由于只有一个内核进程修改head，只有一个用户进程修改tail，
 * 因此不需要对buf进行加锁操作。
 */
struct dump_buf_header {
	u32 head; /* head offset */
	u32 tail; /* tail offset */
};

struct dump_frame {
	u32 size;
	u8 data[];
};

struct goodix_dump_dev {
	struct goodix_ts_core *cd;
	int mode;

	u32 address;
	int frame_size;
	int max_frame_num;
	int buffer_size;
	int sample_interval;

	u8 *dbuf;
	struct dump_buf_header *db_header;
	struct page *page;
	int page_order;

	wait_queue_head_t waitq;
	int need_wakeup;
	int dbuf_in_use;
} g_dump_dev;

/* cdev + class + device state */
static struct class *goodix_dump_class;
static struct cdev goodix_dump_cdev;
static dev_t goodix_dump_devno;

static int goodix_dump_buf_init(struct goodix_dump_dev *dump_dev)
{
	size_t size;
	void *ptr;

	size = sizeof(struct dump_buf_header) + dump_dev->buffer_size;
	dump_dev->page_order = get_order(size);
	dump_dev->page =
		alloc_pages(GFP_KERNEL | __GFP_ZERO, dump_dev->page_order);
	if (!dump_dev->page) {
		ts_err(NULL, "failed alloc memory, size %zu, page order %d", size,
		       dump_dev->page_order);
		return -ENOMEM;
	}
	ptr = kmap_local_page(dump_dev->page);
	dump_dev->db_header = ptr;
	dump_dev->dbuf = (u8 *)ptr + sizeof(struct dump_buf_header);

	dump_dev->db_header->head = 0;
	dump_dev->db_header->tail = 0;
	ts_info(NULL, "dump buf init success, size %zu", size);
	return 0;
}

static int goodix_dump_buf_deinit(struct goodix_dump_dev *dump_dev)
{
	if (!dump_dev->page) {
		ts_info(NULL, "dump buf hasn't init");
		return 0;
	}
	while (dump_dev->dbuf_in_use) {
		ts_info(NULL, "dump buf in use, waiting");
		msleep(20);
		continue;
	}
	__free_pages(dump_dev->page, dump_dev->page_order);
	dump_dev->db_header = NULL;
	dump_dev->dbuf = NULL;
	dump_dev->page = NULL;
	return 0;
}

static struct dump_frame *
goodix_get_dump_frame_buf(struct goodix_dump_dev *dump_dev, int size)
{
	struct dump_buf_header *dbh = dump_dev->db_header;
	struct dump_frame *frame;
	u32 valid_buf_size;

	if (dump_dev->mode == DUMP_MODE_OFF || !dump_dev->page)
		return NULL;

	valid_buf_size = dump_dev->buffer_size - (dbh->head - dbh->tail);
	if (valid_buf_size < size) {
		ts_err(NULL, "dump frame buf full");
		return NULL;
	}
	frame = (struct dump_frame *)(dump_dev->dbuf +
				      (dbh->head % dump_dev->buffer_size));
	frame->size = size;
	return frame;
}

static void goodix_dump_frame_write(struct goodix_dump_dev *dump_dev,
				    struct dump_frame *frame)
{
	struct dump_buf_header *dbh = dump_dev->db_header;

	dbh->head += frame->size;
	dump_dev->need_wakeup = 1;
	wake_up_interruptible(&dump_dev->waitq);
}

void goodix_get_dump_frame(struct goodix_ts_core *cd)
{
	int ret;
	static u32 irq_count;
	struct dump_frame *frame;

	if (g_dump_dev.mode == DUMP_MODE_OFF)
		return;

	if (irq_count++ % g_dump_dev.sample_interval) {
		ts_debug(NULL, "skip frame %d, interval %d", irq_count,
			 g_dump_dev.sample_interval);
		return;
	}

	g_dump_dev.dbuf_in_use = 1;
	frame = goodix_get_dump_frame_buf(&g_dump_dev, g_dump_dev.frame_size);
	if (!frame) {
		ts_debug(NULL, "failed get dump buf");
		goto err_out;
	}

	ret = cd->hw_ops->read(cd, g_dump_dev.address, frame->data,
			       frame->size - sizeof(frame->size));
	if (ret) {
		ts_err(NULL, "failed read dump info 0x%x, len %d",
		       g_dump_dev.address, frame->size);
		goto err_out;
	}

	goodix_dump_frame_write(&g_dump_dev, frame);

err_out:
	g_dump_dev.dbuf_in_use = 0;
}

static int goodix_dump_cmd_enable(struct goodix_dump_dev *dump_dev, int enable)
{
	struct goodix_ts_cmd cmd;

	if (enable)
		cmd.data[0] = 0x01; /* enable dump*/
	else
		cmd.data[0] = 0x00;

	cmd.cmd = GOODIX_DUMP_CMD;
	cmd.len = 5;
	return dump_dev->cd->hw_ops->send_cmd(dump_dev->cd, &cmd);
}

static int goodix_dump_params_set(struct goodix_dump_dev *dump_dev,
				  void __user *arg)
{
	struct dump_params params;

	if (copy_from_user(&params, arg, sizeof(params))) {
		ts_err(NULL, "failed copy dump dev params from user space");
		return -EFAULT;
	}

	ts_info(NULL, "default dump addr 0x%x, len %d, max buf num %d, interval %d",
		dump_dev->address, dump_dev->frame_size,
		dump_dev->max_frame_num, dump_dev->sample_interval);

	ts_info(NULL, "new params address 0x%x, len %d, max_frame_num %d, interval %d",
		params.address, params.frame_size, params.max_frame_num,
		params.sample_interval);

	if (params.address) {
		dump_dev->address = params.address;
		ts_info(NULL, "update dump addr 0x%x", dump_dev->address);
	}
	if (params.frame_size) {
		dump_dev->frame_size = params.frame_size;
		ts_info(NULL, "update dump len %d", dump_dev->frame_size);
	}
	if (params.max_frame_num) {
		dump_dev->max_frame_num = params.max_frame_num;
		ts_info(NULL, "update max frame num %d", dump_dev->max_frame_num);
	}
	if (params.sample_interval) {
		dump_dev->sample_interval = params.sample_interval;
		ts_info(NULL, "update sample interval %d", dump_dev->sample_interval);
	}
	dump_dev->buffer_size = dump_dev->frame_size * dump_dev->max_frame_num;

	if (!dump_dev->address || !dump_dev->frame_size ||
	    dump_dev->frame_size > GOODIX_MAX_DUMP_FRAME_SIZE ||
	    !dump_dev->max_frame_num) {
		ts_err(NULL, "invalid dump params");
		return -EINVAL;
	}

	if (goodix_dump_buf_init(dump_dev)) {
		ts_err(NULL, "failed init dump buf");
		return -EFAULT;
	}

	return 0;
}

static int goodix_dump_start(struct goodix_dump_dev *dump_dev)
{
	if (goodix_dump_cmd_enable(dump_dev, 1)) {
		ts_err(NULL, "failed send dump enable command");
		return -EFAULT;
	}

	dump_dev->mode = DUMP_MODE_ON;
	return 0;
}

static int goodix_dump_stop(struct goodix_dump_dev *dump_dev)
{
	dump_dev->need_wakeup = 1;
	wake_up_interruptible(&dump_dev->waitq);
	if (dump_dev->mode == DUMP_MODE_OFF)
		return 0;

	dump_dev->mode = DUMP_MODE_OFF;

	if (goodix_dump_cmd_enable(dump_dev, 0))
		ts_err(NULL, "failed send dump disable command");

	return 0;
}

static long goodix_dump_dev_ioctl(struct file *filp, unsigned int cmd,
				  unsigned long arg)
{
	int ret = -EINVAL;
	struct goodix_dump_dev *dump_dev = filp->private_data;
	struct dump_buf_header *dbh;
	struct dump_params params;

	if (_IOC_TYPE(cmd) != GOODIX_TS_IOC_MAGIC) {
		ts_err(NULL, "Bad magic num:%c", _IOC_TYPE(cmd));
		return -ENOTTY;
	}

	switch (cmd & NEGLECT_SIZE_MASK) {
	case GTP_DUMP_DEV_VER:
		ret = copy_to_user((void *)arg, &goodix_dump_dev_ver,
				   sizeof(u16));
		if (ret)
			ts_err(NULL, "failed copy driver version info to user, %d",
			       ret);
		break;
	case GTP_DUMP_PARAMS_GET:
		params.address = dump_dev->address;
		params.frame_size = dump_dev->frame_size;
		params.max_frame_num = dump_dev->max_frame_num;
		params.sample_interval = dump_dev->sample_interval;

		ret = copy_to_user((void *)arg, &params, sizeof(params));
		if (ret)
			ts_err(NULL, "failed copy driver params to user, %d", ret);
		break;
	case GTP_DUMP_PARAMS_SET:
		ret = goodix_dump_params_set(dump_dev, (void __user *)arg);
		break;
	case GTP_DUMP_START:
		ret = goodix_dump_start(dump_dev);
		if (ret)
			ts_err(NULL, "failed enable dump dev");
		break;
	case GTP_DUMP_STOP:
		ret = goodix_dump_stop(dump_dev);
		if (ret)
			ts_err(NULL, "failed disable dump");
		break;
	case GTP_DUMP_FRAME_GET:
		dbh = dump_dev->db_header;
		if (dbh->head != dbh->tail)
			return 0;
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		dump_dev->need_wakeup = 0;
		ret = wait_event_interruptible(dump_dev->waitq,
					       dump_dev->need_wakeup);
		break;
	}
	return ret;
}

#ifdef CONFIG_COMPAT
static long goodix_dump_dev_compat_ioctl(struct file *file, unsigned int cmd,
					 unsigned long arg)
{
	void __user *arg32 = compat_ptr(arg);

	if (!file->f_op || !file->f_op->unlocked_ioctl)
		return -ENOTTY;
	return file->f_op->unlocked_ioctl(file, cmd, (unsigned long)arg32);
}
#endif

static int goodix_dump_dev_open(struct inode *inode, struct file *filp)
{
	filp->private_data = &g_dump_dev;
	return 0;
}

static int goodix_dump_dev_release(struct inode *inode, struct file *filp)
{
	struct goodix_dump_dev *dump_dev = filp->private_data;

	goodix_dump_stop(dump_dev);
	goodix_dump_buf_deinit(dump_dev);
	return 0;
}

static int goodix_dump_dev_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct goodix_dump_dev *dump_dev = file->private_data;
	unsigned long size = vma->vm_end - vma->vm_start;

	if (size > PAGE_ALIGN(dump_dev->buffer_size +
			      sizeof(struct dump_buf_header))) {
		ts_err(NULL, "buffer size exceed limit %zu > %zu", size,
		       PAGE_ALIGN(dump_dev->buffer_size +
				  sizeof(struct dump_buf_header)));
		return -EINVAL;
	}

	if (remap_pfn_range(vma, vma->vm_start, page_to_pfn(dump_dev->page),
			    size, vma->vm_page_prot)) {
		ts_err(NULL, "failed remap memory");
		return -EAGAIN;
	}

	return 0;
}

static const struct file_operations goodix_dump_fops = {
	.owner = THIS_MODULE,
	.open = goodix_dump_dev_open,
	.release = goodix_dump_dev_release,
	.mmap = goodix_dump_dev_mmap,
	.unlocked_ioctl = goodix_dump_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = goodix_dump_dev_compat_ioctl,
#endif
};

int goodix_dump_dev_init(struct goodix_ts_core *cd)
{
	struct device *parent = cd->bus->dev;
	struct device *dev;
	int ret;

	ts_info(NULL, "goodix dump dev init: v%d.%d", GOODIX_DUMP_DEV_VER_MAJOR,
		GOODIX_DUMP_DEV_VER_MINOR);

	ret = alloc_chrdev_region(&goodix_dump_devno, 0, 1, DEVICE_NAME);
	if (ret) {
		ts_err(NULL, "failed alloc chrdev region, ret %d", ret);
		return ret;
	}

#if (KERNEL_VERSION(6, 4, 0) <= LINUX_VERSION_CODE)
	goodix_dump_class = class_create(DEVICE_NAME);
#else
	goodix_dump_class = class_create(THIS_MODULE, DEVICE_NAME);
#endif
	if (IS_ERR(goodix_dump_class)) {
		ret = PTR_ERR(goodix_dump_class);
		ts_err(NULL, "failed create class %s, ret %d", DEVICE_NAME, ret);
		goto err_class;
	}

	cdev_init(&goodix_dump_cdev, &goodix_dump_fops);
	goodix_dump_cdev.owner = THIS_MODULE;
	ret = cdev_add(&goodix_dump_cdev, goodix_dump_devno, 1);
	if (ret) {
		ts_err(NULL, "failed add cdev %s, ret %d", DEVICE_NAME, ret);
		goto err_cdev;
	}

	dev = device_create(goodix_dump_class, parent, goodix_dump_devno,
			    NULL, DEVICE_NAME);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		ts_err(NULL, "failed create device %s, ret %d", DEVICE_NAME, ret);
		goto err_device;
	}

	init_waitqueue_head(&g_dump_dev.waitq);
	g_dump_dev.cd = cd;
	g_dump_dev.address = cd->ic_info.misc.frame_data_addr;
	g_dump_dev.frame_size = GOODIX_DUMP_HEADER_SIZE +
				cd->ic_info.parm.drv_num *
					cd->ic_info.parm.sen_num * sizeof(u16);
	g_dump_dev.max_frame_num = GOODIX_MAX_DUMP_FRAME_NUM;
	g_dump_dev.sample_interval = GOODIX_DEFAULT_DUMP_INTERVAL;
	return 0;

err_device:
	cdev_del(&goodix_dump_cdev);
err_cdev:
	class_destroy(goodix_dump_class);
err_class:
	unregister_chrdev_region(goodix_dump_devno, 1);
	return ret;
}

void goodix_dump_dev_exit(void)
{
	ts_info(NULL, "goodix dump dev exit");
	goodix_dump_stop(&g_dump_dev);
	goodix_dump_buf_deinit(&g_dump_dev);
	device_destroy(goodix_dump_class, goodix_dump_devno);
	cdev_del(&goodix_dump_cdev);
	class_destroy(goodix_dump_class);
	unregister_chrdev_region(goodix_dump_devno, 1);
}
