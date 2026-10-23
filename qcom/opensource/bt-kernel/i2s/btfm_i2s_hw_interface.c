// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "btfm_i2s.h"
#include "btfm_i2s_hw_interface.h"
#include "btfm_codec_hw_interface.h"

/*
 * LPAIF_AUD (0x00) — LPASS Audio Interface instance for BT I2S.
 * inf_index=0 selects I2S interface 0 (BT audio port).
 */
#define LPAIF_AUD	0x00
#define I2S_INF_INDEX	0

static uint8_t usecase_codec;
static uint8_t rx_sd_line_idx;
static uint8_t tx_sd_line_idx;
int btfm_feedback_ch_setting;

/* --------------------------------------------------------------------------
 * Component driver stubs
 * --------------------------------------------------------------------------
 */

static int btfm_i2s_hwep_write(struct snd_soc_component *codec,
				unsigned int reg, unsigned int value)
{
	BTFMI2S_DBG("reg=0x%x val=0x%x", reg, value);
	return 0;
}

static unsigned int btfm_i2s_hwep_read(struct snd_soc_component *codec,
					unsigned int reg)
{
	BTFMI2S_DBG("reg=0x%x", reg);
	return 0;
}

static int btfm_i2s_hwep_probe(struct snd_soc_component *codec)
{
	BTFMI2S_INFO("codec=%p usecase_codec=%u rx_sd_line_idx=%u tx_sd_line_idx=%u",
		     codec, usecase_codec, rx_sd_line_idx, tx_sd_line_idx);
	return 0;
}

static void btfm_i2s_hwep_remove(struct snd_soc_component *codec)
{
	BTFMI2S_INFO("codec=%p", codec);
}

/* --------------------------------------------------------------------------
 * Mixer control callbacks
 * --------------------------------------------------------------------------
 */

static int btfm_i2s_get_feedback_ch_setting(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol)
{
	BTFMI2S_DBG("current feedback ch setting: %d", btfm_feedback_ch_setting);
	ucontrol->value.integer.value[0] = btfm_feedback_ch_setting;
	return 1;
}

static int btfm_i2s_put_feedback_ch_setting(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol)
{
	btfm_feedback_ch_setting = ucontrol->value.integer.value[0];
	BTFMI2S_DBG("feedback ch setting changed: %d", btfm_feedback_ch_setting);
	return 1;
}

static int btfm_i2s_get_codec_type(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	BTFMI2S_DBG("current codec type: %u (%s)", usecase_codec,
		    codec_text[usecase_codec]);
	ucontrol->value.integer.value[0] = usecase_codec;
	return 1;
}

static int btfm_i2s_put_codec_type(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	uint8_t prev = usecase_codec;

	usecase_codec = ucontrol->value.integer.value[0];
	BTFMI2S_INFO("codec type changed: %u (%s) -> %u (%s)",
		     prev, codec_text[prev],
		     usecase_codec, codec_text[usecase_codec]);
	return 1;
}

static int btfm_i2s_get_rx_sd_line_idx(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	BTFMI2S_DBG("current RX SD line idx: %u", rx_sd_line_idx);
	ucontrol->value.integer.value[0] = rx_sd_line_idx;
	return 1;
}

static int btfm_i2s_put_rx_sd_line_idx(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	uint8_t prev = rx_sd_line_idx;

	rx_sd_line_idx = ucontrol->value.integer.value[0];
	BTFMI2S_INFO("RX SD line idx changed: %u -> %u", prev, rx_sd_line_idx);
	return 1;
}

static int btfm_i2s_get_tx_sd_line_idx(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	BTFMI2S_DBG("current TX SD line idx: %u", tx_sd_line_idx);
	ucontrol->value.integer.value[0] = tx_sd_line_idx;
	return 1;
}

static int btfm_i2s_put_tx_sd_line_idx(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	uint8_t prev = tx_sd_line_idx;

	tx_sd_line_idx = ucontrol->value.integer.value[0];
	BTFMI2S_INFO("TX SD line idx changed: %u -> %u", prev, tx_sd_line_idx);
	return 1;
}

static struct snd_kcontrol_new status_controls[] = {
	SOC_SINGLE_EXT("BT set feedback channel", 0, 0, 1, 0,
		       btfm_i2s_get_feedback_ch_setting,
		       btfm_i2s_put_feedback_ch_setting),
	SOC_ENUM_EXT("BT codec type", codec_display,
		     btfm_i2s_get_codec_type, btfm_i2s_put_codec_type),
	SOC_SINGLE_EXT("BT I2S RX SD line", SND_SOC_NOPM, 0, 8, 0,
		       btfm_i2s_get_rx_sd_line_idx, btfm_i2s_put_rx_sd_line_idx),
	SOC_SINGLE_EXT("BT I2S TX SD line", SND_SOC_NOPM, 0, 8, 0,
		       btfm_i2s_get_tx_sd_line_idx, btfm_i2s_put_tx_sd_line_idx),
};

/* --------------------------------------------------------------------------
 * Sampling rate adjustment (codec-type aware)
 * --------------------------------------------------------------------------
 */

void btfm_i2s_get_sampling_rate(uint32_t *sampling_rate)
{
	uint8_t codec_types_avb = ARRAY_SIZE(codec_text);
	uint32_t original_rate = *sampling_rate;

	BTFMI2S_DBG("enter: codec=%u (%s) rate=%u",
		    usecase_codec, codec_text[usecase_codec], *sampling_rate);

	if (usecase_codec > (codec_types_avb - 1)) {
		BTFMI2S_ERR("codec %u out of range (max %u), keeping rate=%u",
			    usecase_codec, codec_types_avb - 1, *sampling_rate);
		return;
	}

	if (*sampling_rate == 44100 || *sampling_rate == 48000) {
		if (usecase_codec == LDAC ||
		    usecase_codec == APTX_AD ||
		    usecase_codec == LHDC) {
			*sampling_rate = (*sampling_rate) * 2;
			BTFMI2S_INFO("codec %s: doubled rate %u -> %u",
				     codec_text[usecase_codec],
				     original_rate, *sampling_rate);
		}
	}

	if (usecase_codec == LC3_VOICE ||
	    usecase_codec == APTX_AD_SPEECH ||
	    usecase_codec == LC3 ||
	    usecase_codec == APTX_AD_R4 ||
	    usecase_codec == RVP) {
		*sampling_rate = 96000;
		BTFMI2S_INFO("codec %s: forced rate %u -> 96000",
			     codec_text[usecase_codec], original_rate);
	}

	if (usecase_codec == APTX_AD_QLEA || usecase_codec == APTX_PLUS) {
		*sampling_rate = 192000;
		BTFMI2S_INFO("codec %s: forced rate %u -> 192000",
			     codec_text[usecase_codec], original_rate);
	}

	BTFMI2S_INFO("current usecase codec type %s and sampling rate:%u khz",
		     codec_text[usecase_codec], *sampling_rate);
}

/* --------------------------------------------------------------------------
 * DAI ops
 * --------------------------------------------------------------------------
 */

/* hwep_startup must return 0; btfmcodec_hwep_startup() returns -1 if NULL */
static int btfm_i2s_dai_startup(void *dai)
{
	BTFMI2S_INFO("startup: pbtfmi2s=%p initialized=%d",
		     pbtfmi2s, pbtfmi2s ? pbtfmi2s->initialized : -1);
	return 0;
}

static void btfm_i2s_dai_shutdown(void *dai, int id)
{
	BTFMI2S_INFO("shutdown: dai_id=%d", id);
	if (pbtfmi2s)
		BTFMI2S_DBG("shutdown: sample_rate=%u bps=%u ch=%u",
			    pbtfmi2s->sample_rate, pbtfmi2s->bps,
			    pbtfmi2s->num_channels);
}

static int btfm_i2s_dai_set_channel_map(void *dai,
					 unsigned int tx_num,
					 unsigned int *tx_slot,
					 unsigned int rx_num,
					 unsigned int *rx_slot)
{
	BTFMI2S_DBG("set_channel_map: tx_num=%u tx_slot=0x%x rx_num=%u rx_slot=0x%x",
		    tx_num, tx_slot ? *tx_slot : 0,
		    rx_num, rx_slot ? *rx_slot : 0);
	return 0;
}

static int btfm_i2s_dai_hw_params(void *dai, uint32_t bps,
				   uint32_t direction, uint8_t num_channels)
{
	BTFMI2S_INFO("hw_params: bps=%u direction=%u num_channels=%u",
		     bps, direction, num_channels);

	if (!pbtfmi2s) {
		BTFMI2S_ERR("hw_params: pbtfmi2s is NULL - cannot store params");
		return 0;
	}

	pbtfmi2s->bps          = bps;
	pbtfmi2s->direction    = direction;
	pbtfmi2s->num_channels = num_channels;

	BTFMI2S_DBG("hw_params: stored bps=%u direction=%u num_channels=%u",
		    pbtfmi2s->bps, pbtfmi2s->direction, pbtfmi2s->num_channels);
	return 0;
}

static int btfm_i2s_dai_prepare(void *dai, uint32_t sampling_rate,
				 uint32_t direction, int id)
{
	BTFMI2S_INFO("prepare: id=%d rate=%u direction=%u codec=%s",
		     id, sampling_rate, direction, codec_text[usecase_codec]);

	btfm_i2s_get_sampling_rate(&sampling_rate);

	if (!pbtfmi2s) {
		BTFMI2S_ERR("prepare: pbtfmi2s is NULL - cannot store sample_rate");
	} else {
		pbtfmi2s->sample_rate = sampling_rate;
		BTFMI2S_DBG("prepare: stored sample_rate=%u bps=%u ch=%u lpaif=0x%02x inf=%u",
			    pbtfmi2s->sample_rate, pbtfmi2s->bps,
			    pbtfmi2s->num_channels, LPAIF_AUD, I2S_INF_INDEX);
	}

	return 0;
}

static int btfm_i2s_dai_get_channel_map(void *dai,
					 unsigned int *tx_num,
					 unsigned int *tx_slot,
					 unsigned int *rx_num,
					 unsigned int *rx_slot,
					 int id)
{
	BTFMI2S_DBG("get_channel_map: id=%d", id);

	*rx_slot = 0;
	*tx_slot = 0;
	*rx_num  = 0;
	*tx_num  = 0;

	if (!pbtfmi2s) {
		BTFMI2S_ERR("get_channel_map: pbtfmi2s is NULL");
		return 0;
	}

	switch (id) {
	case FMAUDIO_TX:
	case BTAUDIO_TX:
	case BTAUDIO_TX2:
		*tx_num  = pbtfmi2s->num_channels;
		*tx_slot = (pbtfmi2s->num_channels == 2) ? TWO_CHANNEL_MASK : ONE_CHANNEL_MASK;
		BTFMI2S_DBG("get_channel_map: TX id=%d tx_num=%u tx_slot=0x%x",
			    id, *tx_num, *tx_slot);
		break;
	case BTAUDIO_RX:
	case BTAUDIO_RX2:
		*rx_num  = pbtfmi2s->num_channels;
		*rx_slot = (pbtfmi2s->num_channels == 2) ? TWO_CHANNEL_MASK : ONE_CHANNEL_MASK;
		BTFMI2S_DBG("get_channel_map: RX id=%d rx_num=%u rx_slot=0x%x",
			    id, *rx_num, *rx_slot);
		break;
	default:
		BTFMI2S_ERR("get_channel_map: unsupported DAI id=%d", id);
		return -EINVAL;
	}

	return 0;
}

static int btfm_i2s_dai_get_configs(void *dai, void *config, uint8_t id)
{
	struct hwep_i2s_configurations *hwep_config =
		(struct hwep_i2s_configurations *)config;

	BTFMI2S_INFO("get_configs: id=%u codec=%s", id, codec_text[usecase_codec]);

	hwep_config->stream_id = id;
	hwep_config->codec_id  = usecase_codec;

	if (!pbtfmi2s) {
		BTFMI2S_ERR("get_configs: pbtfmi2s is NULL");
		return -EINVAL;
	}

	hwep_config->sample_rate  = pbtfmi2s->sample_rate;
	hwep_config->bit_width    = (uint8_t)pbtfmi2s->bps;
	hwep_config->num_channels = pbtfmi2s->num_channels;
	if (id == BTAUDIO_RX || id == BTAUDIO_RX2)
		hwep_config->channel_mode = rx_sd_line_idx;
	else
		hwep_config->channel_mode = tx_sd_line_idx;
	if (pbtfmi2s->num_channels == 2)
		hwep_config->channel_mask = TWO_CHANNEL_MASK;
	else
		hwep_config->channel_mask = ONE_CHANNEL_MASK;

	hwep_config->lpaif_type = LPAIF_AUD;
	hwep_config->intf_idx   = I2S_INF_INDEX;

	BTFMI2S_INFO("sid=%u rate=%u bw=%u ch=%u mode=%u mask=0x%x codec=%u lpaif=0x%02x if=%u",
		     hwep_config->stream_id, hwep_config->sample_rate,
		     hwep_config->bit_width, hwep_config->num_channels,
		     hwep_config->channel_mode, hwep_config->channel_mask,
		     hwep_config->codec_id, hwep_config->lpaif_type,
		     hwep_config->intf_idx);
	return 1;
}

/* --------------------------------------------------------------------------
 * DAI ops / driver / component driver tables
 * --------------------------------------------------------------------------
 */

static struct hwep_dai_ops btfmi2s_hw_dai_ops = {
	.hwep_startup         = btfm_i2s_dai_startup,
	.hwep_shutdown        = btfm_i2s_dai_shutdown,
	.hwep_hw_params       = btfm_i2s_dai_hw_params,
	.hwep_prepare         = btfm_i2s_dai_prepare,
	.hwep_set_channel_map = btfm_i2s_dai_set_channel_map,
	.hwep_get_channel_map = btfm_i2s_dai_get_channel_map,
	.hwep_get_configs     = btfm_i2s_dai_get_configs,
	.hwep_codectype       = &usecase_codec,
};

static struct hwep_dai_driver btfmi2s_dai_driver[] = {
	{	/* FM audio: FM -> LPASS */
		.dai_name = "btaudio_fm_tx",
		.id       = FMAUDIO_TX,
		.capture  = {
			.stream_name  = "FM I2S TX Capture",
			.rates        = SNDRV_PCM_RATE_48000,
			.formats      = SNDRV_PCM_FMTBIT_S16_LE,
			.rate_max     = 48000,
			.rate_min     = 48000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.dai_ops = &btfmi2s_hw_dai_ops,
	},
	{	/* Bluetooth SCO voice uplink / A2DP capture: BT -> LPASS */
		.dai_name = "btaudio_tx",
		.id       = BTAUDIO_TX,
		.capture  = {
			.stream_name  = "BT Audio I2S Tx Capture",
			/* 8 KHz or 16 KHz */
			.rates        = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000
				| SNDRV_PCM_RATE_8000_192000
				| SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000
				| SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000
				| SNDRV_PCM_RATE_192000,
			.formats      = SNDRV_PCM_FMTBIT_S16_LE, /* 16 bits */
			.rate_max     = 192000,
			.rate_min     = 8000,
			.channels_min = 1,
			.channels_max = 1,
		},
		.dai_ops = &btfmi2s_hw_dai_ops,
	},
	{	/* Bluetooth SCO voice downlink / A2DP playback: LPASS -> BT */
		.dai_name = "btaudio_rx",
		.id       = BTAUDIO_RX,
		.playback = {
			.stream_name  = "BT Audio I2S Rx Playback",
			/* 8/16/44.1/48/88.2/96 KHz */
			.rates        = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000
				| SNDRV_PCM_RATE_8000_192000
				| SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000
				| SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000
				| SNDRV_PCM_RATE_192000,
			.formats      = SNDRV_PCM_FMTBIT_S16_LE, /* 16 bits */
			.rate_max     = 192000,
			.rate_min     = 8000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.dai_ops = &btfmi2s_hw_dai_ops,
	},
	{	/* Bluetooth A2DP sink / HFP client capture: BT -> LPASS (TX2) */
		.dai_name = "btaudio_tx2",
		.id       = BTAUDIO_TX2,
		.capture  = {
			.stream_name  = "BT Audio I2S Tx2 Capture",
			/* 8/16/44.1/48/88.2/96/192 KHz */
			.rates        = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000
				| SNDRV_PCM_RATE_8000_192000
				| SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000
				| SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000
				| SNDRV_PCM_RATE_192000,
			.formats      = SNDRV_PCM_FMTBIT_S16_LE, /* 16 bits */
			.rate_max     = 192000,
			.rate_min     = 8000,
			.channels_min = 1,
			.channels_max = 1,
		},
		.dai_ops = &btfmi2s_hw_dai_ops,
	},
	{	/* Bluetooth audio downlink2: LPASS -> BT (RX2) */
		.dai_name = "btaudio_rx2",
		.id       = BTAUDIO_RX2,
		.playback = {
			.stream_name  = "BT Audio I2S Rx2 Playback",
			.rates        = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000
				| SNDRV_PCM_RATE_8000_192000
				| SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000
				| SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000
				| SNDRV_PCM_RATE_192000,
			.formats      = SNDRV_PCM_FMTBIT_S16_LE, /* 16 bits */
			.rate_max     = 192000,
			.rate_min     = 8000,
			.channels_min = 1,
			.channels_max = 2,
		},
		.dai_ops = &btfmi2s_hw_dai_ops,
	},
};

static struct hwep_comp_drv btfmi2s_hw_driver = {
	.hwep_probe  = btfm_i2s_hwep_probe,
	.hwep_remove = btfm_i2s_hwep_remove,
	.hwep_read   = btfm_i2s_hwep_read,
	.hwep_write  = btfm_i2s_hwep_write,
};

/* --------------------------------------------------------------------------
 * Register / unregister with btfmcodec HWEP interface
 * --------------------------------------------------------------------------
 */

int btfm_i2s_register_hw_ep(struct btfmi2s *btfm_i2s)
{
	struct hwep_data *hwep_info;
	int ret = 0;

	BTFMI2S_INFO("registering with BTFMCODEC HWEP interface");

	hwep_info = kzalloc(sizeof(struct hwep_data), GFP_KERNEL);
	if (!hwep_info) {
		BTFMI2S_ERR("kzalloc failed for hwep_data");
		return -ENOMEM;
	}

	hwep_info->dev            = btfm_i2s ? btfm_i2s->dev : NULL;
	hwep_info->drv            = &btfmi2s_hw_driver;
	hwep_info->dai_drv        = btfmi2s_dai_driver;
	hwep_info->num_dai        = ARRAY_SIZE(btfmi2s_dai_driver);
	hwep_info->mixer_ctrl     = status_controls;
	hwep_info->num_mixer_ctrl = ARRAY_SIZE(status_controls);
	strscpy(hwep_info->driver_name, I2S_COMPATIBLE_STR, DEVICE_NAME_MAX_LEN);

	hwep_info->flags = BIT(BTADV_CONFIGURE_I2S);

	BTFMI2S_DBG("hwep_info: dev=%p driver_name=%s num_dai=%d num_mixer=%d flags=0x%lx",
		    hwep_info->dev, hwep_info->driver_name,
		    hwep_info->num_dai, hwep_info->num_mixer_ctrl,
		    hwep_info->flags);

	ret = btfmcodec_register_hw_ep(hwep_info);
	kfree(hwep_info);

	if (ret)
		BTFMI2S_ERR("btfmcodec_register_hw_ep FAILED ret=%d", ret);
	else
		BTFMI2S_INFO("btfmcodec_register_hw_ep SUCCESS");

	return ret;
}

void btfm_i2s_unregister_hwep(void)
{
	BTFMI2S_INFO("unregistering from BTFMCODEC HWEP interface");
	btfmcodec_unregister_hw_ep(I2S_COMPATIBLE_STR);
	BTFMI2S_INFO("unregister done");
}

MODULE_DESCRIPTION("BTFM I2S Codec driver");
MODULE_LICENSE("GPL");
