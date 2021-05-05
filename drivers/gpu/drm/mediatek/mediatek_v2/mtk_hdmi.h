/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef _MTK_HDMI_CTRL_H
#define _MTK_HDMI_CTRL_H

#include <linux/hdmi.h>
//#include <drm/drmP.h>
#include <drm/drm_bridge.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <sound/hdmi-codec.h>
#include <linux/clk.h>
#include <linux/regmap.h>


#define NCTS_BYTES	7

enum mtk_hdmi_clk_id {
	MTK_HDMI_CLK_HDMI_TXPLL,
	MTK_HDIM_HDCP_SEL,
	MTK_HDMI_HDCP_24M_SEL,
	MTK_HDMI_HD20_HDCP_C_SEL,
	MTK_HDMI_VPP_SPLIT_HDMI,
	MTK_HDMI_CLK_COUNT,
};

enum hdmi_aud_input_type {
	HDMI_AUD_INPUT_I2S = 0,
	HDMI_AUD_INPUT_SPDIF,
};

enum hdmi_aud_i2s_fmt {
	HDMI_I2S_MODE_RJT_24BIT = 0,
	HDMI_I2S_MODE_RJT_16BIT,
	HDMI_I2S_MODE_LJT_24BIT,
	HDMI_I2S_MODE_LJT_16BIT,
	HDMI_I2S_MODE_I2S_24BIT,
	HDMI_I2S_MODE_I2S_16BIT
};

enum hdmi_aud_mclk {
	HDMI_AUD_MCLK_128FS,
	HDMI_AUD_MCLK_192FS,
	HDMI_AUD_MCLK_256FS,
	HDMI_AUD_MCLK_384FS,
	HDMI_AUD_MCLK_512FS,
	HDMI_AUD_MCLK_768FS,
	HDMI_AUD_MCLK_1152FS,
};

enum hdmi_aud_channel_type {
	HDMI_AUD_CHAN_TYPE_1_0 = 0,
	HDMI_AUD_CHAN_TYPE_1_1,
	HDMI_AUD_CHAN_TYPE_2_0,
	HDMI_AUD_CHAN_TYPE_2_1,
	HDMI_AUD_CHAN_TYPE_3_0,
	HDMI_AUD_CHAN_TYPE_3_1,
	HDMI_AUD_CHAN_TYPE_4_0,
	HDMI_AUD_CHAN_TYPE_4_1,
	HDMI_AUD_CHAN_TYPE_5_0,
	HDMI_AUD_CHAN_TYPE_5_1,
	HDMI_AUD_CHAN_TYPE_6_0,
	HDMI_AUD_CHAN_TYPE_6_1,
	HDMI_AUD_CHAN_TYPE_7_0,
	HDMI_AUD_CHAN_TYPE_7_1,
	HDMI_AUD_CHAN_TYPE_3_0_LRS,
	HDMI_AUD_CHAN_TYPE_3_1_LRS,
	HDMI_AUD_CHAN_TYPE_4_0_CLRS,
	HDMI_AUD_CHAN_TYPE_4_1_CLRS,
	HDMI_AUD_CHAN_TYPE_6_1_CS,
	HDMI_AUD_CHAN_TYPE_6_1_CH,
	HDMI_AUD_CHAN_TYPE_6_1_OH,
	HDMI_AUD_CHAN_TYPE_6_1_CHR,
	HDMI_AUD_CHAN_TYPE_7_1_LH_RH,
	HDMI_AUD_CHAN_TYPE_7_1_LSR_RSR,
	HDMI_AUD_CHAN_TYPE_7_1_LC_RC,
	HDMI_AUD_CHAN_TYPE_7_1_LW_RW,
	HDMI_AUD_CHAN_TYPE_7_1_LSD_RSD,
	HDMI_AUD_CHAN_TYPE_7_1_LSS_RSS,
	HDMI_AUD_CHAN_TYPE_7_1_LHS_RHS,
	HDMI_AUD_CHAN_TYPE_7_1_CS_CH,
	HDMI_AUD_CHAN_TYPE_7_1_CS_OH,
	HDMI_AUD_CHAN_TYPE_7_1_CS_CHR,
	HDMI_AUD_CHAN_TYPE_7_1_CH_OH,
	HDMI_AUD_CHAN_TYPE_7_1_CH_CHR,
	HDMI_AUD_CHAN_TYPE_7_1_OH_CHR,
	HDMI_AUD_CHAN_TYPE_7_1_LSS_RSS_LSR_RSR,
	HDMI_AUD_CHAN_TYPE_6_0_CS,
	HDMI_AUD_CHAN_TYPE_6_0_CH,
	HDMI_AUD_CHAN_TYPE_6_0_OH,
	HDMI_AUD_CHAN_TYPE_6_0_CHR,
	HDMI_AUD_CHAN_TYPE_7_0_LH_RH,
	HDMI_AUD_CHAN_TYPE_7_0_LSR_RSR,
	HDMI_AUD_CHAN_TYPE_7_0_LC_RC,
	HDMI_AUD_CHAN_TYPE_7_0_LW_RW,
	HDMI_AUD_CHAN_TYPE_7_0_LSD_RSD,
	HDMI_AUD_CHAN_TYPE_7_0_LSS_RSS,
	HDMI_AUD_CHAN_TYPE_7_0_LHS_RHS,
	HDMI_AUD_CHAN_TYPE_7_0_CS_CH,
	HDMI_AUD_CHAN_TYPE_7_0_CS_OH,
	HDMI_AUD_CHAN_TYPE_7_0_CS_CHR,
	HDMI_AUD_CHAN_TYPE_7_0_CH_OH,
	HDMI_AUD_CHAN_TYPE_7_0_CH_CHR,
	HDMI_AUD_CHAN_TYPE_7_0_OH_CHR,
	HDMI_AUD_CHAN_TYPE_7_0_LSS_RSS_LSR_RSR,
	HDMI_AUD_CHAN_TYPE_8_0_LH_RH_CS,
	HDMI_AUD_CHAN_TYPE_UNKNOWN = 0xFF
};

enum hdmi_aud_channel_swap_type {
	HDMI_AUD_SWAP_LR,
	HDMI_AUD_SWAP_LFE_CC,
	HDMI_AUD_SWAP_LSRS,
	HDMI_AUD_SWAP_RLS_RRS,
	HDMI_AUD_SWAP_LR_STATUS,
};

struct hdmi_audio_param {
	enum hdmi_audio_coding_type aud_codec;
	enum hdmi_audio_sample_size aud_sampe_size;
	enum hdmi_aud_input_type aud_input_type;
	enum hdmi_aud_i2s_fmt aud_i2s_fmt;
	enum hdmi_aud_mclk aud_mclk;
	enum hdmi_aud_channel_type aud_input_chan_type;
	struct hdmi_codec_params codec_params;
};

enum HDMI_COLOR_DEPTH {
	HDMI_8_BIT,
	HDMI_10_BIT,
	HDMI_12_BIT,
	HDMI_16_BIT
};

enum HDMI_HDR_TYPE {
	HDR_TYPE_SDR = 0,
	HDR_TYPE_HDR10,
	HDR_TYPE_DOLBY_VISION,
	HDR_TYPE_PHILIPS_RESVERD,
	HDR_TYPE_DOLBY_VISION_LOWLATENCY,
	HDR_TYPE_HDR10PLUS_EMP,
	HDR_TYPE_SAMSUNG_HDR10PLUS_VSIF,
};

struct mtk_hdmi_edid {
	unsigned char edid[EDID_LENGTH * 4];
	unsigned char blk_num;
};

struct mtk_hdmi {
	struct drm_bridge bridge;
	struct drm_connector conn;
	struct device *dev;
	struct phy *phy;
	struct mtk_hdmi_phy *hdmi_phy_base;
	struct device *cec_dev;
	struct i2c_adapter *ddc_adpt;
	struct clk *clk[MTK_HDMI_CLK_COUNT];
	struct drm_display_mode mode;
	bool dvi_mode;
	u32 max_hdisplay;
	u32 max_vdisplay;
	/* struct regmap *sys_regmap; */
	/* unsigned int sys_offset; */
	void __iomem *regs;
	enum hdmi_colorspace csp;
	struct hdmi_audio_param aud_param;
	bool audio_enable;
	bool powered;
	bool enabled;
	unsigned int hdmi_irq;
	bool hpd;
	enum HDMI_COLOR_DEPTH color_depth;
	enum HDMI_HDR_TYPE hdr_type;
	struct mtk_hdmi_edid raw_edid;
	struct workqueue_struct *hpd_wq;
	struct work_struct hpd_work;
};

extern struct platform_driver mtk_cec_driver;
extern struct platform_driver mtk_hdmi_ddc_driver;
extern struct platform_driver mtk_hdmi_phy_driver;

extern struct mtk_hdmi *global_mtk_hdmi;
extern void mtk_hdmi_disable_hdcp_encrypt(struct mtk_hdmi *hdmi);
extern void mtk_hdmi_yuv420_downsample(struct mtk_hdmi *hdmi, bool enable);
extern void mtk_hdmi_480p_576p_setting(struct mtk_hdmi *hdmi);
extern void mtk_hdmi_write(struct mtk_hdmi *hdmi, u32 offset, u32 val);
extern void mtk_hdmi_mask(struct mtk_hdmi *hdmi,
	u32 offset, u32 val, u32 mask);
extern bool mtk_hdmi_tmds_over_340M(struct mtk_hdmi *hdmi);

extern void mtk_dpi_pattern_en(bool enable);

#endif /* _MTK_HDMI_CTRL_H */
