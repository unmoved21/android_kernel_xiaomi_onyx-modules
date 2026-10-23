/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __LINUX_BTFM_I2S_HW_INTERFACE_H
#define __LINUX_BTFM_I2S_HW_INTERFACE_H

#include "btfm_i2s.h"
#include <sound/soc.h>

enum Codec {
	SBC = 0,
	AAC,
	LDAC,
	APTX,
	APTX_HD,
	APTX_AD,
	LC3,
	APTX_AD_SPEECH,
	LC3_VOICE,
	APTX_AD_QLEA,
	APTX_AD_R4,
	RVP,
	SSC,
	LHDC,
	APTX_PLUS,
	NO_CODEC
};

static const char * const codec_text[] = {"CODEC_TYPE_SBC", "CODEC_TYPE_AAC",
					   "CODEC_TYPE_LDAC", "CODEC_TYPE_APTX",
					   "CODEC_TYPE_APTX_HD", "CODEC_TYPE_APTX_AD",
					   "CODEC_TYPE_LC3", "CODEC_TYPE_APTX_AD_SPEECH",
					   "CODEC_TYPE_LC3_VOICE", "CODEC_TYPE_APTX_AD_QLEA",
					   "CODEC_TYPE_APTX_AD_R4", "CODEC_TYPE_RVP",
					   "CODEC_TYPE_SSC", "CODEC_TYPE_LHDC",
					   "CODEC_TYPE_APTX_PLUS", "CODEC_TYPE_INVALID"};

static SOC_ENUM_SINGLE_EXT_DECL(codec_display, codec_text);

void btfm_i2s_get_sampling_rate(uint32_t *sampling_rate);
int btfm_i2s_register_hw_ep(struct btfmi2s *btfm_i2s);
void btfm_i2s_unregister_hwep(void);

#endif /* __LINUX_BTFM_I2S_HW_INTERFACE_H */
