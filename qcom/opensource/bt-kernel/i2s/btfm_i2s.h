/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef BTFM_I2S_H
#define BTFM_I2S_H

#include <linux/types.h>

#define I2S_COMPATIBLE_STR	"btfmi2s_slave"

#define BTFMI2S_DBG(fmt, arg...)  pr_debug("%s: " fmt "\n", __func__, ## arg)
#define BTFMI2S_INFO(fmt, arg...) pr_info("%s: " fmt "\n", __func__, ## arg)
#define BTFMI2S_ERR(fmt, arg...)  pr_err("%s: " fmt "\n", __func__, ## arg)

/* Channel mask definitions */
#define ONE_CHANNEL_MASK	1
#define TWO_CHANNEL_MASK	3

/* I2S SD line index / channel mode selectors */
enum i2s_channel_mode {
	I2S_CHANNEL_MODE_SD0         = 1,
	I2S_CHANNEL_MODE_SD1         = 2,
	I2S_CHANNEL_MODE_SD2         = 3,
	I2S_CHANNEL_MODE_SD3         = 4,
	I2S_CHANNEL_MODE_SD0_AND_SD1 = 5,
	I2S_CHANNEL_MODE_SD2_AND_SD3 = 6,
	I2S_CHANNEL_MODE_6_CHANNELS  = 7,
	I2S_CHANNEL_MODE_8_CHANNELS  = 8,
};

/* DAI IDs */
enum {
	FMAUDIO_TX = 0,
	BTAUDIO_TX,
	BTAUDIO_RX,
	BTAUDIO_RX2,
	BTAUDIO_TX2,
	BTFM_NUM_CODEC_DAIS
};

/**
 * struct btfmi2s - per-instance driver state
 * @dev:          device pointer (NULL for module-init based driver)
 * @initialized:  hw init done flag
 * @sample_rate:  current sample rate
 * @bps:          bits per sample
 * @direction:    stream direction
 * @num_channels: channel count
 */
struct btfmi2s {
	struct device *dev;
	bool initialized;
	uint32_t sample_rate;
	uint32_t bps;
	uint16_t direction;
	uint8_t num_channels;
};

extern struct btfmi2s *pbtfmi2s;

#endif /* BTFM_I2S_H */
