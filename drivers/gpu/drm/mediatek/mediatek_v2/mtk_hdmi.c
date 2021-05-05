// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */


#include <linux/arm-smccc.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/phy/phy.h>

#include <drm/drm_scdc_helper.h>
#include <drm/drm_probe_helper.h>

#include "mtk_hdmi_ddc.h"
#include "mtk_hdmi_phy.h"
#include "mtk_cec.h"
#include "mtk_hdmi.h"
#include "mtk_hdmi_regs.h"
#include "mtk_log.h"
#include "mtk_hdmi_hdcp.h"

#include <linux/extcon.h>
#include <../../../../extcon/extcon.h>

#include <linux/debugfs.h>


static const char * const mtk_hdmi_clk_names[MTK_HDMI_CLK_COUNT] = {
	[MTK_HDMI_CLK_HDMI_TXPLL] = "txpll",
	[MTK_HDIM_HDCP_SEL] = "hdcp_sel",
	[MTK_HDMI_HDCP_24M_SEL] = "hdcp24_sel",
	[MTK_HDMI_HD20_HDCP_C_SEL] = "hd20_hdcp_sel",
	[MTK_HDMI_VPP_SPLIT_HDMI] = "split_hdmi",
};

//can.zeng todo verify
struct mtk_hdmi *global_mtk_hdmi;

struct extcon_dev *hdmi_extcon;
static const unsigned int hdmi_cable[] = {
	EXTCON_DISP_HDMI,
	EXTCON_NONE,
};

unsigned char mtk_hdmi_log = 1;

#define HDMI_LOG(fmt, arg...) \
	do {	if (mtk_hdmi_log) { \
		pr_info("[HDMI] %s,%d "fmt, __func__, __LINE__, ##arg); \
		} \
	} while (0)

#define HDMI_FUNC()	\
	do {	if (mtk_hdmi_log) \
		pr_info("[HDMI] %s\n", __func__); \
	} while (0)

static inline struct mtk_hdmi *hdmi_ctx_from_bridge(struct drm_bridge *b)
{
	return container_of(b, struct mtk_hdmi, bridge);
}

static inline struct mtk_hdmi *hdmi_ctx_from_conn(struct drm_connector *c)
{
	return container_of(c, struct mtk_hdmi, conn);
}

static inline struct mtk_hdmi_ddc *hdmi_ddc_ctx_from_mtk_hdmi(struct mtk_hdmi *hdmi)
{
	return container_of(hdmi->ddc_adpt, struct mtk_hdmi_ddc, adap);
}


inline u32 mtk_hdmi_read(struct mtk_hdmi *hdmi, u32 offset)
{
	return readl(hdmi->regs + offset);
}

void mtk_hdmi_write(struct mtk_hdmi *hdmi, u32 offset, u32 val)
{
	writel(val, hdmi->regs + offset);
}

static inline void mtk_hdmi_clear_bits(struct mtk_hdmi *hdmi,
	u32 offset, u32 bits)
{
	void __iomem *reg = hdmi->regs + offset;
	u32 tmp;

	tmp = readl(reg);
	tmp &= ~bits;
	writel(tmp, reg);
}

static inline void mtk_hdmi_set_bits(struct mtk_hdmi *hdmi,
	u32 offset, u32 bits)
{
	void __iomem *reg = hdmi->regs + offset;
	u32 tmp;

	tmp = readl(reg);
	tmp |= bits;
	writel(tmp, reg);
}

void mtk_hdmi_mask(struct mtk_hdmi *hdmi,
	u32 offset, u32 val, u32 mask)
{
	void __iomem *reg = hdmi->regs + offset;
	u32 tmp;

	tmp = readl(reg);
	tmp = (tmp & ~mask) | (val & mask);
	writel(tmp, reg);
}

/****************switch uevent**************************/
struct notify_dev {
	const char *name;
	struct device *dev;
	int index;
	int state;

	ssize_t (*print_name)(struct notify_dev *sdev, char *buf);
	ssize_t (*print_state)(struct notify_dev *sdev, char *buf);
};

struct notify_dev hdmi_switch_data;
struct notify_dev hdmires_switch_data;

struct class *switch_class;
static atomic_t device_count_hdmi;

static ssize_t state_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret;
	struct notify_dev *sdev = (struct notify_dev *)
		dev_get_drvdata(dev);

	if (sdev->print_state) {
		ret = sdev->print_state(sdev, buf);
		if (ret >= 0)
			return ret;
	}
	return sprintf(buf, "%d\n", sdev->state);
}

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int ret;
	struct notify_dev *sdev = (struct notify_dev *)
		dev_get_drvdata(dev);

	if (sdev->print_name) {
		ret = sdev->print_name(sdev, buf);
		if (ret >= 0)
			return ret;
	}
	return sprintf(buf, "%s\n", sdev->name);
}

static DEVICE_ATTR_RO(state);
static DEVICE_ATTR_RO(name);

static int create_switch_class(void)
{
	if (!switch_class) {
		switch_class = class_create(THIS_MODULE, "switch");
		if (IS_ERR(switch_class))
			return PTR_ERR(switch_class);
		atomic_set(&device_count_hdmi, 0);
	}
	return 0;
}

static int hdmitx_uevent_dev_register(struct notify_dev *sdev)
{
	int ret;

	if (!switch_class) {
		ret = create_switch_class();

		if (ret == 0)
			HDMI_LOG("create_switch_class susesess\n");
		else {
			HDMI_LOG("create_switch_class fail\n");
			return ret;
		}
	}

	sdev->index = atomic_inc_return(&device_count_hdmi);
	sdev->dev = device_create(switch_class, NULL,
			MKDEV(0, sdev->index), NULL, sdev->name);

	if (sdev->dev) {
		HDMI_LOG("device create ok,index:0x%x\n", sdev->index);
		ret = 0;
	} else {
		HDMI_LOG("device create fail,index:0x%x\n", sdev->index);
		ret = -1;
	}

	ret = device_create_file(sdev->dev, &dev_attr_state);
	if (ret < 0) {
		device_destroy(switch_class, MKDEV(0, sdev->index));
		HDMI_LOG("switch: Failed to register driver %s\n",
				sdev->name);
	}

	ret = device_create_file(sdev->dev, &dev_attr_name);
	if (ret < 0) {
		device_remove_file(sdev->dev, &dev_attr_state);
		HDMI_LOG("switch: Failed to register driver %s\n",
				sdev->name);
	}

	dev_set_drvdata(sdev->dev, sdev);
	sdev->state = 0;

	return ret;
}

static int hdmi_notify_uevent_user(struct notify_dev *sdev, int state)
{
	char *envp[3];
	char name_buf[120];
	char state_buf[120];

	if (sdev == NULL)
		return -1;

	if (sdev->state != state)
		sdev->state = state;

	snprintf(name_buf, sizeof(name_buf), "SWITCH_NAME=%s", sdev->name);
	envp[0] = name_buf;
	snprintf(state_buf, sizeof(state_buf), "SWITCH_STATE=%d", sdev->state);
	envp[1] = state_buf;
	envp[2] = NULL;
	HDMI_LOG("uevent name:%s ,state:%s\n", envp[0], envp[1]);

	kobject_uevent_env(&sdev->dev->kobj, KOBJ_CHANGE, envp);

	return 0;
}

/****************switch uevent**************************/



static inline void mtk_hdmi_clr_all_int_status(struct mtk_hdmi *hdmi)
{
	/*clear all tx irq*/
	mtk_hdmi_write(hdmi, TOP_INT_CLR00, 0xffffffff);
	mtk_hdmi_write(hdmi, TOP_INT_CLR00, 0x00000000);
	mtk_hdmi_write(hdmi, TOP_INT_CLR01, 0xffffffff);
	mtk_hdmi_write(hdmi, TOP_INT_CLR01, 0x00000000);
}

static inline void mtk_hdmi_disable_all_int(struct mtk_hdmi *hdmi)
{
	/*disable all tx irq*/
	mtk_hdmi_write(hdmi, TOP_INT_MASK00, 0x00000000);
	mtk_hdmi_write(hdmi, TOP_INT_MASK01, 0x00000000);
}

static inline void mtk_hdmi_en_hdcp_reauth_int(
	struct mtk_hdmi *hdmi,  bool enable)
{
	if (enable == true)
		mtk_hdmi_mask(hdmi, TOP_INT_MASK00,
			HDCP2X_RX_REAUTH_REQ_DDCM_INT_UNMASK,
			HDCP2X_RX_REAUTH_REQ_DDCM_INT_UNMASK);
	else
		mtk_hdmi_mask(hdmi, TOP_INT_MASK00,
			HDCP2X_RX_REAUTH_REQ_DDCM_INT_MASK,
			HDCP2X_RX_REAUTH_REQ_DDCM_INT_UNMASK);
}


//can.zeng modification done->void HDMIForceHDCPHPCLevel(void)
static inline void mtk_hdmi_force_hdcp_hpd(struct mtk_hdmi *hdmi)
{
	/* force HDCP HPD to 1*/
	mtk_hdmi_mask(hdmi, HDCP2X_CTRL_0, HDCP2X_HPD_OVR, HDCP2X_HPD_OVR);
	mtk_hdmi_mask(hdmi, HDCP2X_CTRL_0, HDCP2X_HPD_SW, HDCP2X_HPD_SW);
}


//can.zeng modification done->void vDisable_HDCP_Encrypt(void)
void mtk_hdmi_disable_hdcp_encrypt(struct mtk_hdmi *hdmi)
{
	HDMI_FUNC();

	mtk_hdmi_mask(hdmi, HDCP2X_CTRL_0,
		0x0 << HDCP2X_ENCRYPT_EN_SHIFT, HDCP2X_ENCRYPT_EN);
	mtk_hdmi_mask(hdmi, HDCP1X_CTRL,
		0x0 << HDCP1X_ENC_EN_SHIFT, HDCP1X_ENC_EN);
}

void mtk_hdmi_yuv420_downsample(struct mtk_hdmi *hdmi, bool enable)
{
	HDMI_FUNC();

	if (enable == true) {
		HDMI_LOG("Color Space: YUV420 downsample\n");
		mtk_hdmi_mask(hdmi, VID_DOWNSAMPLE_CONFIG,
			C444_C422_CONFIG_ENABLE, C444_C422_CONFIG_ENABLE);
		mtk_hdmi_mask(hdmi, VID_DOWNSAMPLE_CONFIG,
			C422_C420_CONFIG_ENABLE, C422_C420_CONFIG_ENABLE);
		mtk_hdmi_mask(hdmi, VID_DOWNSAMPLE_CONFIG,
			0, C422_C420_CONFIG_BYPASS);
		mtk_hdmi_mask(hdmi, VID_DOWNSAMPLE_CONFIG,
			C422_C420_CONFIG_OUT_CB_OR_CR,
			C422_C420_CONFIG_OUT_CB_OR_CR);
		mtk_hdmi_mask(hdmi, VID_OUT_FORMAT,
			OUTPUT_FORMAT_DEMUX_420_ENABLE,
			OUTPUT_FORMAT_DEMUX_420_ENABLE);
	} else {
		HDMI_LOG("Color Space: not YUV420 downsample\n");
		mtk_hdmi_mask(hdmi, VID_DOWNSAMPLE_CONFIG,
			0, C444_C422_CONFIG_ENABLE);
		mtk_hdmi_mask(hdmi, VID_DOWNSAMPLE_CONFIG,
			0, C422_C420_CONFIG_ENABLE);
		mtk_hdmi_mask(hdmi, VID_DOWNSAMPLE_CONFIG,
			C422_C420_CONFIG_BYPASS, C422_C420_CONFIG_BYPASS);
		mtk_hdmi_mask(hdmi, VID_DOWNSAMPLE_CONFIG,
			0, C422_C420_CONFIG_OUT_CB_OR_CR);
		mtk_hdmi_mask(hdmi, VID_OUT_FORMAT,
			0, OUTPUT_FORMAT_DEMUX_420_ENABLE);
	}
}

bool mtk_hdmi_tmds_over_340M(struct mtk_hdmi *hdmi)
{
	unsigned long pixel_clk, tmds_clk;

	HDMI_FUNC();

	pixel_clk = hdmi->mode.clock * 1000;//in HZ

/* TMDS clk frequency */
	if (hdmi->color_depth == HDMI_8_BIT)
		tmds_clk = pixel_clk;
	else if (hdmi->color_depth == HDMI_10_BIT)
		tmds_clk = pixel_clk * 5 / 4; // *1.25
	else if (hdmi->color_depth == HDMI_12_BIT)
		tmds_clk = pixel_clk * 3 / 2; // *1.5
	else if (hdmi->color_depth == HDMI_16_BIT)
		tmds_clk = pixel_clk * 2; // *2
	else {
		HDMI_LOG("%s, unknown color depth\n", __func__);
		return -EINVAL;
	}
	if ((tmds_clk >= 340000000) && (hdmi->csp != HDMI_COLORSPACE_YUV420)) {
		HDMI_LOG("TMDS over 340M!\n");
		return true;
	}
	HDMI_LOG("TMDS under 340M\n");
	return false;
}

static inline void mtk_hdmi_enable_scrambling(
	struct mtk_hdmi *hdmi, bool enable)
{
	/* DDC write scrambing enable */
	//drm_scdc_set_scrambling(hdmi->ddc_adpt, enable);
	//udelay(100);

	if (enable == true)
		mtk_hdmi_mask(hdmi, TOP_CFG00, SCR_ON | HDMI2_ON,
			SCR_ON | HDMI2_ON);
	else
		mtk_hdmi_mask(hdmi, TOP_CFG00, SCR_OFF | HDMI2_OFF,
			SCR_ON | HDMI2_ON);
}

static inline void mtk_hdmi_high_tmds_clock_ratio(
	struct mtk_hdmi *hdmi, bool enable)
{
	/* DDC write scrambing enable */
	//drm_scdc_set_high_tmds_clock_ratio(hdmi->ddc_adpt, enable);
	/* hw set high bit clock ratio*/
	mtk_tmds_high_bit_clk_ratio(hdmi, enable);
}

static u8 mtk_hdmi_aud_get_chnl_count(enum hdmi_aud_channel_type channel_type)
{
	switch (channel_type) {
	case HDMI_AUD_CHAN_TYPE_1_0:
	case HDMI_AUD_CHAN_TYPE_1_1:
	case HDMI_AUD_CHAN_TYPE_2_0:
		return 2;
	case HDMI_AUD_CHAN_TYPE_2_1:
	case HDMI_AUD_CHAN_TYPE_3_0:
		return 3;
	case HDMI_AUD_CHAN_TYPE_3_1:
	case HDMI_AUD_CHAN_TYPE_4_0:
	case HDMI_AUD_CHAN_TYPE_3_0_LRS:
		return 4;
	case HDMI_AUD_CHAN_TYPE_4_1:
	case HDMI_AUD_CHAN_TYPE_5_0:
	case HDMI_AUD_CHAN_TYPE_3_1_LRS:
	case HDMI_AUD_CHAN_TYPE_4_0_CLRS:
		return 5;
	case HDMI_AUD_CHAN_TYPE_5_1:
	case HDMI_AUD_CHAN_TYPE_6_0:
	case HDMI_AUD_CHAN_TYPE_4_1_CLRS:
	case HDMI_AUD_CHAN_TYPE_6_0_CS:
	case HDMI_AUD_CHAN_TYPE_6_0_CH:
	case HDMI_AUD_CHAN_TYPE_6_0_OH:
	case HDMI_AUD_CHAN_TYPE_6_0_CHR:
		return 6;
	case HDMI_AUD_CHAN_TYPE_6_1:
	case HDMI_AUD_CHAN_TYPE_6_1_CS:
	case HDMI_AUD_CHAN_TYPE_6_1_CH:
	case HDMI_AUD_CHAN_TYPE_6_1_OH:
	case HDMI_AUD_CHAN_TYPE_6_1_CHR:
	case HDMI_AUD_CHAN_TYPE_7_0:
	case HDMI_AUD_CHAN_TYPE_7_0_LH_RH:
	case HDMI_AUD_CHAN_TYPE_7_0_LSR_RSR:
	case HDMI_AUD_CHAN_TYPE_7_0_LC_RC:
	case HDMI_AUD_CHAN_TYPE_7_0_LW_RW:
	case HDMI_AUD_CHAN_TYPE_7_0_LSD_RSD:
	case HDMI_AUD_CHAN_TYPE_7_0_LSS_RSS:
	case HDMI_AUD_CHAN_TYPE_7_0_LHS_RHS:
	case HDMI_AUD_CHAN_TYPE_7_0_CS_CH:
	case HDMI_AUD_CHAN_TYPE_7_0_CS_OH:
	case HDMI_AUD_CHAN_TYPE_7_0_CS_CHR:
	case HDMI_AUD_CHAN_TYPE_7_0_CH_OH:
	case HDMI_AUD_CHAN_TYPE_7_0_CH_CHR:
	case HDMI_AUD_CHAN_TYPE_7_0_OH_CHR:
	case HDMI_AUD_CHAN_TYPE_7_0_LSS_RSS_LSR_RSR:
	case HDMI_AUD_CHAN_TYPE_8_0_LH_RH_CS:
		return 7;
	case HDMI_AUD_CHAN_TYPE_7_1:
	case HDMI_AUD_CHAN_TYPE_7_1_LH_RH:
	case HDMI_AUD_CHAN_TYPE_7_1_LSR_RSR:
	case HDMI_AUD_CHAN_TYPE_7_1_LC_RC:
	case HDMI_AUD_CHAN_TYPE_7_1_LW_RW:
	case HDMI_AUD_CHAN_TYPE_7_1_LSD_RSD:
	case HDMI_AUD_CHAN_TYPE_7_1_LSS_RSS:
	case HDMI_AUD_CHAN_TYPE_7_1_LHS_RHS:
	case HDMI_AUD_CHAN_TYPE_7_1_CS_CH:
	case HDMI_AUD_CHAN_TYPE_7_1_CS_OH:
	case HDMI_AUD_CHAN_TYPE_7_1_CS_CHR:
	case HDMI_AUD_CHAN_TYPE_7_1_CH_OH:
	case HDMI_AUD_CHAN_TYPE_7_1_CH_CHR:
	case HDMI_AUD_CHAN_TYPE_7_1_OH_CHR:
	case HDMI_AUD_CHAN_TYPE_7_1_LSS_RSS_LSR_RSR:
		return 8;
	default:
		return 2;
	}
}

//can.zeng modification verify-> void hdmi_config_ref_clock(int resolution)
void mtk_hdmi_480p_576p_setting(struct mtk_hdmi *hdmi)
{
	//can.zeng need to verify
/*
 *	switch (resolutionmode) {
 *	case HDMI_VIDEO_720x480p_60Hz:
 *	case HDMI_VIDEO_720x576p_50Hz:
 *	case HDMI_VIDEO_720x480i_60Hz:
 *	case HDMI_VIDEO_720x576i_50Hz:
 *		vWriteHdmiSYSMsk(0x228, 1 << 16, 1 << 16);
 *		break;
 *
 *	default:
 *		vWriteHdmiSYSMsk(0x228, 0 << 16, 1 << 16);
 *		break;
 *	}
 */
	if (((hdmi->mode.hdisplay == 720) && (hdmi->mode.vdisplay == 480)) ||
		((hdmi->mode.hdisplay == 720) && (hdmi->mode.vdisplay == 576)))
		; /* vWriteHdmiSYSMsk(0x228, 1 << 16, 1 << 16); can.zeng*/
	else
		; /* vWriteHdmiSYSMsk(0x228, 0 << 16, 1 << 16); can.zeng*/

}

//can.zeng done -> void vBlackHDMIOnly(),void vUnBlackHDMIOnly()
static void mtk_hdmi_hw_vid_black(struct mtk_hdmi *hdmi,
	bool black)
{
	HDMI_FUNC();
	if (black == true)
		mtk_hdmi_mask(hdmi, TOP_VMUTE_CFG1, REG_VMUTE_EN, REG_VMUTE_EN);
	else
		mtk_hdmi_mask(hdmi, TOP_VMUTE_CFG1, 0, REG_VMUTE_EN);
}




//can.zeng modification done->void MuteHDMIAudio(void)
static void mtk_hdmi_hw_aud_mute(struct mtk_hdmi *hdmi)
{
	HDMI_FUNC();

	if (mtk_hdmi_read(hdmi, AIP_CTRL) & DSD_EN)
		mtk_hdmi_mask(hdmi, AIP_TXCTRL,
			DSD_MUTE_DATA | AUD_MUTE_FIFO_EN,
			DSD_MUTE_DATA | AUD_MUTE_FIFO_EN);
	else
		mtk_hdmi_mask(hdmi, AIP_TXCTRL,	AUD_MUTE_FIFO_EN,
		AUD_MUTE_FIFO_EN);
}

//can.zeng modification done->void UnMuteHDMIAudio(void)
static void mtk_hdmi_hw_aud_unmute(struct mtk_hdmi *hdmi)
{
	HDMI_FUNC();

	mtk_hdmi_mask(hdmi, AIP_TXCTRL, AUD_MUTE_DIS, AUD_MUTE_FIFO_EN);
}

//can.zeng modification done-> void vResetHDMI(char bRst)
static void mtk_hdmi_hw_reset(struct mtk_hdmi *hdmi)
{
	HDMI_FUNC();
	mtk_hdmi_mask(hdmi, HDMITX_CONFIG,
		0x0 << HDMITX_SW_RSTB_SHIFT, HDMITX_SW_RSTB);
	udelay(1);
	mtk_hdmi_mask(hdmi, HDMITX_CONFIG,
		0x1 << HDMITX_SW_RSTB_SHIFT, HDMITX_SW_RSTB);
}

//can.zeng modification done->void vEnableHdmiMode(char bOn)
static void mtk_hdmi_enable_hdmi_mode(struct mtk_hdmi *hdmi, bool enable)
{
	if (enable == true) {
		mtk_hdmi_mask(hdmi, TOP_CFG00, HDMI_MODE_HDMI, HDMI_MODE_HDMI);
		HDMI_LOG("HDMI Mode\n");
	} else {
		mtk_hdmi_mask(hdmi, TOP_CFG00, HDMI_MODE_DVI, HDMI_MODE_HDMI);
		HDMI_LOG("DVI Mode\n");
	}
}

//can.zeng modification done->bool fgTVisHDMI(void)
static bool mtk_hdmi_sink_is_hdmi_device(struct mtk_hdmi *hdmi)
{
	if (hdmi->dvi_mode == true)
		return false;
	else
		return true;
}

//can.zeng modification done->void vEnableDeepColor()
static void mtk_hdmi_set_deep_color(struct mtk_hdmi *hdmi,
	bool is_hdmi_sink)
{
	unsigned int deep_color;

	HDMI_FUNC();

	if (hdmi->color_depth == HDMI_8_BIT) {
		deep_color = DEEPCOLOR_MODE_8BIT;
		HDMI_LOG("not DEEP COLOR\n");
	} else if (hdmi->color_depth == HDMI_10_BIT) {
		deep_color = DEEPCOLOR_MODE_10BIT;
		HDMI_LOG("DEEP COLOR 10-bit\n");
	} else if (hdmi->color_depth == HDMI_12_BIT) {
		deep_color = DEEPCOLOR_MODE_12BIT;
		HDMI_LOG("DEEP COLOR 12-bit\n");
	} else if (hdmi->color_depth == HDMI_16_BIT) {
		deep_color = DEEPCOLOR_MODE_16BIT;
		HDMI_LOG("DEEP COLOR 16-bit\n");
	} else {
		HDMI_LOG("color depth err\n");
		WARN_ON(1);
	}

	mtk_hdmi_mask(hdmi, TOP_CFG00, deep_color, DEEPCOLOR_MODE_MASKBIT);

	/* GCP */
	mtk_hdmi_mask(hdmi, TOP_CFG00, 0, DEEPCOLOR_PAT_EN);
	if ((is_hdmi_sink == true) && (deep_color != DEEPCOLOR_MODE_8BIT))
		mtk_hdmi_mask(hdmi, TOP_MISC_CTLR, DEEP_COLOR_ADD,
			DEEP_COLOR_ADD);
	else
		mtk_hdmi_mask(hdmi, TOP_MISC_CTLR, 0, DEEP_COLOR_ADD);
}

//can.zeng modification done->void vHDMIVideoOutput(char ui1ColorSpace)
static void mtk_hdmi_colorspace_setting(struct mtk_hdmi *hdmi)
{
	if ((hdmi->csp == HDMI_COLORSPACE_YUV444) ||
		(hdmi->csp == HDMI_COLORSPACE_YUV420)) {
		mtk_hdmi_write(hdmi, TOP_VMUTE_CFG1, 0x8000);
		mtk_hdmi_write(hdmi, TOP_VMUTE_CFG2, 0x80001000);
	} else if (hdmi->csp == HDMI_COLORSPACE_YUV422) {
		mtk_hdmi_write(hdmi, TOP_VMUTE_CFG1, 0x8000);
		mtk_hdmi_write(hdmi, TOP_VMUTE_CFG2, 0x00001000);
	} else {
		mtk_hdmi_write(hdmi, TOP_VMUTE_CFG1, 0x1000);
		mtk_hdmi_write(hdmi, TOP_VMUTE_CFG2, 0x10001000);
	}

/*  can.zeng todo verify dolby HDR case:
 *	if (_fgDolbyHdrEnable) {
 *		mtk_hdmi_write(hdmi, TOP_VMUTE_CFG1, 0x8000);
 *		mtk_hdmi_write(hdmi, TOP_VMUTE_CFG2, 0x80001000);
 *	}
 */
}

//can.zeng done->void vHalSendAudioInfoFrame()
static void mtk_hdmi_hw_audio_infoframe(
	struct mtk_hdmi *hdmi, u8 *buffer, u8 len)
{
	enum hdmi_infoframe_type frame_type;
	u8 frame_ver;
	u8 frame_len;
	u8 checksum;

	frame_type = buffer[0];
	frame_ver = buffer[1];
	frame_len = buffer[2];
	checksum = buffer[3];

	HDMI_LOG("frame_type:0x%x,version:0x%x,length:0x%x,checksum:0x%x\n",
		frame_type, frame_ver, frame_len, checksum);

	mtk_hdmi_mask(hdmi, TOP_INFO_EN, AUD_DIS_WR | AUD_DIS,
		AUD_EN_WR | AUD_EN);
	mtk_hdmi_mask(hdmi, TOP_INFO_RPT, AUD_RPT_DIS, AUD_RPT_EN);

	mtk_hdmi_write(hdmi, TOP_AIF_HEADER, (frame_len << 16) +
		(frame_ver << 8) + (frame_type << 0));
	mtk_hdmi_write(hdmi, TOP_AIF_PKT00,
		(buffer[6] << 24) + (buffer[5] << 16) +
		(buffer[4] << 8) + (buffer[3] << 0));
	mtk_hdmi_write(hdmi, TOP_AIF_PKT01,
		(buffer[8] << 8) + (buffer[7] << 0));
	mtk_hdmi_write(hdmi, TOP_AIF_PKT02, 0);
	mtk_hdmi_write(hdmi, TOP_AIF_PKT03, 0);

	mtk_hdmi_mask(hdmi, TOP_INFO_RPT, AUD_RPT_EN, AUD_RPT_EN);
	mtk_hdmi_mask(hdmi, TOP_INFO_EN, AUD_EN_WR | AUD_EN,
		AUD_EN_WR | AUD_EN);

}

//can.zeng done->void vHalSendAVIInfoFrame(unsigned char *pr_bData)
static void mtk_hdmi_hw_avi_infoframe(
	struct mtk_hdmi *hdmi, u8 *buffer, u8 len)
{

	enum hdmi_infoframe_type frame_type = buffer[0];
	u8 frame_ver = buffer[1];
	u8 frame_len = buffer[2];
	u8 checksum = buffer[3];

	HDMI_LOG("frame_type:0x%x,version:0x%x,length:0x%x,checksum:0x%x\n",
		frame_type, frame_ver, frame_len, checksum);

	mtk_hdmi_mask(hdmi, TOP_INFO_EN, AVI_DIS_WR | AVI_DIS,
		AVI_EN_WR | AVI_EN);
	mtk_hdmi_mask(hdmi, TOP_INFO_RPT, AVI_RPT_DIS, AVI_RPT_EN);

	mtk_hdmi_write(hdmi, TOP_AVI_HEADER, (buffer[2] << 16) +
		(buffer[1] << 8) + (buffer[0] << 0));

	mtk_hdmi_write(hdmi, TOP_AVI_PKT00,
		(buffer[6] << 24) + (buffer[5] << 16) +
		(buffer[4] << 8) + (buffer[3] << 0));

	mtk_hdmi_write(hdmi, TOP_AVI_PKT01,
		(buffer[9] << 16) + (buffer[8] << 8) +
		(buffer[7] << 0));

	mtk_hdmi_write(hdmi, TOP_AVI_PKT02,
		(buffer[13] << 24) + (buffer[12] << 16) +
		(buffer[11] << 8) + (buffer[10] << 0));

	mtk_hdmi_write(hdmi, TOP_AVI_PKT03,
		(buffer[16] << 16) + (buffer[15] << 8) +
		(buffer[14] << 0));

	mtk_hdmi_write(hdmi, TOP_AVI_PKT04, 0);
	mtk_hdmi_write(hdmi, TOP_AVI_PKT05, 0);

	mtk_hdmi_mask(hdmi, TOP_INFO_RPT, AVI_RPT_EN, AVI_RPT_EN);
	mtk_hdmi_mask(hdmi, TOP_INFO_EN, AVI_EN_WR | AVI_EN,
		AVI_EN_WR | AVI_EN);

}

static void mtk_hdmi_hw_spd_infoframe(struct mtk_hdmi *hdmi, u8 *buffer,
					u8 len)
{
	enum hdmi_infoframe_type frame_type = buffer[0];
	u8 frame_ver = buffer[1];
	u8 frame_len = buffer[2];
	u8 checksum = buffer[3];

	HDMI_LOG("frame_type:0x%x,version:0x%x,length:0x%x,checksum:0x%x\n",
		frame_type, frame_ver, frame_len, checksum);

	mtk_hdmi_mask(hdmi, TOP_INFO_EN, SPD_DIS_WR | SPD_DIS,
		SPD_EN_WR | SPD_EN);
	mtk_hdmi_mask(hdmi, TOP_INFO_RPT, SPD_RPT_DIS, SPD_RPT_EN);

	mtk_hdmi_write(hdmi, TOP_SPDIF_HEADER, (buffer[2] << 16) +
		(buffer[1] << 8) + (buffer[0] << 0));

	mtk_hdmi_write(hdmi, TOP_SPDIF_PKT00,
		(buffer[6] << 24) + (buffer[5] << 16) +
		(buffer[4] << 8) + (buffer[3] << 0));

	mtk_hdmi_write(hdmi, TOP_SPDIF_PKT01,
		(buffer[9] << 16) + (buffer[8] << 8) +
		(buffer[7] << 0));

	mtk_hdmi_write(hdmi, TOP_SPDIF_PKT02,
		(buffer[13] << 24) + (buffer[12] << 16) +
		(buffer[11] << 8) + (buffer[10] << 0));

	mtk_hdmi_write(hdmi, TOP_SPDIF_PKT03,
		(buffer[16] << 16) + (buffer[15] << 8) +
		(buffer[14] << 0));

	mtk_hdmi_write(hdmi, TOP_SPDIF_PKT04,
		(buffer[20] << 24) + (buffer[19] << 16) +
		(buffer[18] << 8) + (buffer[17] << 0));

	mtk_hdmi_write(hdmi, TOP_SPDIF_PKT05,
		(buffer[23] << 16) + (buffer[22] << 8) +
		(buffer[21] << 0));

	mtk_hdmi_write(hdmi, TOP_SPDIF_PKT06,
		(buffer[27] << 24) + (buffer[26] << 16) +
		(buffer[25] << 8) + (buffer[24] << 0));

	mtk_hdmi_write(hdmi, TOP_SPDIF_PKT07,
		(buffer[30] << 16) + (buffer[29] << 8) +
		(buffer[28] << 0));

	mtk_hdmi_mask(hdmi, TOP_INFO_RPT, SPD_RPT_EN, SPD_RPT_EN);
	mtk_hdmi_mask(hdmi, TOP_INFO_EN, SPD_EN_WR | SPD_EN,
		SPD_EN_WR | SPD_EN);

}

/*for 3D and standard Dolby Vision info;
 *low-latency dolby vision use Dolby VSIF
 */
 //can.zeng todo verify the buffer length
static void mtk_hdmi_hw_h14b_vsif(
	struct mtk_hdmi *hdmi, u8 *buffer, u8 len)
{

	enum hdmi_infoframe_type frame_type = buffer[0];
	u8 frame_ver = buffer[1];
	u8 frame_len = buffer[2];
	u8 checksum = buffer[3];

	HDMI_LOG("frame_type:0x%x,version:0x%x,length:0x%x,checksum:0x%x\n",
		frame_type, frame_ver, frame_len, checksum);

	mtk_hdmi_mask(hdmi, TOP_INFO_EN, VSIF_DIS | VSIF_DIS_WR,
		VSIF_EN | VSIF_EN_WR);
	mtk_hdmi_mask(hdmi, TOP_INFO_RPT, VSIF_RPT_DIS, VSIF_RPT_EN);

	mtk_hdmi_write(hdmi, TOP_VSIF_HEADER,
		(buffer[2] << 16) + (buffer[1] << 8) +
		(buffer[0] << 0));

	mtk_hdmi_write(hdmi, TOP_VSIF_PKT00,
		(buffer[6] << 24) + (buffer[5] << 16) +
		(buffer[4] << 8) + (buffer[3] << 0));
	mtk_hdmi_write(hdmi, TOP_VSIF_PKT01,
		(buffer[8] << 8) + (buffer[7] << 0));

	mtk_hdmi_write(hdmi, TOP_VSIF_PKT02, 0);
	mtk_hdmi_write(hdmi, TOP_VSIF_PKT03, 0);
	mtk_hdmi_write(hdmi, TOP_VSIF_PKT04, 0);
	mtk_hdmi_write(hdmi, TOP_VSIF_PKT05, 0);
	mtk_hdmi_write(hdmi, TOP_VSIF_PKT06, 0);
	mtk_hdmi_write(hdmi, TOP_VSIF_PKT07, 0);

	mtk_hdmi_mask(hdmi, TOP_INFO_RPT, VSIF_RPT_EN, VSIF_RPT_EN);
	mtk_hdmi_mask(hdmi, TOP_INFO_EN, VSIF_EN | VSIF_EN_WR,
		VSIF_EN | VSIF_EN_WR);

}

//can.zeng todo verify, which TOP_GEN_PCK to be used?
static void mtk_hdmi_hw_hf_vsif(
	struct mtk_hdmi *hdmi/*, u8 *buffer, u8 len*/)
{

}


//can.zeng done->void vHalSendStaticHdrInfoFrame()
//Dynamic Range and Mastering infoframe
//can.zeng todo verify the 'buffer' is NULL and 'len'
static void mtk_hdmi_hw_static_hdr_infoframe(
	struct mtk_hdmi *hdmi, u8 *buffer, u8 len)
{
	enum hdmi_infoframe_type frame_type = buffer[0];
	u8 frame_ver = buffer[1];
	u8 frame_len = buffer[2];
	u8 checksum = buffer[3];

	HDMI_LOG("frame_type:0x%x,version:0x%x,length:0x%x,checksum:0x%x\n",
		frame_type, frame_ver, frame_len, checksum);

	mtk_hdmi_mask(hdmi, TOP_INFO_EN, 0, GEN4_EN | GEN4_EN_WR);
	mtk_hdmi_mask(hdmi, TOP_INFO_RPT, 0, GEN4_RPT_EN);

	mtk_hdmi_write(hdmi, TOP_GEN4_HEADER, (buffer[2] << 16) +
		(buffer[1] << 8) + (buffer[0] << 0));

	mtk_hdmi_write(hdmi, TOP_GEN4_PKT00,
		(buffer[6] << 24) + (buffer[5] << 16) +
		(buffer[4] << 8) + (buffer[3] << 0));

	mtk_hdmi_write(hdmi, TOP_GEN4_PKT01,
		(buffer[9] << 16) + (buffer[8] << 8) +
		(buffer[7] << 0));

	mtk_hdmi_write(hdmi, TOP_GEN4_PKT02,
		(buffer[13] << 24) + (buffer[12] << 16) +
		(buffer[11] << 8) + (buffer[10] << 0));

	mtk_hdmi_write(hdmi, TOP_GEN4_PKT03,
		(buffer[16] << 16) + (buffer[15] << 8) +
		(buffer[14] << 0));

	mtk_hdmi_write(hdmi, TOP_GEN4_PKT04,
		(buffer[20] << 24) + (buffer[19] << 16) +
		(buffer[18] << 8) + (buffer[17] << 0));

	mtk_hdmi_write(hdmi, TOP_GEN4_PKT05,
		(buffer[23] << 16) + (buffer[22] << 8) +
		(buffer[21] << 0));

	mtk_hdmi_write(hdmi, TOP_GEN4_PKT06,
		(buffer[27] << 24) + (buffer[26] << 16) +
		(buffer[25] << 8) + (buffer[24] << 0));

	mtk_hdmi_write(hdmi, TOP_GEN4_PKT07,
		(buffer[30] << 16) + (buffer[29] << 8) +
		(buffer[28] << 0));

	mtk_hdmi_mask(hdmi, TOP_INFO_RPT, GEN4_RPT_EN, GEN4_RPT_EN);
	mtk_hdmi_mask(hdmi, TOP_INFO_EN, GEN4_EN | GEN4_EN_WR,
		GEN4_EN | GEN4_EN_WR);

}


//can.zeng done->void vHalSendHdr10PlusVSIF()
//can.zeng todo verify
static void mtk_hdmi_hw_samsung_hdr10p_vsif(
	struct mtk_hdmi *hdmi/*, u8 *buffer, u8 len*/)
{


}


//can.zeng done->void vHalSendDynamicHdrEMPs()
static void mtk_hdmi_hw_dynamic_hdr_emp(
	struct mtk_hdmi *hdmi/*, u8 *buffer, u8 len*/)
{

}

static void mtk_hdmi_hw_dolby_vsif(
	struct mtk_hdmi *hdmi, u8 *buffer, u8 len)
{

	enum hdmi_infoframe_type frame_type = buffer[0];
	u8 frame_ver = buffer[1];
	u8 frame_len = buffer[2];
	u8 checksum = buffer[3];

	HDMI_LOG("frame_type:0x%x,version:0x%x,length:0x%x,checksum:0x%x\n",
		frame_type, frame_ver, frame_len, checksum);

	mtk_hdmi_mask(hdmi, TOP_INFO_EN, 0, GEN5_EN | GEN5_EN_WR);
	mtk_hdmi_mask(hdmi, TOP_INFO_RPT, 0, GEN5_RPT_EN);

	mtk_hdmi_write(hdmi, TOP_GEN5_HEADER,
		(buffer[2] << 16) + (buffer[1] << 8) +
		(buffer[0] << 0));

	mtk_hdmi_write(hdmi, TOP_GEN5_PKT00,
		(buffer[6] << 24) + (buffer[5] << 16) +
		(buffer[4] << 8) + (buffer[3] << 0));
	mtk_hdmi_write(hdmi, TOP_GEN5_PKT01, (buffer[8] << 16) +
		(buffer[8] << 8) + (buffer[7] << 0));

	mtk_hdmi_write(hdmi, TOP_GEN5_PKT02, 0);
	mtk_hdmi_write(hdmi, TOP_GEN5_PKT03, 0);
	mtk_hdmi_write(hdmi, TOP_GEN5_PKT04, 0);
	mtk_hdmi_write(hdmi, TOP_GEN5_PKT05, 0);
	mtk_hdmi_write(hdmi, TOP_GEN5_PKT06, 0);
	mtk_hdmi_write(hdmi, TOP_GEN5_PKT07, 0);

	mtk_hdmi_mask(hdmi, TOP_INFO_RPT, GEN5_RPT_EN, GEN5_RPT_EN);
	mtk_hdmi_mask(hdmi, TOP_INFO_EN, GEN5_EN |
		GEN5_EN_WR, GEN5_EN | GEN5_EN_WR);

}

//can.zeng modification done
static int mtk_hdmi_setup_audio_infoframe(struct mtk_hdmi *hdmi)
{
	struct hdmi_audio_infoframe frame;
	u8 buffer[14];
	ssize_t err;

	err = hdmi_audio_infoframe_init(&frame);
	if (err < 0) {
		HDMI_LOG("Failed to setup audio infoframe: %zd\n", err);
		return err;
	}

	frame.coding_type = HDMI_AUDIO_CODING_TYPE_STREAM;
	frame.sample_frequency = HDMI_AUDIO_SAMPLE_FREQUENCY_STREAM;
	frame.sample_size = HDMI_AUDIO_SAMPLE_SIZE_STREAM;
	frame.channels = mtk_hdmi_aud_get_chnl_count(
		hdmi->aud_param.aud_input_chan_type);

	err = hdmi_audio_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		HDMI_LOG("Failed to pack audio infoframe: %zd\n", err);
		return err;
	}

	mtk_hdmi_hw_audio_infoframe(hdmi, buffer, sizeof(buffer));
	return 0;
}

static int mtk_hdmi_setup_h14b_vsif(struct mtk_hdmi *hdmi,
						struct drm_display_mode *mode)
{
	struct hdmi_vendor_infoframe frame;
	u8 buffer[10];
	ssize_t err;

	err = drm_hdmi_vendor_infoframe_from_display_mode(
		&frame, &hdmi->conn, mode);
	if (err) {
		HDMI_LOG("Failed to get VSIF from mode: %zd\n", err);
		return err;
	}

	err = hdmi_vendor_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		HDMI_LOG("Failed to pack VSIF: %zd\n", err);
		return err;
	}

	mtk_hdmi_hw_h14b_vsif(hdmi, buffer, sizeof(buffer));
	return 0;
}

//can.zeng todo function'implementation
static int mtk_hdmi_setup_hf_vsif(struct mtk_hdmi *hdmi,
	struct drm_display_mode *mode)
{
	mtk_hdmi_hw_hf_vsif(hdmi);

	return 0;
}

//can.zeng todo function'implementation
static int mtk_hdmi_setup_hdr10_infoframe(struct mtk_hdmi *hdmi)
{
	unsigned char hdr10buff[32] = {0};
	unsigned char length = 32;

	mtk_hdmi_hw_static_hdr_infoframe(hdmi, hdr10buff, length);

	return 0;
}

//can.zeng todo function'implementation
static int mtk_hdmi_setup_hdr10plus_vsif(struct mtk_hdmi *hdmi)
{
	mtk_hdmi_hw_samsung_hdr10p_vsif(hdmi);

	return 0;
}

//can.zeng todo function'implementation
static int mtk_hdmi_setup_dolby_lowlatency_vsif(struct mtk_hdmi *hdmi)
{
	unsigned char hdr10buff[32] = {0};
	unsigned char length = 32;

	mtk_hdmi_hw_dolby_vsif(hdmi, hdr10buff, length);

	return 0;
}

//can.zeng todo function'implementation
static int mtk_hdmi_setup_hdr10p_emp(struct mtk_hdmi *hdmi)
{
	mtk_hdmi_hw_dynamic_hdr_emp(hdmi);

	return 0;
}

//can.zeng modification done->void vAudioPacketOff(unsigned char bOn)
static void mtk_hdmi_hw_send_aud_packet(struct mtk_hdmi *hdmi, bool enable)
{
	if (enable == false) {
		mtk_hdmi_mask(hdmi, AIP_TXCTRL, AUD_PACKET_DROP,
			AUD_PACKET_DROP);
		HDMI_LOG("audio packet Off\n");
	} else {
		mtk_hdmi_mask(hdmi, AIP_TXCTRL, 0, AUD_PACKET_DROP);
		HDMI_LOG("audio packet On\n");
	}
}

//can.zeng modification done-> void vSend_AVMUTE(void)
static inline void mtk_hdmi_hw_send_av_mute(struct mtk_hdmi *hdmi)
{
	/*GCP packet */
	HDMI_FUNC();

	mtk_hdmi_mask(hdmi, TOP_CFG01, 0, CP_CLR_MUTE_EN);
	mtk_hdmi_mask(hdmi, TOP_CFG01, 0, CP_SET_MUTE_EN);
	mtk_hdmi_mask(hdmi, TOP_INFO_RPT, 0, CP_RPT_EN);
	mtk_hdmi_mask(hdmi, TOP_INFO_EN, 0, CP_EN | CP_EN_WR);

	mtk_hdmi_mask(hdmi, TOP_CFG01, 0, CP_CLR_MUTE_EN);
	mtk_hdmi_mask(hdmi, TOP_CFG01, CP_SET_MUTE_EN, CP_SET_MUTE_EN);
	mtk_hdmi_mask(hdmi, TOP_INFO_RPT, CP_RPT_EN, CP_RPT_EN);
	mtk_hdmi_mask(hdmi, TOP_INFO_EN, CP_EN | CP_EN_WR, CP_EN | CP_EN_WR);

}

//can.zeng modification done-> void vSend_AVUNMUTE(void)
static inline void mtk_hdmi_hw_send_av_unmute(struct mtk_hdmi *hdmi)
{
	/*GCP packet */
	HDMI_FUNC();

	mtk_hdmi_mask(hdmi, TOP_CFG01, 0, CP_CLR_MUTE_EN);
	mtk_hdmi_mask(hdmi, TOP_CFG01, 0, CP_SET_MUTE_EN);
	mtk_hdmi_mask(hdmi, TOP_INFO_RPT, 0, CP_RPT_EN);
	mtk_hdmi_mask(hdmi, TOP_INFO_EN, 0, CP_EN | CP_EN_WR);

	mtk_hdmi_mask(hdmi, TOP_CFG01, CP_CLR_MUTE_EN, CP_CLR_MUTE_EN);
	mtk_hdmi_mask(hdmi, TOP_CFG01, 0, CP_SET_MUTE_DIS);
	mtk_hdmi_mask(hdmi, TOP_INFO_RPT, CP_RPT_EN, CP_RPT_EN);
	mtk_hdmi_mask(hdmi, TOP_INFO_EN, CP_EN | CP_EN_WR, CP_EN | CP_EN_WR);

}

static void mtk_hdmi_hw_ncts_enable(struct mtk_hdmi *hdmi, bool enable)
{
	unsigned int data;

	data = mtk_hdmi_read(hdmi, AIP_CTRL);

	if (enable == false)
		data |= CTS_SW_SEL;
	else
		data &= ~CTS_SW_SEL;

	mtk_hdmi_write(hdmi, AIP_CTRL, data);
}

static void mtk_hdmi_hw_aud_set_channel_status(
	struct mtk_hdmi *hdmi, u8 *channel_status)
{
	HDMI_LOG("Channel Status Bit: %x, %x, %x, %x, %x, %x, %x\n",
		channel_status[0], channel_status[1], channel_status[2],
		channel_status[3], channel_status[4], channel_status[5],
		channel_status[6]);

	/* actually, only the first 5 or 7 bytes of Channel Status
	 * contain useful information
	 */
	mtk_hdmi_write(hdmi, AIP_I2S_CHST0,
		(channel_status[3] << 24) + (channel_status[2] << 16) +
		(channel_status[1] << 8) + (channel_status[0] << 0));
	mtk_hdmi_write(hdmi, AIP_I2S_CHST1,
		(channel_status[6] << 16) + (channel_status[5] << 8) +
		(channel_status[4] << 0));
}

struct hdmi_acr_n {
	unsigned int clock;
	unsigned int n[3];
};

/* Recommended N values from HDMI specification, tables 7-1 to 7-3 */
static const struct hdmi_acr_n hdmi_rec_n_table[] = {
	/* Clock, N: 32kHz 44.1kHz 48kHz */
	{  25175, {  4576,  7007,  6864 } },
	{  74176, { 11648, 17836, 11648 } },
	{ 148352, { 11648,  8918,  5824 } },
	{ 296703, {  5824,  4459,  5824 } },
	{ 297000, {  3072,  4704,  5120 } },
	{      0, {  4096,  6272,  6144 } }, /* all other TMDS clocks */
};

/**
 * hdmi_recommended_n() - Return N value recommended by HDMI specification
 * @freq: audio sample rate in Hz
 * @clock: rounded TMDS clock in kHz
 */
static unsigned int hdmi_recommended_n(unsigned int freq, unsigned int clock)
{
	const struct hdmi_acr_n *recommended;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(hdmi_rec_n_table) - 1; i++) {
		if (clock == hdmi_rec_n_table[i].clock)
			break;
	}
	recommended = hdmi_rec_n_table + i;

	switch (freq) {
	case 32000:
		return recommended->n[0];
	case 44100:
		return recommended->n[1];
	case 48000:
		return recommended->n[2];
	case 88200:
		return recommended->n[1] * 2;
	case 96000:
		return recommended->n[2] * 2;
	case 176400:
		return recommended->n[1] * 4;
	case 192000:
		return recommended->n[2] * 4;
	default:
		return (128 * freq) / 1000;
	}
}

static unsigned int hdmi_mode_clock_to_hz(unsigned int clock)
{
	switch (clock) {
	case 25175:
		return 25174825;	/* 25.2/1.001 MHz */
	case 74176:
		return 74175824;	/* 74.25/1.001 MHz */
	case 148352:
		return 148351648;	/* 148.5/1.001 MHz */
	case 296703:
		return 296703297;	/* 297/1.001 MHz */
	default:
		return clock * 1000;
	}
}

static unsigned int hdmi_expected_cts(unsigned int audio_sample_rate,
				      unsigned int tmds_clock, unsigned int n)
{
	return DIV_ROUND_CLOSEST_ULL((u64)hdmi_mode_clock_to_hz(tmds_clock) * n,
				     128 * audio_sample_rate);
}

static void mtk_hdmi_hw_aud_set_ncts(struct mtk_hdmi *hdmi,
				     unsigned int sample_rate,
				     unsigned int clock)
{
	unsigned int n, ncts;

	n = hdmi_recommended_n(sample_rate, clock);
	ncts = hdmi_expected_cts(sample_rate, clock, n);

	HDMI_LOG("sample_rate=%u, clock=%d, ncts=%u, n=%u\n",
		sample_rate, clock, n, ncts);

	mtk_hdmi_write(hdmi, AIP_N_VAL, n);
	mtk_hdmi_write(hdmi, AIP_CTS_SVAL, ncts);

}

static int mtk_hdmi_video_change_vpll(struct mtk_hdmi *hdmi, u32 clock)
{
	unsigned long rate;
	int ret;

	/* The DPI driver already should have set TVDPLL to the correct rate */
	ret = clk_set_rate(hdmi->clk[MTK_HDMI_CLK_HDMI_TXPLL], clock);
	if (ret) {
		HDMI_LOG("Failed to set PLL to %u Hz: %d\n", clock, ret);
		return ret;
	}

	rate = clk_get_rate(hdmi->clk[MTK_HDMI_CLK_HDMI_TXPLL]);

	if (DIV_ROUND_CLOSEST(rate, 1000) != DIV_ROUND_CLOSEST(clock, 1000))
		HDMI_LOG("Want PLL %u Hz, got %lu Hz\n", clock, rate);
	else
		HDMI_LOG("Want PLL %u Hz, got %lu Hz\n", clock, rate);

	return 0;
}

static int mtk_hdmi_aud_enable_packet(struct mtk_hdmi *hdmi, bool enable)
{
	mtk_hdmi_hw_send_aud_packet(hdmi, enable);
	return 0;
}

static int mtk_hdmi_aud_on_off_hw_ncts(struct mtk_hdmi *hdmi, bool on)
{
	mtk_hdmi_hw_ncts_enable(hdmi, on);
	return 0;
}

//can.zeng done->void vSetHdmiDsdConfig()
static void mtk_hdmi_audio_dsd_config(struct mtk_hdmi *hdmi,
	unsigned char chNum, bool dsd_bypass)
{
	HDMI_FUNC();

	mtk_hdmi_mask(hdmi, AIP_CTRL, DSD_EN, SPDIF_EN | DSD_EN | HBRA_ON);
	mtk_hdmi_mask(hdmi, AIP_TXCTRL, DSD_MUTE_DATA, DSD_MUTE_DATA);
	if (dsd_bypass == true)
		mtk_hdmi_write(hdmi, TOP_AUD_MAP, 0x75316420);
	/* 0x13570246 */
	else
		mtk_hdmi_write(hdmi, TOP_AUD_MAP, 0x04230150);
	/* 0 FL;1 SL;2 CENT;3 FR;4 SR;5 LFE 0x32400510 */

	/* rxtx bypass */
	mtk_hdmi_mask(hdmi, AIP_SPDIF_CTRL, 0, I2S2DSD_EN);
}

//can.zeng done->void vSetHdmi2I2SfifoMap(unsigned int bFifoMap)
static inline void mtk_hdmi_hw_i2s_fifo_map(struct mtk_hdmi *hdmi,
	unsigned int fifo_mapping)
{
	mtk_hdmi_mask(hdmi, AIP_I2S_CTRL, fifo_mapping,
		FIFO3_MAP |	FIFO2_MAP | FIFO1_MAP | FIFO0_MAP);
}

//can.zeng done-> void vSetHdmi2I2SCH(unsigned int bCH)
static inline void mtk_hdmi_hw_i2s_ch_number(struct mtk_hdmi *hdmi,
	unsigned int chNum)
{
	mtk_hdmi_mask(hdmi, AIP_CTRL, chNum << I2S_EN_SHIFT, I2S_EN);
}


//can.zeng done->void vSetHdmiI2SChNum()
static void mtk_hdmi_hw_i2s_ch_mapping(struct mtk_hdmi *hdmi,
	unsigned char chNum, unsigned char mapping)
{
	unsigned int bData, bData1, bData2, bData3;

	if (chNum == 2) {	/* I2S 2ch */
		bData = 0x1;	/* 2ch data */
		bData1 = 0x50;	/* data0 */

	} else if ((chNum == 3) || (chNum == 4)) {	/* I2S 2ch */
		if ((chNum == 4) && (mapping == 0x08))
			bData = 0x3;	/* 4ch data */
		else
			bData = 0x03;	/* 4ch data */

		bData1 = 0x50;	/* data0 */

	} else if ((chNum == 6) || (chNum == 5)) {	/* I2S 5.1ch */
		if ((chNum == 6) && (mapping == 0x0E)) {
			bData = 0xf;	/* 6.0 ch data */
			bData1 = 0x50;	/* data0 */
		} else {
			bData = 0x7;	/* 5.1ch data, 5/0ch */
			bData1 = 0x50;	/* data0 */
		}

	} else if (chNum == 8) {	/* I2S 5.1ch */
		bData = 0xf;	/* 7.1ch data */
		bData1 = 0x50;	/* data0 */
	} else if (chNum == 7) {	/* I2S 6.1ch */
		bData = 0xf;	/* 6.1ch data */
		bData1 = 0x50;	/* data0 */
	} else {
		bData = 0x01;	/* 2ch data */
		bData1 = 0x50;	/* data0 */
	}

	bData2 = 0xc6;
	bData3 = 0xfa;

	mtk_hdmi_hw_i2s_fifo_map(hdmi, (MAP_SD3 << 6) | (MAP_SD2 << 4) |
		(MAP_SD1 << 2) | (MAP_SD0 << 0));
	mtk_hdmi_hw_i2s_ch_number(hdmi, bData);

	if (chNum == 2)
		mtk_hdmi_mask(hdmi, AIP_TXCTRL, LAYOUT0, LAYOUT1);
	else
		mtk_hdmi_mask(hdmi, AIP_TXCTRL, LAYOUT1, LAYOUT1);

}

//can.zeng done->void vSetHdmiI2SDataFmt(unsigned char bFmt)
static void mtk_hdmi_i2s_data_fmt(struct mtk_hdmi *hdmi,
	unsigned char fmt)
{
	unsigned int u4Data;

	HDMI_LOG("fmt = %d-------\n", fmt);

	u4Data = mtk_hdmi_read(hdmi, AIP_I2S_CTRL);
	HDMI_LOG("AIP_I2S_CTRL1 value = 0x%x-------\n", u4Data);
	u4Data &= ~(WS_HIGH | I2S_1ST_BIT_NOSHIFT | JUSTIFY_RIGHT);

	switch (fmt) {
	case HDMI_I2S_MODE_RJT_24BIT:
		u4Data |= (WS_HIGH | I2S_1ST_BIT_NOSHIFT | JUSTIFY_RIGHT);
		break;

	case HDMI_I2S_MODE_RJT_16BIT:
		u4Data |= (WS_HIGH | I2S_1ST_BIT_NOSHIFT | JUSTIFY_RIGHT);
		break;

	case HDMI_I2S_MODE_LJT_24BIT:
		u4Data |= (WS_HIGH | I2S_1ST_BIT_NOSHIFT);
		break;

	case HDMI_I2S_MODE_LJT_16BIT:
		u4Data |= (WS_HIGH | I2S_1ST_BIT_NOSHIFT);
		break;

	case HDMI_I2S_MODE_I2S_24BIT:
		break;

	case HDMI_I2S_MODE_I2S_16BIT:
		break;

	default:
		break;
	}
	mtk_hdmi_write(hdmi, AIP_I2S_CTRL, u4Data);

	u4Data = mtk_hdmi_read(hdmi, AIP_I2S_CTRL);
	HDMI_LOG("AIP_I2S_CTRL2 value = 0x%x-------\n", u4Data);
}

//can.zeng done->void vSetHdmiI2SSckEdge(unsigned int bEdge)
static inline void mtk_hdmi_i2s_sck_edge(
	struct mtk_hdmi *hdmi, unsigned int edge)
{
	mtk_hdmi_mask(hdmi, AIP_I2S_CTRL, edge, SCK_EDGE_RISE);
}

//can.zeng done->void vSetHdmiI2SCbitOrder(unsigned int bCbit)
static inline void mtk_hdmi_i2s_cbit_order(
	struct mtk_hdmi *hdmi, unsigned int cbit)
{
	mtk_hdmi_mask(hdmi, AIP_I2S_CTRL, cbit, CBIT_ORDER_SAME);
}

//can.zeng done->void vSetHdmiI2SVbit(unsigned int bVbit)
static inline void mtk_hdmi_i2s_vbit(
	struct mtk_hdmi *hdmi, unsigned int vbit)
{
	mtk_hdmi_mask(hdmi, AIP_I2S_CTRL, vbit, VBIT_COM);
}

//can.zeng done->void vSetHdmiI2SDataDir(unsigned int bDataDir)
static inline void mtk_hdmi_i2s_data_direction(
	struct mtk_hdmi *hdmi, unsigned int data_dir)
{
	mtk_hdmi_mask(hdmi, AIP_I2S_CTRL, data_dir, DATA_DIR_LSB);
}

//can.zeng done->void vEnableInputAudioType(unsigned int bspdifi2s)
static inline void mtk_hdmi_hw_audio_type(
	struct mtk_hdmi *hdmi, unsigned int spdif_i2s)
{
	mtk_hdmi_mask(hdmi, AIP_CTRL, spdif_i2s << SPDIF_EN_SHIFT, SPDIF_EN);
}

//can.zeng done->unsigned char bGetChannelMapping(void)
//can.zeng done->void vUpdateAudSrcChType(unsigned char u1SrcChType)
static unsigned char mtk_hdmi_get_i2s_ch_mapping(
	struct mtk_hdmi *hdmi, unsigned char channel_type)
{
	unsigned char FR, FL, FC, LFE, RR, RL, RRC, RLC, RC;
	unsigned char ch_number = 0;
	unsigned char ChannelMap = 0x00;

	switch (channel_type) {
	case HDMI_AUD_CHAN_TYPE_1_0:
	case HDMI_AUD_CHAN_TYPE_2_0:
		FR = 1;
		FL = 1;
		LFE = 0;
		ch_number = 2;
		break;

	case HDMI_AUD_CHAN_TYPE_1_1:
	case HDMI_AUD_CHAN_TYPE_2_1:
		FR = 1;
		FL = 1;
		LFE = 1;
		ch_number = 3;
		break;

	case HDMI_AUD_CHAN_TYPE_3_0:
		FR = 1;
		FL = 1;
		FC = 1;
		LFE = 0;
		ch_number = 3;
		break;

	case HDMI_AUD_CHAN_TYPE_3_0_LRS:
		FR = 1;
		FL = 1;
		RR = 1;
		RL = 1;
		LFE = 0;
		ch_number = 4;
		break;

	case HDMI_AUD_CHAN_TYPE_3_1_LRS:
		FR = 1;
		FL = 1;
		FC = 0;
		LFE = 1;
		RR = 1;
		RL = 1;
		ch_number = 5;
		break;

	case HDMI_AUD_CHAN_TYPE_4_0_CLRS:
		FR = 1;
		FL = 1;
		FC = 1;
		LFE = 0;
		RR = 1;
		RL = 1;
		ch_number = 5;
		break;

	case HDMI_AUD_CHAN_TYPE_4_1_CLRS:
		FR = 1;
		FL = 1;
		FC = 1;
		LFE = 1;
		RR = 1;
		RL = 1;
		ch_number = 6;
		break;

	case HDMI_AUD_CHAN_TYPE_3_1:
		FR = 1;
		FL = 1;
		FC = 1;
		LFE = 1;
		ch_number = 4;
		break;

	case HDMI_AUD_CHAN_TYPE_4_0:
		FR = 1;
		FL = 1;
		RR = 1;
		RL = 1;
		LFE = 0;
		ch_number = 4;
		break;

	case HDMI_AUD_CHAN_TYPE_4_1:
		FR = 1;
		FL = 1;
		RR = 1;
		RL = 1;
		LFE = 1;
		ch_number = 5;
		break;

	case HDMI_AUD_CHAN_TYPE_5_0:
		FR = 1;
		FL = 1;
		FC = 1;
		LFE = 0;
		RR = 1;
		RL = 1;
		ch_number = 5;
		break;

	case HDMI_AUD_CHAN_TYPE_5_1:
		FR = 1;
		FL = 1;
		FC = 1;
		LFE = 1;
		RR = 1;
		RL = 1;
		ch_number = 6;
		break;

	case HDMI_AUD_CHAN_TYPE_6_0:
	case HDMI_AUD_CHAN_TYPE_6_0_CS:
	case HDMI_AUD_CHAN_TYPE_6_0_CH:
	case HDMI_AUD_CHAN_TYPE_6_0_OH:
	case HDMI_AUD_CHAN_TYPE_6_0_CHR:
		FR = 1;
		FL = 1;
		FC = 1;
		LFE = 0;
		RR = 1;
		RL = 1;
		RC = 1;
		ch_number = 6;
		break;

	case HDMI_AUD_CHAN_TYPE_6_1:
	case HDMI_AUD_CHAN_TYPE_6_1_CS:
	case HDMI_AUD_CHAN_TYPE_6_1_CH:
	case HDMI_AUD_CHAN_TYPE_6_1_OH:
	case HDMI_AUD_CHAN_TYPE_6_1_CHR:
		FR = 1;
		FL = 1;
		FC = 1;
		LFE = 1;
		RR = 1;
		RL = 1;
		RC = 1;
		ch_number = 7;
		break;

	case HDMI_AUD_CHAN_TYPE_7_0:
	case HDMI_AUD_CHAN_TYPE_7_0_LH_RH:
	case HDMI_AUD_CHAN_TYPE_7_0_LSR_RSR:
	case HDMI_AUD_CHAN_TYPE_7_0_LC_RC:
	case HDMI_AUD_CHAN_TYPE_7_0_LW_RW:
	case HDMI_AUD_CHAN_TYPE_7_0_LSD_RSD:
	case HDMI_AUD_CHAN_TYPE_7_0_LSS_RSS:
	case HDMI_AUD_CHAN_TYPE_7_0_LHS_RHS:
	case HDMI_AUD_CHAN_TYPE_7_0_CS_CH:
	case HDMI_AUD_CHAN_TYPE_7_0_CS_OH:
	case HDMI_AUD_CHAN_TYPE_7_0_CS_CHR:
	case HDMI_AUD_CHAN_TYPE_7_0_CH_OH:
	case HDMI_AUD_CHAN_TYPE_7_0_CH_CHR:
	case HDMI_AUD_CHAN_TYPE_7_0_OH_CHR:
	case HDMI_AUD_CHAN_TYPE_7_0_LSS_RSS_LSR_RSR:
	case HDMI_AUD_CHAN_TYPE_8_0_LH_RH_CS:
		FR = 1;
		FL = 1;
		FC = 1;
		LFE = 0;
		RR = 1;
		RL = 1;
		RRC = 1;
		RLC = 1;
		ch_number = 7;
		break;

	case HDMI_AUD_CHAN_TYPE_7_1:
	case HDMI_AUD_CHAN_TYPE_7_1_LH_RH:
	case HDMI_AUD_CHAN_TYPE_7_1_LSR_RSR:
	case HDMI_AUD_CHAN_TYPE_7_1_LC_RC:
	case HDMI_AUD_CHAN_TYPE_7_1_LW_RW:
	case HDMI_AUD_CHAN_TYPE_7_1_LSD_RSD:
	case HDMI_AUD_CHAN_TYPE_7_1_LSS_RSS:
	case HDMI_AUD_CHAN_TYPE_7_1_LHS_RHS:
	case HDMI_AUD_CHAN_TYPE_7_1_CS_CH:
	case HDMI_AUD_CHAN_TYPE_7_1_CS_OH:
	case HDMI_AUD_CHAN_TYPE_7_1_CS_CHR:
	case HDMI_AUD_CHAN_TYPE_7_1_CH_OH:
	case HDMI_AUD_CHAN_TYPE_7_1_CH_CHR:
	case HDMI_AUD_CHAN_TYPE_7_1_OH_CHR:
	case HDMI_AUD_CHAN_TYPE_7_1_LSS_RSS_LSR_RSR:
		FR = 1;
		FL = 1;
		FC = 1;
		LFE = 1;
		RR = 1;
		RL = 1;
		RRC = 1;
		RLC = 1;
		ch_number = 8;
		break;

	default:
		FR = 1;
		FL = 1;
		ch_number = 2;
		break;
	}


	switch (ch_number) {
	case 8:
		break;

	case 7:
		break;

	case 6:
		if ((FR == 1) && (FL == 1) && (FC == 1) &&
			(RR == 1) && (RL == 1) && (RC == 1) &&
			(LFE == 0)) {
			/* 6.0 */
			ChannelMap = 0x0E;
		} else if ((FR == 1) && (FL == 1) && (FC == 1) &&
			(RR == 1) && (RL == 1) && (RC == 0) && (LFE == 1)) {
			/* 5.1 */
			ChannelMap = 0x0B;
		}
		break;

	case 5:
		break;

	case 4:
		if ((FR == 1) && (FL == 1) && (RR == 1) &&
		    (RL == 1) && (LFE == 0))
			ChannelMap = 0x08;
		else if ((FR == 1) && (FL == 1) && (FC == 1) &&
			(LFE == 1))
			ChannelMap = 0x03;
		break;

	case 3:
		if ((FR == 1) && (FL == 1) && (FC == 1))
			ChannelMap = 0x02;
		else if ((FR == 1) && (FL == 1) && (LFE == 1))
			ChannelMap = 0x01;
		break;

	case 2:
		if ((FR == 1) && (FL == 1))
			ChannelMap = 0x00;
		break;

	default:
		break;
	}

	HDMI_LOG("Ch_Num=%d, FR %d, FL %d, FC %d, LFE %d\n",
		ch_number, FR, FL, FC, LFE);
	HDMI_LOG("RR %d, RL %d, RRC %d, RLC %d, RC %d\n",
		RR, RL, RRC, RLC, RC);

	return ChannelMap;
}

//can.zeng done->void vSetChannelSwap(unsigned char u1SwapBit)
static inline void mtk_hdmi_hw_i2s_ch_swap(
	struct mtk_hdmi *hdmi, unsigned char SwapBit)
{
	mtk_hdmi_mask(hdmi, AIP_SPDIF_CTRL, SwapBit << 20, 0x0F << 20);
}

//can.zeng done->void vSetHdmiHbrConfig(bool fgRxDsdBypass)
static void mtk_hdmi_hbr_config(
	struct mtk_hdmi *hdmi, bool dsd_bypass)
{
	if (dsd_bypass == true) {
		mtk_hdmi_mask(hdmi, AIP_CTRL,
			HBRA_ON, SPDIF_EN | DSD_EN | HBRA_ON);
		mtk_hdmi_mask(hdmi, AIP_CTRL, I2S_EN, I2S_EN);
	} else {
		mtk_hdmi_mask(hdmi, AIP_CTRL,
			SPDIF_EN, SPDIF_EN | DSD_EN | HBRA_ON);
		mtk_hdmi_mask(hdmi, AIP_CTRL, SPDIF_INTERNAL_MODULE,
			SPDIF_INTERNAL_MODULE);
		mtk_hdmi_mask(hdmi, AIP_CTRL, HBR_FROM_SPDIF, HBR_FROM_SPDIF);
		mtk_hdmi_mask(hdmi, AIP_CTRL, CTS_CAL_N4, CTS_CAL_N4);

		/* can.zeng todo verify what is following setting for */
		/* vWriteHdmiTOPCKMsk(0xe8, (0x1 << 11),
		 *	(0x1 << 12) | (0x1 << 11));
		 */
	}
}

//can.zeng done->void vSetHdmiSpdifConfig(void)
static inline void mtk_hdmi_hw_spdif_config(struct mtk_hdmi *hdmi)
{
	mtk_hdmi_mask(hdmi, AIP_SPDIF_CTRL, WR_1UI_UNLOCK, WR_1UI_LOCK);
	mtk_hdmi_mask(hdmi, AIP_SPDIF_CTRL, FS_UNOVERRIDE, FS_OVERRIDE_WRITE);
	mtk_hdmi_mask(hdmi, AIP_SPDIF_CTRL, WR_2UI_UNLOCK, WR_2UI_LOCK);
	mtk_hdmi_mask(hdmi, AIP_SPDIF_CTRL, 0x4 <<
		MAX_1UI_WRITE_SHIFT, MAX_1UI_WRITE);
	mtk_hdmi_mask(hdmi, AIP_SPDIF_CTRL, 0x9 <<
		MAX_2UI_WRITE_SHIFT, MAX_2UI_WRITE);
	mtk_hdmi_mask(hdmi, AIP_SPDIF_CTRL, 0x4 <<
		AUD_ERR_THRESH_SHIFT, AUD_ERR_THRESH);
	mtk_hdmi_mask(hdmi, AIP_SPDIF_CTRL, I2S2DSD_EN, I2S2DSD_EN);
}

//can.zeng modification done->void vSetHDMIAudioIn(void)
static int mtk_hdmi_aud_set_input(struct mtk_hdmi *hdmi)
{
	unsigned char ChMapping;

	HDMI_FUNC();

	mtk_hdmi_write(hdmi, TOP_AUD_MAP,
			  C_SD7 + C_SD6 + C_SD5 + C_SD4 +
			  C_SD3 + C_SD2 + C_SD1 + C_SD0);
	mtk_hdmi_mask(hdmi, AIP_SPDIF_CTRL, 0, 0x0F << 20);
	mtk_hdmi_mask(hdmi, AIP_CTRL, 0, SPDIF_EN | DSD_EN | HBRA_ON |
			 CTS_CAL_N4 | HBR_FROM_SPDIF | SPDIF_INTERNAL_MODULE);
	mtk_hdmi_mask(hdmi, AIP_TXCTRL, 0, DSD_MUTE_DATA | LAYOUT1);

	if (hdmi->aud_param.aud_input_type == HDMI_AUD_INPUT_I2S) {
		if (hdmi->aud_param.aud_codec == HDMI_AUDIO_CODING_TYPE_DSD) {
			mtk_hdmi_audio_dsd_config(hdmi,
				hdmi->aud_param.codec_params.channels, 0);
			mtk_hdmi_hw_i2s_ch_mapping(hdmi,
				hdmi->aud_param.codec_params.channels, 1);
		} else {
			mtk_hdmi_i2s_data_fmt(hdmi,
				hdmi->aud_param.aud_i2s_fmt);
			mtk_hdmi_i2s_sck_edge(hdmi, SCK_EDGE_RISE);
			mtk_hdmi_i2s_cbit_order(hdmi, CBIT_ORDER_SAME);
			mtk_hdmi_i2s_vbit(hdmi, VBIT_PCM);
			mtk_hdmi_i2s_data_direction(hdmi, DATA_DIR_MSB);
			mtk_hdmi_hw_audio_type(hdmi, HDMI_AUD_INPUT_I2S);
			ChMapping = mtk_hdmi_get_i2s_ch_mapping(hdmi,
				hdmi->aud_param.aud_input_chan_type);
			mtk_hdmi_hw_i2s_ch_mapping(hdmi,
				hdmi->aud_param.codec_params.channels,
				ChMapping);
			mtk_hdmi_hw_i2s_ch_swap(hdmi, LFE_CC_SWAP);
		}
	} else {
		if ((hdmi->aud_param.aud_input_type == HDMI_AUD_INPUT_SPDIF) &&
		((hdmi->aud_param.aud_codec == HDMI_AUDIO_CODING_TYPE_DTS_HD)
		|| (hdmi->aud_param.aud_codec == HDMI_AUDIO_CODING_TYPE_MLP))
		&& (hdmi->aud_param.codec_params.sample_rate == 768000)) {
			mtk_hdmi_hbr_config(hdmi, false);
		} else {
			mtk_hdmi_hw_spdif_config(hdmi);
			mtk_hdmi_hw_i2s_ch_mapping(hdmi, 2, 0);
		}
	}

	return 0;
}

static int mtk_hdmi_aud_set_sw_ncts(struct mtk_hdmi *hdmi,
				struct drm_display_mode *display_mode)
{
	unsigned int sample_rate = hdmi->aud_param.codec_params.sample_rate;

	mtk_hdmi_aud_on_off_hw_ncts(hdmi, false);

	mtk_hdmi_hw_aud_set_ncts(hdmi, sample_rate, display_mode->clock);

	return 0;
}

//can.zeng done->void vEnableAudio(unsigned int bOn)
static inline void mtk_hdmi_hw_audio_input_enable(
	struct mtk_hdmi *hdmi, unsigned int enable)
{
	if (enable == true)
		mtk_hdmi_mask(hdmi, AIP_CTRL, AUD_IN_EN, AUD_IN_EN);
	else
		mtk_hdmi_mask(hdmi, AIP_CTRL, 0x0 << AUD_IN_EN_SHIFT,
			AUD_IN_EN);
}

//void vAipCtrlInit(void)
static void mtk_hdmi_aip_ctrl_init(struct mtk_hdmi *hdmi)
{
	mtk_hdmi_mask(hdmi, AIP_CTRL,
		AUD_SEL_OWRT | NO_MCLK_CTSGEN_SEL | CTS_REQ_EN,
		AUD_SEL_OWRT | NO_MCLK_CTSGEN_SEL | MCLK_EN | CTS_REQ_EN);
	mtk_hdmi_mask(hdmi, AIP_TPI_CTRL, TPI_AUDIO_LOOKUP_DIS,
		TPI_AUDIO_LOOKUP_EN);
}

//can.zeng modification done->void vResetAudioHDMI(unsigned char bRst)
static void mtk_hdmi_audio_reset(struct mtk_hdmi *hdmi, bool rst)
{
	HDMI_FUNC();

	if (rst == true) {
		mtk_hdmi_mask(hdmi, AIP_TXCTRL, RST4AUDIO |
			RST4AUDIO_FIFO | RST4AUDIO_ACR,
			RST4AUDIO | RST4AUDIO_FIFO | RST4AUDIO_ACR);
	} else {
		mtk_hdmi_mask(hdmi, AIP_TXCTRL, 0,
			RST4AUDIO | RST4AUDIO_FIFO | RST4AUDIO_ACR);
	}
}

//can.zeng modification done-> vChgHDMIAudioOutput()
static int mtk_hdmi_aud_output_config(struct mtk_hdmi *hdmi,
				      struct drm_display_mode *display_mode)
{
	HDMI_FUNC();

	/*can.zeng todo verify-> what is this setting for */
	mtk_hdmi_write(hdmi, HDMITX_CONFIG, 0xffff0200);//1006

	mtk_hdmi_hw_aud_mute(hdmi);
	mtk_hdmi_aud_enable_packet(hdmi, false);
	mtk_hdmi_audio_reset(hdmi, true);
	mtk_hdmi_aip_ctrl_init(hdmi);

	mtk_hdmi_aud_set_input(hdmi);

	mtk_hdmi_hw_aud_set_channel_status(hdmi,
			hdmi->aud_param.codec_params.iec.status);

	//can.zeng todo check
	//mtk_hdmi_setup_audio_infoframe(hdmi);
	//this function move to inside mtk_hdmi_send_infoframe

	mtk_hdmi_hw_audio_input_enable(hdmi, true);

	mtk_hdmi_audio_reset(hdmi, false);

	mtk_hdmi_aud_set_sw_ncts(hdmi, display_mode);

	udelay(25);
	mtk_hdmi_aud_on_off_hw_ncts(hdmi, true);

	mtk_hdmi_aud_enable_packet(hdmi, true);
	mtk_hdmi_hw_aud_unmute(hdmi);
	return 0;
}

static int mtk_hdmi_setup_avi_infoframe(struct mtk_hdmi *hdmi,
					struct drm_display_mode *mode)
{
	struct hdmi_avi_infoframe frame;
	u8 buffer[17];
	ssize_t err;

	//err = drm_hdmi_avi_infoframe_from_display_mode(&frame, mode, true);
	err = drm_hdmi_avi_infoframe_from_display_mode(&frame, &(hdmi->conn), mode);
	/*can.zeng todo verify the 3th parameter,  bool is_hdmi2_sink,
	 *should recognize 2.0 hdmi device from edid
	 */
	if (err < 0) {
		dev_err(hdmi->dev,
			"Failed to get AVI infoframe from mode: %zd\n", err);
		return err;
	}

	err = hdmi_avi_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		dev_err(hdmi->dev, "Failed to pack AVI infoframe: %zd\n", err);
		return err;
	}

	mtk_hdmi_hw_avi_infoframe(hdmi, buffer, sizeof(buffer));
	return 0;
}

static int mtk_hdmi_setup_spd_infoframe(struct mtk_hdmi *hdmi,
					const char *vendor,
					const char *product)
{
	struct hdmi_spd_infoframe frame;
	u8 buffer[29];
	ssize_t err;

	err = hdmi_spd_infoframe_init(&frame, vendor, product);
	if (err < 0) {
		dev_err(hdmi->dev, "Failed to initialize SPD infoframe: %zd\n",
			err);
		return err;
	}

	err = hdmi_spd_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		dev_err(hdmi->dev, "Failed to pack SDP infoframe: %zd\n", err);
		return err;
	}

	mtk_hdmi_hw_spd_infoframe(hdmi, buffer, sizeof(buffer));
	return 0;
}

static int mtk_hdmi_output_init(struct mtk_hdmi *hdmi)
{
	struct hdmi_audio_param *aud_param = &hdmi->aud_param;

	hdmi->csp = HDMI_COLORSPACE_RGB;
	aud_param->aud_codec = HDMI_AUDIO_CODING_TYPE_PCM;
	aud_param->aud_sampe_size = HDMI_AUDIO_SAMPLE_SIZE_16;
	aud_param->aud_input_type = HDMI_AUD_INPUT_I2S;
	aud_param->aud_i2s_fmt = HDMI_I2S_MODE_I2S_24BIT;
	aud_param->aud_mclk = HDMI_AUD_MCLK_128FS;
	aud_param->aud_input_chan_type = HDMI_AUD_CHAN_TYPE_2_0;

	/* hpd initial state */
	hdmi->hpd = false;
	/* color depth initial state */
	hdmi->color_depth = HDMI_8_BIT;
	/* hdr type initial state */
	hdmi->hdr_type = HDR_TYPE_SDR;

	return 0;
}

/* 16 - 1920x1080@60Hz 16:9 */
struct drm_display_mode display_mode_1080p = {
	DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2008,
	2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, };

static int mtk_hdmi_txpll_clk_init(struct mtk_hdmi *hdmi)
{
	if (hdmi == NULL)
		return -EINVAL;

	memcpy(&hdmi->mode, &display_mode_1080p, sizeof(struct drm_display_mode));
	udelay(2);
	mtk_hdmi_video_change_vpll(hdmi, hdmi->mode.clock * 1000);
	udelay(2);
	clk_prepare_enable(hdmi->clk[MTK_HDMI_CLK_HDMI_TXPLL]);

	return 0;
}

static void mtk_hdmi_audio_enable(struct mtk_hdmi *hdmi)
{
	HDMI_FUNC();

	mtk_hdmi_aud_enable_packet(hdmi, true);
	hdmi->audio_enable = true;
}

static void mtk_hdmi_audio_disable(struct mtk_hdmi *hdmi)
{
	HDMI_FUNC();

	mtk_hdmi_aud_enable_packet(hdmi, false);
	hdmi->audio_enable = false;
}

static int mtk_hdmi_audio_set_param(struct mtk_hdmi *hdmi,
				    struct hdmi_audio_param *param)
{
	if (!hdmi->audio_enable) {
		dev_err(hdmi->dev, "hdmi audio is in disable state!\n");
		return -EINVAL;
	}
	dev_dbg(hdmi->dev, "codec:%d, input:%d, channel:%d, fs:%d\n",
		param->aud_codec, param->aud_input_type,
		param->aud_input_chan_type, param->codec_params.sample_rate);
	memcpy(&hdmi->aud_param, param, sizeof(*param));
	return mtk_hdmi_aud_output_config(hdmi, &hdmi->mode);
}


//can.zeng modification done-> void vChgHDMIVideoResolution(void)
static void mtk_hdmi_change_video_resolution(struct mtk_hdmi *hdmi)
{
	bool is_over_340M = false;
	bool is_hdmi_sink = false;
	unsigned char enscramble;
	struct mtk_hdmi_ddc *ddc = hdmi_ddc_ctx_from_mtk_hdmi(hdmi);

	mtk_hdmi_hw_reset(hdmi);

	mtk_hdmi_colorspace_setting(hdmi);
	udelay(2);

	mtk_hdmi_write(hdmi, HDCP_TOP_CTRL, 0x0);
	mtk_hdmi_en_hdcp_reauth_int(hdmi, true);
	mtk_hdmi_force_hdcp_hpd(hdmi);

	is_hdmi_sink = mtk_hdmi_sink_is_hdmi_device(hdmi);
	mtk_hdmi_set_deep_color(hdmi, is_hdmi_sink);
	mtk_hdmi_enable_hdmi_mode(hdmi, is_hdmi_sink);

	udelay(5);
	mtk_hdmi_hw_vid_black(hdmi, true);
	mtk_hdmi_hw_aud_mute(hdmi);
	mtk_hdmi_hw_send_av_unmute(hdmi);

	mtk_hdmi_mask(hdmi, TOP_CFG01, NULL_PKT_VSYNC_HIGH_EN,
		NULL_PKT_VSYNC_HIGH_EN | NULL_PKT_EN);

	is_over_340M = mtk_hdmi_tmds_over_340M(hdmi);
	if (is_over_340M == true)
		enscramble = SCRAMBLING_ENABLE | TMDS_BIT_CLOCK_RATION;
	else
		enscramble = 0;
	pr_info("[james] %s %s, %d\n", __FILE__, __FUNCTION__, __LINE__);
	fgDDCDataWrite(ddc, RX_REG_SCRAMBLE >> 1,
		RX_REG_TMDS_CONFIG, 1, &enscramble);
	pr_info("[james] %s %s, %d\n", __FILE__, __FUNCTION__, __LINE__);

	mtk_hdmi_enable_scrambling(hdmi, is_over_340M);
	mtk_hdmi_high_tmds_clock_ratio(hdmi, is_over_340M);

	if (hdmi->csp == HDMI_COLORSPACE_YUV420)
		mtk_hdmi_yuv420_downsample(hdmi, true);
	else
		mtk_hdmi_yuv420_downsample(hdmi, false);

	mtk_hdmi_480p_576p_setting(hdmi);
	/* hdmitx_confighdmisetting(vformat); necessary here? */

}

static int mtk_hdmi_output_set_display_mode(struct mtk_hdmi *hdmi,
					    struct drm_display_mode *mode)
{
	int ret;

	mtk_hdmi_hw_send_av_mute(hdmi);
	usleep_range(50000, 50050);

	mtk_hdmi_hw_vid_black(hdmi, true);
	mtk_hdmi_hw_aud_mute(hdmi);

	mtk_hdmi_disable_hdcp_encrypt(hdmi);
	usleep_range(50000, 50050);

/*	can.zeng todo verify
 *	phy_power_off(hdmi->phy);
 */
	vTxSignalOnOff(hdmi->hdmi_phy_base, false);

	if (__clk_is_enabled(hdmi->clk[MTK_HDMI_CLK_HDMI_TXPLL]) == true)
		clk_disable_unprepare(hdmi->clk[MTK_HDMI_CLK_HDMI_TXPLL]);

	/* vHDCPReset(); can.zeng remove mask
	 * mtk_hdmi_hdcp_TZ_hdcp_version(hdmi); can.zeng remove mask
	 *can.zeng nend to check when to get HDCP version
	 *that sink support through DDC
	 */

	ret = mtk_hdmi_video_change_vpll(hdmi, mode->clock * 1000);
	if (ret) {
		dev_err(hdmi->dev, "Failed to set vpll: %d\n", ret);
		return ret;
	}

	mtk_hdmi_change_video_resolution(hdmi);

	mtk_hdmi_aud_output_config(hdmi, mode);

	return 0;
}

static int mtk_hdmi_get_all_clk(struct mtk_hdmi *hdmi,
				struct device_node *np)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mtk_hdmi_clk_names); i++) {
		//can.zeng todo verify, only for bring up
		if (i == MTK_HDMI_CLK_HDMI_TXPLL)
			hdmi->clk[i] = hdmi->hdmi_phy_base->txpll;
#if 1
		else
			hdmi->clk[i] = of_clk_get_by_name(np,
				mtk_hdmi_clk_names[i]);

		if (IS_ERR(hdmi->clk[i]))
			return PTR_ERR(hdmi->clk[i]);
#endif
	}

	//can.zeng todo verify, enalbe all clks temporarily
	for (i = 0; i < ARRAY_SIZE(mtk_hdmi_clk_names); i++) {
		if (i == MTK_HDMI_CLK_HDMI_TXPLL)
			continue;
#if 1
		else
			clk_prepare_enable(hdmi->clk[i]);
#endif
	}

	return 0;
}

static int mtk_hdmi_clk_enable_audio(struct mtk_hdmi *hdmi)
{
	//can.zeng todo verify, which clk for audio
	return 0;
}

static void mtk_hdmi_clk_disable_audio(struct mtk_hdmi *hdmi)
{
	//can.zeng todo verify, which clk for audio
}

void mtk_hdmi_enable_hpd_pord_irq(struct mtk_hdmi *hdmi, bool enable)
{
	if (enable == true)
		mtk_hdmi_mask(hdmi, TOP_INT_MASK00, 0x0000000f, 0x0000000f);
	else
		mtk_hdmi_mask(hdmi, TOP_INT_MASK00, 0x00000000, 0x0000000f);
}

void mtk_hdmi_clr_htplg_pord_irq(struct mtk_hdmi *hdmi)
{
	mtk_hdmi_mask(hdmi, TOP_INT_CLR00, 0x0000000f, 0x0000000f);
	mtk_hdmi_mask(hdmi, TOP_INT_CLR00, 0x00000000, 0x0000000f);
}

static void mtk_hdmi_hpd_event(bool hpd, struct device *dev)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	if (hdmi && hdmi->bridge.encoder && hdmi->bridge.encoder->dev)
		drm_helper_hpd_irq_event(hdmi->bridge.encoder->dev);
}

bool mtk_hdmi_hpd_high(struct device *dev)
{
	//struct mtk_hdmi *hdmi = dev_get_drvdata(dev);
	struct mtk_hdmi *hdmi = global_mtk_hdmi;
	unsigned int status;

	status = mtk_hdmi_read(hdmi, HPD_DDC_STATUS);

	//can.zeng remove hpd interrupt temporary
/*
 *	return (status & (HPD_PIN_STA | PORD_PIN_STA)) ==
 *		(HPD_PIN_STA | PORD_PIN_STA);
 */
	return ((status & PORD_PIN_STA) == PORD_PIN_STA);
}

static irqreturn_t mtk_hdmi_htplg_isr(int irq, void *arg)
{
	struct device *dev = arg;
	//can.zeng todo verify
	struct mtk_hdmi *hdmi = global_mtk_hdmi;
	bool hpd;

	HDMI_LOG("hdmi IRQ!!\n");

	if (hdmi == NULL) {
		HDMI_LOG("irq: hdmi handle is NULL\n");
		return IRQ_HANDLED;
	}

	mtk_hdmi_enable_hpd_pord_irq(hdmi, false);
	mtk_hdmi_clr_htplg_pord_irq(hdmi);
	hpd = mtk_hdmi_hpd_high(dev);

	if (hdmi->hpd != hpd) {
		HDMI_LOG("hotplug event! old hpd = %d, new hpd = %d\n",
			hdmi->hpd, hpd);
		//hdmi->hpd = hpd;
		//mtk_hdmi_hpd_event(hpd, hdmi->dev);
		queue_work(hdmi->hpd_wq, &hdmi->hpd_work);
	}
	mtk_hdmi_enable_hpd_pord_irq(hdmi, true);
	return IRQ_HANDLED;
}

static inline void mtk_hdmi_audio_extcon_state(
	struct mtk_hdmi *hdmi, bool state)
{
	extcon_set_state_sync(hdmi_extcon, EXTCON_DISP_HDMI, state);
}


static enum drm_connector_status hdmi_conn_detect(struct drm_connector *conn,
						  bool force)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_conn(conn);
	bool hpd = mtk_hdmi_hpd_high(hdmi->dev);

	HDMI_LOG();

	mtk_hdmi_audio_extcon_state(hdmi, hpd);


	/* hpd false, clr all the edid raw data */
	if (hpd == false) {
		memset(hdmi->raw_edid.edid, 0,
			sizeof(hdmi->raw_edid.blk_num * EDID_LENGTH));
		hdmi->raw_edid.blk_num = 0;
		HDMI_LOG("HPD low\n");
		//can.zeng todo verify
		hdmi_notify_uevent_user(&hdmi_switch_data, 0);
	} else {
		HDMI_LOG("HPD High\n");
		hdmi_notify_uevent_user(&hdmi_switch_data, 1);
	}

	return (hpd == true) ? connector_status_connected : connector_status_disconnected;
}

static void hdmi_conn_destroy(struct drm_connector *conn)
{
	drm_connector_cleanup(conn);
}

struct mtk_hdmi_edid *mtk_hdmi_get_raw_edid(struct drm_bridge *bridge)
{
	struct mtk_hdmi *hdmi =	hdmi_ctx_from_bridge(bridge);
	struct edid *edid = (struct edid *)(hdmi->raw_edid.edid);

	if (!drm_edid_is_valid(edid)) {
		HDMI_LOG("EDID invalid\n");
		return NULL;
	}

	return &(hdmi->raw_edid);
}
EXPORT_SYMBOL(mtk_hdmi_get_raw_edid);

static int mtk_hdmi_conn_get_modes(struct drm_connector *conn)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_conn(conn);
	struct mtk_hdmi_ddc *ddc = hdmi_ddc_ctx_from_mtk_hdmi(hdmi);
	unsigned char *edid_buff;
	struct edid *edid;
	int ret;
	bool edid_read;

	edid_buff = kzalloc(EDID_LENGTH * 2, GFP_KERNEL);

	HDMI_LOG("start\n");

	if (!hdmi->ddc_adpt)
		return -ENODEV;

	//edid = drm_get_edid(conn, hdmi->ddc_adpt);
	edid_read = fgDDCDataRead(ddc, 0x50, 0x00, EDID_LENGTH, &edid_buff[0]);
	if (edid_read == true)
		HDMI_LOG("read Block 0 success\n");
	else {
		edid_read = fgDDCDataRead(ddc, 0x50, 0x00, EDID_LENGTH, &edid_buff[0]);
		if (edid_read == true)
			HDMI_LOG("RE-read Block 0 success\n");
	}


	edid_read = fgDDCDataRead(ddc, 0x50, 0x80, EDID_LENGTH, &edid_buff[128]);

	if (edid_read == true)
		HDMI_LOG("read Block 1 success\n");
	else {
		edid_read = fgDDCDataRead(ddc, 0x50, 0x80, EDID_LENGTH, &edid_buff[128]);
		if (edid_read == true)
			HDMI_LOG("RE-read Block 1 success\n");
	}

	edid = (struct edid *)edid_buff;
	if (!drm_edid_is_valid(edid)) {
		HDMI_LOG("EDID invalid\n");
		return -EINVAL;
	}

	if ((edid->extensions + 1) <= 4)
		hdmi->raw_edid.blk_num = edid->extensions + 1;
	else
		hdmi->raw_edid.blk_num = 4;

	memcpy(hdmi->raw_edid.edid, edid,
		sizeof(hdmi->raw_edid.blk_num * EDID_LENGTH));


	hdmi->dvi_mode = !drm_detect_monitor_audio(edid);

	drm_connector_update_edid_property(conn, edid); //can.zeng todo check
	/*drm_mode_connector_update_edid_property
	 * VS
	 *drm_connector_update_edid_property
	 *what is difference??
	 */
	ret = drm_add_edid_modes(conn, edid);
	//drm_edid_to_eld(conn, edid);
	/*drm_add_edid_mode call drm_edid_to_eld internally,
	 *no need anymore
	 */
	kfree(edid_buff);

	HDMI_LOG("end\n");


	return ret;
}

static int mtk_hdmi_conn_mode_valid(struct drm_connector *conn,
				    struct drm_display_mode *mode)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_conn(conn);

#if 0
	HDMI_LOG("xres=%d, yres=%d, refresh=%d, intl=%d clock=%d\n",
		mode->hdisplay, mode->vdisplay, mode->vrefresh,
		!!(mode->flags & DRM_MODE_FLAG_INTERLACE), mode->clock * 1000);

	if (hdmi->bridge.next) {
		struct drm_display_mode adjusted_mode;

		drm_mode_copy(&adjusted_mode, mode);
		if (!drm_bridge_mode_fixup(hdmi->bridge.next, mode,
					   &adjusted_mode)) {
			HDMI_LOG("MODE BAD!\n");
			return MODE_BAD;
		}
	}
#endif

	if (mode->clock < 27000) {
		HDMI_LOG("MODE_CLOCK_LOW!\n");
		return MODE_CLOCK_LOW;
	}
	if (mode->clock > 594000) {
		HDMI_LOG("MODE_CLOCK_HIGH!\n");
		return MODE_CLOCK_HIGH;
	}

	return drm_mode_validate_size(mode, 0x1fff, 0x1fff);
}

static struct drm_encoder *mtk_hdmi_conn_best_enc(struct drm_connector *conn)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_conn(conn);

	HDMI_LOG();

	return hdmi->bridge.encoder;
}

static const struct drm_connector_funcs mtk_hdmi_connector_funcs = {
	/* .dpms = drm_atomic_helper_connector_dpms, */
	.detect = hdmi_conn_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = hdmi_conn_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs
		mtk_hdmi_connector_helper_funcs = {
	.get_modes = mtk_hdmi_conn_get_modes,
	.mode_valid = mtk_hdmi_conn_mode_valid,
	.best_encoder = mtk_hdmi_conn_best_enc,
};

/*
 * Bridge callbacks
 */

//static int mtk_hdmi_bridge_attach(struct drm_bridge *bridge)
static int mtk_hdmi_bridge_attach(struct drm_bridge *bridge,
							enum drm_bridge_attach_flags flags)

{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);
	int ret;

	HDMI_LOG("start\n");

	ret = drm_connector_init(bridge->encoder->dev, &hdmi->conn,
				 &mtk_hdmi_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		HDMI_LOG("Failed to initialize connector: %d\n", ret);
		return ret;
	}
	drm_connector_helper_add(&hdmi->conn, &mtk_hdmi_connector_helper_funcs);

	hdmi->conn.polled = DRM_CONNECTOR_POLL_HPD;
	hdmi->conn.interlace_allowed = true;
	hdmi->conn.doublescan_allowed = false;

	ret = drm_connector_attach_encoder(&hdmi->conn,
						bridge->encoder);
	if (ret) {
		HDMI_LOG("Failed to attach connector to encoder: %d\n", ret);
		return ret;
	}
#if 0
	if (bridge->next) {
		bridge->next->encoder = bridge->encoder;
		ret = drm_bridge_attach(bridge->encoder, bridge->next, NULL);
		if (ret) {
			HDMI_LOG("Failed to attach external bridge: %d\n", ret);
			return ret;
		}
	}
#endif
	HDMI_LOG("end\n");


	return 0;
}

static bool mtk_hdmi_bridge_mode_fixup(struct drm_bridge *bridge,
				       const struct drm_display_mode *mode,
				       struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void mtk_hdmi_bridge_disable(struct drm_bridge *bridge)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	if (!hdmi->enabled)
		return;

/*	can.zeng todo verify
 *	phy_power_off(hdmi->phy);
 */
	vTxSignalOnOff(hdmi->hdmi_phy_base, false);
	clk_disable_unprepare(hdmi->clk[MTK_HDMI_CLK_HDMI_TXPLL]);

	//can.zeng todo verify
	//clk_disable_unprepare(hdmi->clk[MTK_HDMI_CLK_HDMI_PIXEL]);

	hdmi->enabled = false;
}

static void mtk_hdmi_bridge_post_disable(struct drm_bridge *bridge)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	HDMI_LOG("start\n");

	if (!hdmi->powered)
		return;

	hdmi->powered = false;

	/* clear all the edid raw data */
	memset(hdmi->raw_edid.edid, 0,
		sizeof(hdmi->raw_edid.blk_num * EDID_LENGTH));
	hdmi->raw_edid.blk_num = 0;

	HDMI_LOG("end\n");


}

//static void mtk_hdmi_bridge_mode_set(struct drm_bridge *bridge,
//				     struct drm_display_mode *mode,
//				     struct drm_display_mode *adjusted_mode)
static void mtk_hdmi_bridge_mode_set(struct drm_bridge *bridge,
		const struct drm_display_mode *mode, const struct drm_display_mode *adjusted_mode)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	HDMI_LOG("start\n");

	HDMI_LOG("cur info: name:%s, hdisplay:%d\n",
		adjusted_mode->name, adjusted_mode->hdisplay);
	HDMI_LOG("hsync_start:%d,hsync_end:%d, htotal:%d",
		adjusted_mode->hsync_start, adjusted_mode->hsync_end,
		adjusted_mode->htotal);
	HDMI_LOG("hskew:%d, vdisplay:%d\n",
		adjusted_mode->hskew, adjusted_mode->vdisplay);
	HDMI_LOG("vsync_start:%d, vsync_end:%d, vtotal:%d",
		adjusted_mode->vsync_start, adjusted_mode->vsync_end,
		adjusted_mode->vtotal);
	HDMI_LOG("vscan:%d, flag:%d\n",
		adjusted_mode->vscan, adjusted_mode->flags);

	drm_mode_copy(&hdmi->mode, adjusted_mode);

	HDMI_LOG("end\n");

}

static void mtk_hdmi_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	hdmi->powered = true;
}

static void mtk_hdmi_send_infoframe(struct mtk_hdmi *hdmi,
				    struct drm_display_mode *mode)
{
	mtk_hdmi_setup_audio_infoframe(hdmi);
	mtk_hdmi_setup_avi_infoframe(hdmi, mode);
	mtk_hdmi_setup_spd_infoframe(hdmi, "mediatek", "On-chip HDMI");
	if ((mode->flags & DRM_MODE_FLAG_3D_MASK) ||
		(hdmi->hdr_type == HDR_TYPE_DOLBY_VISION))
		mtk_hdmi_setup_h14b_vsif(hdmi, mode);

	if (0) //can.zeng todo verify the condition
		mtk_hdmi_setup_hf_vsif(hdmi, mode);

	if (hdmi->hdr_type == HDR_TYPE_HDR10)
		mtk_hdmi_setup_hdr10_infoframe(hdmi);
	else if (hdmi->hdr_type == HDR_TYPE_SAMSUNG_HDR10PLUS_VSIF)
		mtk_hdmi_setup_hdr10plus_vsif(hdmi);
	else if (hdmi->hdr_type == HDR_TYPE_DOLBY_VISION_LOWLATENCY)
		mtk_hdmi_setup_dolby_lowlatency_vsif(hdmi);
	else if (hdmi->hdr_type == HDR_TYPE_HDR10PLUS_EMP)
		mtk_hdmi_setup_hdr10p_emp(hdmi);

}

static void mtk_hdmi_bridge_enable(struct drm_bridge *bridge)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	mtk_hdmi_output_set_display_mode(hdmi, &hdmi->mode);
	mtk_hdmi_send_infoframe(hdmi, &hdmi->mode);
	//can.zeng todo verify
	//clk_prepare_enable(hdmi->clk[MTK_HDMI_CLK_HDMI_PIXEL]);
	msleep(100);
/*	can.zeng todo verify
 *	phy_power_on(hdmi->phy);
 */
	clk_prepare_enable(hdmi->clk[MTK_HDMI_CLK_HDMI_TXPLL]);
	vTxSignalOnOff(hdmi->hdmi_phy_base, true);
	usleep_range(100, 150);


#ifdef CONFIG_MTK_HDMI_HDCP_SUPPORT
	//can.zeng need to verify, start HDCP authentication here?
	//av_hdmiset(HDMI_SET_HDCP_INITIAL_AUTH, &_stAvdAVInfo, 1);
#else
	mtk_hdmi_hw_vid_black(hdmi, false);
	mtk_hdmi_hw_aud_unmute(hdmi);
#endif

	hdmi->enabled = true;
}

static const struct drm_bridge_funcs mtk_hdmi_bridge_funcs = {
	.attach = mtk_hdmi_bridge_attach,
	.mode_fixup = mtk_hdmi_bridge_mode_fixup,
	.disable = mtk_hdmi_bridge_disable,
	.post_disable = mtk_hdmi_bridge_post_disable,
	.mode_set = mtk_hdmi_bridge_mode_set,
	.pre_enable = mtk_hdmi_bridge_pre_enable,
	.enable = mtk_hdmi_bridge_enable,
};

static int mtk_hdmi_dt_parse_pdata(struct mtk_hdmi *hdmi,
				   struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	//struct device_node *cec_np,
	//struct device_node *port, *ep, *remote, *i2c_np;
	//struct device_node *remote,
	struct device_node *i2c_np;
	//struct platform_device *cec_pdev;
	struct resource *mem;
	int ret;
	struct mtk_hdmi_ddc *ddc;

	//can.zeng todo verify, where to pm_runtime_disable
	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_get_sync(&pdev->dev);
	HDMI_LOG("Power Domain On %d\n", ret);

	ret = mtk_hdmi_get_all_clk(hdmi, np);
	if (ret) {
		HDMI_LOG("Failed to get clocks: %d\n", ret);
		return ret;
	}

/*
 *	The CEC module handles HDMI hotplug detection
 *	cec_np = of_find_compatible_node(np->parent, NULL,
 *					 "mediatek,mt8173-cec");
 *	if (!cec_np) {
 *		HDMI_LOG(dev, "Failed to find CEC node\n");
 *		return -EINVAL;
 *	}
 *
 *	cec_pdev = of_find_device_by_node(cec_np);
 *	if (!cec_pdev) {
 *		HDMI_LOG("Waiting for CEC device %s\n",
 *			cec_np->full_name);
 *		return -EPROBE_DEFER;
 *	}
 *	hdmi->cec_dev = &cec_pdev->dev;
 */

	/*
	 * The mediatek,syscon-hdmi property contains a phandle link to the
	 * MMSYS_CONFIG device and the register offset of the HDMI_SYS_CFG
	 * registers it contains.
	 */
/*	regmap = syscon_regmap_lookup_by_phandle(np, "mediatek,syscon-hdmi");
 *	ret = of_property_read_u32_index(np, "mediatek,syscon-hdmi", 1,
 *					 &hdmi->sys_offset);
 *	if (IS_ERR(regmap))
 *		ret = PTR_ERR(regmap);
 *	if (ret) {
 *		ret = PTR_ERR(regmap);
 *		HDMI_LOG("Failed to get system configuration registers: %d\n",
 *			ret);
 *		return ret;
 *	}
 *	hdmi->sys_regmap = regmap;
 */

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hdmi->regs = devm_ioremap_resource(dev, mem);
	if (IS_ERR(hdmi->regs))
		return PTR_ERR(hdmi->regs);

//can.zeng todo remove mask
/*	port = of_graph_get_port_by_id(np, 1);
 *	if (!port) {
 *		HDMI_LOG("Missing output port node\n");
 *		return -EINVAL;
 *	}
 *
 *	ep = of_get_child_by_name(port, "endpoint");
 *	if (!ep) {
 *		HDMI_LOG("Missing endpoint node in port %s\n",
 *			port->full_name);
 *		of_node_put(port);
 *		return -EINVAL;
 *	}
 *	of_node_put(port);
 *
 *	remote = of_graph_get_remote_port_parent(ep);
 *	if (!remote) {
 *		HDMI_LOG("Missing connector/bridge node for endpoint %s\n",
 *			ep->full_name);
 *		of_node_put(ep);
 *		return -EINVAL;
 *	}
 *	of_node_put(ep);
 *
 *	if (!of_device_is_compatible(remote, "hdmi-connector")) {
 *		hdmi->bridge.next = of_drm_find_bridge(remote);
 *		if (!hdmi->bridge.next) {
 *			HDMI_LOG("Waiting for external bridge\n");
 *			of_node_put(remote);
 *			return -EPROBE_DEFER;
 *		}
 *	}
 */
	i2c_np = of_parse_phandle(pdev->dev.of_node, "ddc-i2c-bus", 0);
	if (!i2c_np) {
		HDMI_LOG("Failed to find ddc-i2c-bus node in %s\n",
			(pdev->dev.of_node)->full_name);
		of_node_put(pdev->dev.of_node);
		return -EINVAL;
	}
	of_node_put(pdev->dev.of_node);

	hdmi->ddc_adpt = of_find_i2c_adapter_by_node(i2c_np);
	if (!hdmi->ddc_adpt) {
		HDMI_LOG("Failed to get ddc i2c adapter by node\n");
		return -EINVAL;
	}

	//can.zeng todo verify
	ddc = hdmi_ddc_ctx_from_mtk_hdmi(hdmi);
	ddc->regs = hdmi->regs;

	return 0;
}

/*
 * HDMI audio codec callbacks
 */

static int mtk_hdmi_audio_hw_params(struct device *dev, void *data,
				    struct hdmi_codec_daifmt *daifmt,
				    struct hdmi_codec_params *params)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);
	struct hdmi_audio_param hdmi_params;
	unsigned int chan = params->cea.channels;

	dev_dbg(hdmi->dev, "%s: %u Hz, %d bit, %d channels\n", __func__,
		params->sample_rate, params->sample_width, chan);

	if (!hdmi->bridge.encoder)
		return -ENODEV;

	switch (chan) {
	case 2:
		hdmi_params.aud_input_chan_type = HDMI_AUD_CHAN_TYPE_2_0;
		break;
	case 4:
		hdmi_params.aud_input_chan_type = HDMI_AUD_CHAN_TYPE_4_0;
		break;
	case 6:
		hdmi_params.aud_input_chan_type = HDMI_AUD_CHAN_TYPE_5_1;
		break;
	case 8:
		hdmi_params.aud_input_chan_type = HDMI_AUD_CHAN_TYPE_7_1;
		break;
	default:
		dev_err(hdmi->dev, "channel[%d] not supported!\n", chan);
		return -EINVAL;
	}

	switch (params->sample_rate) {
	case 32000:
	case 44100:
	case 48000:
	case 88200:
	case 96000:
	case 176400:
	case 192000:
		break;
	default:
		dev_err(hdmi->dev, "rate[%d] not supported!\n",
			params->sample_rate);
		return -EINVAL;
	}

	switch (daifmt->fmt) {
	case HDMI_I2S:
		hdmi_params.aud_codec = HDMI_AUDIO_CODING_TYPE_PCM;
		hdmi_params.aud_sampe_size = HDMI_AUDIO_SAMPLE_SIZE_16;
		hdmi_params.aud_input_type = HDMI_AUD_INPUT_I2S;
		hdmi_params.aud_i2s_fmt = HDMI_I2S_MODE_I2S_24BIT;
		hdmi_params.aud_mclk = HDMI_AUD_MCLK_128FS;
		break;
	default:
		dev_err(hdmi->dev, "%s: Invalid DAI format %d\n", __func__,
			daifmt->fmt);
		return -EINVAL;
	}

	memcpy(&hdmi_params.codec_params, params,
	       sizeof(hdmi_params.codec_params));

	mtk_hdmi_audio_set_param(hdmi, &hdmi_params);

	return 0;
}

static int mtk_hdmi_audio_startup(struct device *dev, void *data)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	mtk_hdmi_audio_enable(hdmi);

	return 0;
}

static void mtk_hdmi_audio_shutdown(struct device *dev, void *data)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	mtk_hdmi_audio_disable(hdmi);
}

#if 0
static int
mtk_hdmi_audio_digital_mute(struct device *dev, void *data, bool enable)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	dev_dbg(dev, "%s(%d)\n", __func__, enable);

	if (enable)
		mtk_hdmi_hw_aud_mute(hdmi);
	else
		mtk_hdmi_hw_aud_unmute(hdmi);

	return 0;
}
#endif

static int mtk_hdmi_audio_get_eld(struct device *dev, void *data, uint8_t *buf, size_t len)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	memcpy(buf, hdmi->conn.eld, min(sizeof(hdmi->conn.eld), len));

	return 0;
}

static const struct hdmi_codec_ops mtk_hdmi_audio_codec_ops = {
	.hw_params = mtk_hdmi_audio_hw_params,
	.audio_startup = mtk_hdmi_audio_startup,
	.audio_shutdown = mtk_hdmi_audio_shutdown,
	//.digital_mute = mtk_hdmi_audio_digital_mute,
	.get_eld = mtk_hdmi_audio_get_eld,
};

static void mtk_hdmi_register_audio_driver(struct device *dev)
{
	struct hdmi_codec_pdata codec_data = {
		.ops = &mtk_hdmi_audio_codec_ops,
		.max_i2s_channels = 2,
		.i2s = 1,
	};
	struct platform_device *pdev;

	pdev = platform_device_register_data(dev, HDMI_CODEC_DRV_NAME,
					     PLATFORM_DEVID_AUTO, &codec_data,
					     sizeof(codec_data));
	if (IS_ERR(pdev))
		return;
#if 0
	DDPINFO("%s driver bound to HDMI\n", HDMI_CODEC_DRV_NAME);
#endif
}

static void mtk_hdmi_hpd_work_handle(struct work_struct *data)
{
	struct mtk_hdmi *hdmi = container_of(data, struct mtk_hdmi, hpd_work);
	unsigned long long starttime = sched_clock();
	bool hpd;

	if (abs(sched_clock() - starttime) > 1000000000ULL)
		HDMI_LOG("Handle time over 1s\n");

	hpd = mtk_hdmi_hpd_high(NULL);

	if (hdmi->hpd != hpd) {
		HDMI_LOG("hotplug event! old hpd = %d, new hpd = %d\n",
			hdmi->hpd, hpd);
		hdmi->hpd = hpd;
		mtk_hdmi_hpd_event(hpd, hdmi->dev);
	}

}

static int mtk_hdmi_init_workqueue(struct mtk_hdmi *hdmi)
{
	HDMI_FUNC();

	hdmi->hpd_wq = create_singlethread_workqueue("hdmitx_hpd_wq");
	if (!hdmi->hpd_wq) {
		HDMI_LOG("Failed to create dptx workqueue\n");
		return -ENOMEM;
	}

	INIT_WORK(&hdmi->hpd_work, mtk_hdmi_hpd_work_handle);
	return 0;
}

/* ----------------debugfs start----------------------- */


static char debug_buffer[2048] = {0};
static unsigned int temp_len, buf_offset;

#define HDMI_ATTR_SPRINTF(fmt, arg...)  \
do { \
	HDMI_LOG(fmt, ##arg); \
	if (buf_offset < (sizeof(debug_buffer) - 1)) { \
		temp_len = \
		sprintf(debug_buffer + buf_offset, fmt, ##arg);	 \
		buf_offset += temp_len; \
		debug_buffer[buf_offset] = 0;\
	} \
} while (0)

static void mtk_hdmi_freq_meter(struct mtk_hdmi *hdmi,
	unsigned int mode)
{
	unsigned int clockFreq;

	if (mode == 1) //pixel clock
		mtk_hdmi_mask(hdmi, 0x1a4, 0x0 << 5, 0x3 << 5);
	else if (mode == 2) //tmds clock
		mtk_hdmi_mask(hdmi, 0x1a4, 0x1 << 5, 0x3 << 5);
	else if (mode == 3) //i2s sck
		mtk_hdmi_mask(hdmi, 0x1a4, 0x2 << 5, 0x3 << 5);
	else //i2s lrck
		mtk_hdmi_mask(hdmi, 0x1a4, 0x3 << 5, 0x3 << 5);

	mtk_hdmi_write(hdmi, 0x1d4, 0x000007ff);
	mtk_hdmi_mask(hdmi, 0x1d0, 0x1 << 0, 0x1 << 0);

	udelay(40);

	clockFreq = mtk_hdmi_read(hdmi, 0x1d4);
	clockFreq = clockFreq >> 16;
	clockFreq = clockFreq * 24000 / 1024;

	if (mode == 1)
		HDMI_ATTR_SPRINTF("pixel clock freq meter is %d khz\n",
			clockFreq);
	else if (mode == 2)
		HDMI_ATTR_SPRINTF("tmds clock freq meter is %d khz\n",
			clockFreq);
	else if (mode == 3)
		HDMI_ATTR_SPRINTF("i2s Sck freq meter is %d khz\n",
			clockFreq);
	else
		HDMI_ATTR_SPRINTF("i2s LRck freq meter is %d khz\n",
			clockFreq);

	HDMI_LOG("clock freq meter is %d khz\n", clockFreq);
}

static unsigned int mtk_hdmi_set_txpll(
	struct mtk_hdmi *hdmi, char *str_p)
{
	unsigned long clkrate; // in HZ
	int ret;

	ret = kstrtol(str_p, 0, &clkrate);
	if (ret < 1) {
		HDMI_ATTR_SPRINTF("parameter error\n");
		return -EINVAL;
	}
	HDMI_ATTR_SPRINTF("set txpll rate: %ld HZ\n", clkrate);

	vTxSignalOnOff(hdmi->hdmi_phy_base, false);
	clk_disable_unprepare(hdmi->hdmi_phy_base->txpll);
	udelay(10);

	clk_set_rate(hdmi->hdmi_phy_base->txpll, clkrate);
	udelay(10);

	clk_prepare_enable(hdmi->hdmi_phy_base->txpll);
	vTxSignalOnOff(hdmi->hdmi_phy_base, true);

	mdelay(10);
	mtk_hdmi_freq_meter(hdmi, 2);//tmds clock
	return 0;
}

static unsigned int set_colordeep(struct mtk_hdmi *hdmi,
	char *str_p)
{
	unsigned int colorspace;
	unsigned int deepcolor;
	int ret;

	ret = sscanf(str_p, "0x%x/0x%x", &colorspace,
		&deepcolor);
	if (ret < 1) {
		HDMI_ATTR_SPRINTF("parameter error\n");
		return -EINVAL;
	}

	if (colorspace == HDMI_COLORSPACE_RGB) {
		hdmi->csp = HDMI_COLORSPACE_RGB;
		HDMI_ATTR_SPRINTF("HDMI_COLORSPACE_RGB\n");
	} else if (colorspace == HDMI_COLORSPACE_YUV422) {
		hdmi->csp = HDMI_COLORSPACE_YUV422;
		HDMI_ATTR_SPRINTF("HDMI_COLORSPACE_YUV422\n");
	} else if (colorspace == HDMI_COLORSPACE_YUV444) {
		hdmi->csp = HDMI_COLORSPACE_YUV444;
		HDMI_ATTR_SPRINTF("HDMI_COLORSPACE_YUV444\n");
	} else if (colorspace == HDMI_COLORSPACE_YUV420) {
		hdmi->csp = HDMI_COLORSPACE_YUV420;
		HDMI_ATTR_SPRINTF("HDMI_COLORSPACE_YUV420\n");
	} else {
		hdmi->csp = HDMI_COLORSPACE_RGB;
		HDMI_ATTR_SPRINTF("HDMI_COLORSPACE_RGB\n");
	}

	if (deepcolor == HDMI_8_BIT) {
		hdmi->color_depth = HDMI_8_BIT;
		HDMI_ATTR_SPRINTF("HDMI_8_BIT\n");
	} else if (deepcolor == HDMI_10_BIT) {
		hdmi->color_depth = HDMI_10_BIT;
		HDMI_ATTR_SPRINTF("HDMI_10_BIT\n");
	} else if (deepcolor == HDMI_12_BIT) {
		hdmi->color_depth = HDMI_12_BIT;
		HDMI_ATTR_SPRINTF("HDMI_12_BIT\n");
	} else if (deepcolor == HDMI_16_BIT) {
		hdmi->color_depth = HDMI_16_BIT;
		HDMI_ATTR_SPRINTF("HDMI_16_BIT\n");
	} else {
		hdmi->color_depth = HDMI_8_BIT;
		HDMI_ATTR_SPRINTF("HDMI_8_BIT\n");
	}
	return 0;
}

static unsigned int hdmi_read_reg(struct mtk_hdmi *hdmi, char *str_p)
{
	unsigned int u4Data;
	unsigned int u4Addr;
	int ret;

	if (strncmp(str_p, "help", 4) == 0) {
		HDMI_ATTR_SPRINTF("rg:0xPPPPPPPP\n");
		return 0;
	}

	ret = kstrtouint(str_p, 0, &u4Addr);

	if (u4Addr >= 0x1c300000)
		u4Data = mtk_hdmi_read(hdmi, u4Addr - 0x1c300000);
	else if (u4Addr >= 0x11d5f000)
		u4Data = mtk_hdmi_phy_read(hdmi->hdmi_phy_base,
			u4Addr - 0x11d5f000);
	else
		HDMI_ATTR_SPRINTF(
	"%s the address is over of the range: 0x%08x\n",
	__func__, u4Addr);

	HDMI_ATTR_SPRINTF("0x%08x = 0x%08x\n", u4Addr, u4Data);

	return 0;
}

static unsigned int hdmi_read_reg_range(struct mtk_hdmi *hdmi, char *str_p)
{
	unsigned int u4Data;
	unsigned int u4Addr;
	unsigned int u4Count;
	int ret;

	if (strncmp(str_p, "help", 4) == 0) {
		HDMI_ATTR_SPRINTF("rg:0xPPPPPPPP/0xPPP\n");
		return 0;
	}

	ret = sscanf(str_p, "0x%x/0x%x", &u4Addr, &u4Count);

	while (u4Count >= 4) {
		if (u4Addr >= 0x1c300000)
			u4Data = mtk_hdmi_read(hdmi, u4Addr - 0x1c300000);
		else if (u4Addr >= 0x11d5f000)
			u4Data = mtk_hdmi_phy_read(hdmi->hdmi_phy_base,
				u4Addr - 0x11d5f000);
		else
			HDMI_ATTR_SPRINTF(
	"%s the address is over of the range: 0x%08x\n",
	__func__, u4Addr);

		HDMI_ATTR_SPRINTF("0x%08x = 0x%08x\n", u4Addr, u4Data);
		udelay(2);
		u4Addr = u4Addr + 4;
		u4Count = u4Count - 4;
	}

	return 0;
}

static unsigned int hdmi_write_reg(struct mtk_hdmi *hdmi, char *str_p)
{
	unsigned int u4Data;
	unsigned int u4Addr;
	int ret;

	if (strncmp(str_p, "help", 4) == 0) {
		HDMI_ATTR_SPRINTF("wg:0xPPPPPPPP=0xPPPPPPPP\n");
		return 0;
	}

	ret = sscanf(str_p, "0x%x=0x%x", &u4Addr, &u4Data);

	if (u4Addr >= 0x1c300000)
		mtk_hdmi_write(hdmi, u4Addr - 0x1c300000, u4Data);
	else if (u4Addr >= 0x11d5f000)
		mtk_hdmi_phy_write(hdmi->hdmi_phy_base,
			u4Addr - 0x11d5f000, u4Data);
	else
		HDMI_ATTR_SPRINTF(
	"%s the address is over of the range: 0x%08x\n",
	__func__, u4Addr);

	HDMI_ATTR_SPRINTF("0x%08x = 0x%08x\n", u4Addr, u4Data);

	return 0;
}

/* 1 - 640x480@60Hz 4:3 */
struct drm_display_mode mode_640x480_60hz_4v3 = {
	DRM_MODE("640x480", DRM_MODE_TYPE_DRIVER, 25175, 640, 656,
	752, 800, 0, 480, 490, 492, 525, 0,
	 DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3,};
EXPORT_SYMBOL(mode_640x480_60hz_4v3);

/* 2 - 720x480@60Hz 4:3 */
struct drm_display_mode mode_720x480_60hz_4v3 = {
	DRM_MODE("720x480", DRM_MODE_TYPE_DRIVER, 27000, 720, 736,
	798, 858, 0, 480, 489, 495, 525, 0,
	DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3,};
EXPORT_SYMBOL(mode_720x480_60hz_4v3);

/* 18 - 720x576@50Hz 16:9 */
struct drm_display_mode mode_720x576_50hz_16v9 = {
	DRM_MODE("720x576", DRM_MODE_TYPE_DRIVER, 27000, 720, 732,
	796, 864, 0, 576, 581, 586, 625, 0,
	DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9,};
EXPORT_SYMBOL(mode_720x576_50hz_16v9);

/* 19 - 1280x720@50Hz 16:9 */
struct drm_display_mode mode_1280x720_50hz_16v9 = {
	DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1720,
	1760, 1980, 0, 720, 725, 730, 750, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9,};
EXPORT_SYMBOL(mode_1280x720_50hz_16v9);

/* 4 - 1280x720@60Hz 16:9 */
struct drm_display_mode mode_1280x720_60hz_16v9 = {
	DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1390,
	1430, 1650, 0, 720, 725, 730, 750, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9,};
EXPORT_SYMBOL(mode_1280x720_60hz_16v9);

/* 16 - 1920x1080@60Hz 16:9 */
struct drm_display_mode mode_1920x1080_60hz_16v9 = {
	DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2008,
	2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9,};
EXPORT_SYMBOL(mode_1920x1080_60hz_16v9);

/* 31 - 1920x1080@50Hz 16:9 */
struct drm_display_mode mode_1920x1080_50hz_16v9 = {
	DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2448,
	2492, 2640, 0, 1080, 1084, 1089, 1125, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9,};
EXPORT_SYMBOL(mode_1920x1080_50hz_16v9);

/* 32 - 1920x1080@24Hz 16:9 */
struct drm_display_mode mode_1920x1080_24hz_16v9 = {
	DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2558,
	2602, 2750, 0, 1080, 1084, 1089, 1125, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9,};
EXPORT_SYMBOL(mode_1920x1080_24hz_16v9);

/* 33 - 1920x1080@25Hz 16:9 */
struct drm_display_mode mode_1920x1080_25hz_16v9 = {
	DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2448,
	2492, 2640, 0, 1080, 1084, 1089, 1125, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9,};
EXPORT_SYMBOL(mode_1920x1080_25hz_16v9);

/* 34 - 1920x1080@30Hz 16:9 */
struct drm_display_mode mode_1920x1080_30hz_16v9 = {
	DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 74250, 1920, 2008,
	2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9,};
EXPORT_SYMBOL(mode_1920x1080_30hz_16v9);

/* 93 - 3840x2160@24Hz 16:9 */
struct drm_display_mode mode_3840x2160_24hz_16v9 = {
	DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 297000, 3840, 5116,
	5204, 5500, 0, 2160, 2168, 2178, 2250, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9,};
EXPORT_SYMBOL(mode_3840x2160_24hz_16v9);

/* 94 - 3840x2160@25Hz 16:9 */
struct drm_display_mode mode_3840x2160_25hz_16v9 = {
	DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 297000, 3840, 4896,
	4984, 5280, 0, 2160, 2168, 2178, 2250, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9,};
EXPORT_SYMBOL(mode_3840x2160_25hz_16v9);

/* 95 - 3840x2160@30Hz 16:9 */
struct drm_display_mode mode_3840x2160_30hz_16v9 = {
	DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 297000, 3840, 4016,
	4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9,};
EXPORT_SYMBOL(mode_3840x2160_30hz_16v9);

/* 96 - 3840x2160@50Hz 16:9 */
struct drm_display_mode mode_3840x2160_50hz_16v9 = {
	DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 594000, 3840, 4896,
	4984, 5280, 0, 2160, 2168, 2178, 2250, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9,};
EXPORT_SYMBOL(mode_3840x2160_50hz_16v9);

/* 97 - 3840x2160@60Hz 16:9 */
struct drm_display_mode mode_3840x2160_60hz_16v9 = {
	DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 594000, 3840, 4016,
	4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9,};
EXPORT_SYMBOL(mode_3840x2160_60hz_16v9);

/* 98 - 4096x2160@24Hz 256:135 */
struct drm_display_mode mode_4096x2160_24hz = {
	DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 297000, 4096, 5116,
	5204, 5500, 0, 2160, 2168, 2178, 2250, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_256_135,};
EXPORT_SYMBOL(mode_4096x2160_24hz);

/* 99 - 4096x2160@25Hz 256:135 */
struct drm_display_mode mode_4096x2160_25hz = {
	DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 297000, 4096, 5064,
	5152, 5280, 0, 2160, 2168, 2178, 2250, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_256_135,};
EXPORT_SYMBOL(mode_4096x2160_25hz);

/* 100 - 4096x2160@30Hz 256:135 */
struct drm_display_mode mode_4096x2160_30hz = {
	DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 297000, 4096, 4184,
	4272, 4400, 0, 2160, 2168, 2178, 2250, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_256_135,};
EXPORT_SYMBOL(mode_4096x2160_30hz);

/* 101 - 4096x2160@50Hz 256:135 */
struct drm_display_mode mode_4096x2160_50hz = {
	DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 594000, 4096, 5064,
	5152, 5280, 0, 2160, 2168, 2178, 2250, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_256_135,};
EXPORT_SYMBOL(mode_4096x2160_50hz);

/* 102 - 4096x2160@60Hz 256:135 */
struct drm_display_mode mode_4096x2160_60hz = {
	DRM_MODE("4096x2160", DRM_MODE_TYPE_DRIVER, 594000, 4096, 4184,
	4272, 4400, 0, 2160, 2168, 2178, 2250, 0,
	DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	.picture_aspect_ratio = HDMI_PICTURE_ASPECT_256_135,};
EXPORT_SYMBOL(mode_4096x2160_60hz);

static unsigned int set_abist_pattern(
	struct mtk_hdmi *hdmi, char *str_p)
{
	unsigned int ret;
	unsigned int mode_num, OnOff;
	unsigned char abist_format;
	struct drm_display_mode *mode;

	ret = sscanf(str_p, "0x%x/0x%x", &OnOff, &mode_num);

	switch (mode_num) {
	case 0x2:
		HDMI_ATTR_SPRINTF("0x2: 720*480P@60\n");
		mode = &mode_720x480_60hz_4v3;
		abist_format = 0x2;
		break;
	case 0x3:
		HDMI_ATTR_SPRINTF("0x3: 1280*720P@60\n");
		mode = &mode_1280x720_60hz_16v9;
		abist_format = 0x3;
		break;
	case 0x4:
		HDMI_ATTR_SPRINTF("0x4: 1280*720P@50\n");
		mode = &mode_1280x720_50hz_16v9;
		abist_format = 0xc;
		break;
	case 0x5:
		HDMI_ATTR_SPRINTF("0x5: 1920*1080P@60\n");
		mode = &mode_1920x1080_60hz_16v9;
		abist_format = 0xa;
		break;
	case 0x6:
		HDMI_ATTR_SPRINTF("0x6: 1920*1080P@50\n");
		mode = &mode_1920x1080_50hz_16v9;
		abist_format = 0x13;
		break;
	case 0x7:
		HDMI_ATTR_SPRINTF("0x7: 3840*2160P@30\n");
		mode = &mode_3840x2160_30hz_16v9;
		abist_format = 0x19;
		break;
	case 0x8:
		HDMI_ATTR_SPRINTF("0x8: 3840*2160P@50\n");
		mode = &mode_3840x2160_50hz_16v9;
		abist_format = 0x18;
		break;
	case 0x9:
		HDMI_ATTR_SPRINTF("0x9: 3840*2160P@60\n");
		mode = &mode_3840x2160_60hz_16v9;
		abist_format = 0x19;
		break;
	case 0xa:
		HDMI_ATTR_SPRINTF("0xa: 4096*2160P@30\n");
		mode = &mode_4096x2160_30hz;
		abist_format = 0x1a;
		break;
	default:
		HDMI_ATTR_SPRINTF("Wrong Mode!!\n");
		return 0;
	}

	//drm_bridge_mode_set(&hdmi->bridge, NULL, mode);
	mtk_hdmi_bridge_mode_set(&hdmi->bridge, NULL, mode);

	//drm_bridge_pre_enable(&hdmi->bridge);
	mtk_hdmi_bridge_pre_enable(&hdmi->bridge);

	//drm_bridge_enable(&hdmi->bridge);
	mtk_hdmi_bridge_enable(&hdmi->bridge);

	mtk_hdmi_mask(hdmi, TOP_CFG00, (abist_format << 16),
		ABIST_VIDEO_FORMAT_MASKBIT);
	mtk_hdmi_mask(hdmi, TOP_CFG00, (OnOff << 31), ABIST_ENABLE);


	return 0;
}

void set_dpi_pattern(struct mtk_hdmi *hdmi, char *str_p)
{
	const struct drm_encoder_helper_funcs *funcs;
	struct drm_bridge *bridge;
	struct drm_encoder *dpi_encoder;
	struct drm_display_mode *mode, *adjusted_mode;
	unsigned int OnOff, mode_num;
	int ret;

	ret = sscanf(str_p, "0x%x/0x%x", &OnOff, &mode_num);

	bridge = &hdmi->bridge;
	dpi_encoder = bridge->encoder;
	funcs = dpi_encoder->helper_private;

	switch (mode_num) {
	case 0x2:
		HDMI_ATTR_SPRINTF("0x2: 720*480P@60\n");
		mode = &mode_720x480_60hz_4v3;
		break;
	case 0x3:
		HDMI_ATTR_SPRINTF("0x3: 1280*720P@60\n");
		mode = &mode_1280x720_60hz_16v9;
		break;
	case 0x4:
		HDMI_ATTR_SPRINTF("0x4: 1280*720P@50\n");
		mode = &mode_1280x720_50hz_16v9;
		break;
	case 0x5:
		HDMI_ATTR_SPRINTF("0x5: 1920*1080P@60\n");
		mode = &mode_1920x1080_60hz_16v9;
		break;
	case 0x6:
		HDMI_ATTR_SPRINTF("0x6: 1920*1080P@50\n");
		mode = &mode_1920x1080_50hz_16v9;
		break;
	case 0x7:
		HDMI_ATTR_SPRINTF("0x7: 3840*2160P@30\n");
		mode = &mode_3840x2160_30hz_16v9;
		break;
	case 0x8:
		HDMI_ATTR_SPRINTF("0x8: 3840*2160P@50\n");
		mode = &mode_3840x2160_50hz_16v9;
		break;
	case 0x9:
		HDMI_ATTR_SPRINTF("0x9: 3840*2160P@60\n");
		mode = &mode_3840x2160_60hz_16v9;
		break;
	case 0xa:
		HDMI_ATTR_SPRINTF("0xa: 4096*2160P@30\n");
		mode = &mode_4096x2160_30hz;
		break;
	default:
		HDMI_ATTR_SPRINTF("Wrong Mode!!\n");
	}

	funcs->mode_set(dpi_encoder, NULL, mode);
	funcs->enable(dpi_encoder);
#if 0
	if (OnOff != 0)
		mtk_dpi_pattern_en(true);
	else
		mtk_dpi_pattern_en(false);
#endif

	//drm_bridge_mode_set(&hdmi->bridge, NULL, mode);
	mtk_hdmi_bridge_mode_set(&hdmi->bridge, NULL, mode);

	//drm_bridge_pre_enable(&hdmi->bridge);
	mtk_hdmi_bridge_pre_enable(&hdmi->bridge);

	//drm_bridge_enable(&hdmi->bridge);
	mtk_hdmi_bridge_enable(&hdmi->bridge);

	mtk_hdmi_mask(hdmi, TOP_CFG00, (0x0 << 31), ABIST_ENABLE);
}
static void process_dbg_cmd(char *opt)
{
	unsigned char en, ret;

	struct mtk_hdmi *hdmi;

	if (global_mtk_hdmi != NULL)
		hdmi = global_mtk_hdmi;
	else {
		HDMI_ATTR_SPRINTF("mtk_hdmi structure is NULL\n");
		HDMI_LOG("mtk_hdmi structure is NULL\n");
		return;
	}

	if (strncmp(opt, "settxpll:", 9) == 0)
		mtk_hdmi_set_txpll(hdmi, opt + 9);
	else if (strncmp(opt, "codeep:", 7) == 0)
		set_colordeep(hdmi, opt + 7);
	else if (strncmp(opt, "abist:", 6) == 0)
		set_abist_pattern(hdmi, opt + 6);
	else if (strncmp(opt, "dpi_pattern:", 12) == 0)
		set_dpi_pattern(hdmi, opt + 12);
#if 0
	else if (strncmp(opt, "color_bar_on", 12) == 0)
		mtk_dpi_pattern_en(true);
	else if (strncmp(opt, "color_bar_off", 13) == 0)
		mtk_dpi_pattern_en(false);
#endif
	else if (strncmp(opt, "audreset", 8) == 0) {
		mtk_hdmi_audio_reset(hdmi, true);
	} else if (strncmp(opt, "audpacket:", 10) == 0) {
		ret = sscanf(opt+10, "%hhx", &en);
		if (ret < 1)
			return;
		mtk_hdmi_hw_send_aud_packet(hdmi, !!en);

	} else if (strncmp(opt, "vidmute:", 8) == 0) {
		ret = sscanf(opt+8, "%hhx", &en);
		if (ret < 1)
			return;
		mtk_hdmi_hw_vid_black(hdmi, !!en);

	} else if (strncmp(opt, "audmute:", 8) == 0) {
		ret = sscanf(opt+8, "%hhx", &en);
		if (ret < 1)
			return;
		if (en)
			mtk_hdmi_hw_aud_mute(hdmi);
		else
			mtk_hdmi_hw_aud_unmute(hdmi);
	} else if (strncmp(opt, "getfreqmeter:", 13) == 0) {
		unsigned int mode;

		ret = sscanf(opt+13, "%x", &mode);
		if (ret < 1)
			return;
		mtk_hdmi_freq_meter(hdmi, mode);
	} else if (strncmp(opt, "rrg:", 4) == 0)
		hdmi_read_reg_range(hdmi, opt + 4);
	else if (strncmp(opt, "rg:", 3) == 0)
		hdmi_read_reg(hdmi, opt + 3);
	else if (strncmp(opt, "wg:", 3) == 0)
		hdmi_write_reg(hdmi, opt + 3);

}

struct dentry *hdmitx_debugfs;

static int debug_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t debug_read(struct file *file,
	char __user *ubuf, size_t count, loff_t *ppos)
{
	int n = 0;

	n = strlen(debug_buffer);
	debug_buffer[n++] = 0;

	return simple_read_from_buffer(ubuf, count, ppos, debug_buffer, n);
}

static ssize_t debug_write(struct file *file,
	const char __user *ubuf, size_t count, loff_t *ppos)
{
	const int debug_bufmax = sizeof(debug_buffer) - 1;
	ssize_t ret;

	ret = count;

	if (count > debug_bufmax)
		count = debug_bufmax;

	if (copy_from_user(&debug_buffer, ubuf, count))
		return -EFAULT;

	debug_buffer[count] = 0;
	buf_offset = 0;

	process_dbg_cmd(debug_buffer);

	return ret;
}

static const struct file_operations debug_fops = {
	.read = debug_read,
	.write = debug_write,
	.open = debug_open,
};

static int hdmitx_debug_init(void)
{
	HDMI_FUNC();
	hdmitx_debugfs = debugfs_create_file("hdmitx",
		S_IFREG | 0444, NULL, (void *)0, &debug_fops);

	if (IS_ERR(hdmitx_debugfs))
		return PTR_ERR(hdmitx_debugfs);


	HDMI_LOG("register hdmitx debugfs success\n");

	return 0;
}

void hdmitx_debug_uninit(void)
{
	debugfs_remove(hdmitx_debugfs);
}


/* ----------------debugfs end----------------------- */

static int mtk_drm_hdmi_probe(struct platform_device *pdev)
{
	struct mtk_hdmi *hdmi;
	struct device *dev = &pdev->dev;
	int ret;

	HDMI_LOG("probe start\n");

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->dev = dev;

	global_mtk_hdmi = hdmi;
	if (global_hdmi_phy != NULL)
		hdmi->hdmi_phy_base = global_hdmi_phy;

	ret = mtk_hdmi_dt_parse_pdata(hdmi, pdev);
	if (ret)
		return ret;

	hdmi->phy = devm_phy_get(dev, "hdmi");
	if (IS_ERR(hdmi->phy)) {
		ret = PTR_ERR(hdmi->phy);
		HDMI_LOG("Failed to get HDMI PHY: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, hdmi);

	ret = mtk_hdmi_output_init(hdmi);
	if (ret) {
		HDMI_LOG("Failed to initialize hdmi output\n");
		return ret;
	}

	mtk_hdmi_register_audio_driver(dev);

/* register audio extcon for WiredAccessoryManager start */
	hdmi_extcon = devm_extcon_dev_allocate(&pdev->dev, hdmi_cable);
	if (IS_ERR(hdmi_extcon)) {
		pr_info("Couldn't allocate HDMI extcon device\n");
		return PTR_ERR(hdmi_extcon);
	}

	hdmi_extcon->dev.init_name = "HDMI_audio_extcon";

	ret = devm_extcon_dev_register(&pdev->dev, hdmi_extcon);
	if (ret) {
		pr_info("failed to register HDMI extcon: %d\n", ret);
		return ret;
	}
/* register audio extcon for WiredAccessoryManager end */



/* register uevent device */
	hdmi_switch_data.name = "hdmi";
	hdmi_switch_data.index = 0;
	hdmi_switch_data.state = 0;
	ret = hdmitx_uevent_dev_register(&hdmi_switch_data);

	hdmires_switch_data.name = "res_hdmi";
	hdmires_switch_data.index = 0;
	hdmires_switch_data.state = 0;
	ret = hdmitx_uevent_dev_register(&hdmires_switch_data);
/* register uevent device */


/* create hdmi debugfs */
	hdmitx_debug_init();

	mtk_hdmi_init_workqueue(hdmi);


	hdmi->bridge.funcs = &mtk_hdmi_bridge_funcs;
	hdmi->bridge.of_node = pdev->dev.of_node;
	drm_bridge_add(&hdmi->bridge);

	ret = mtk_hdmi_clk_enable_audio(hdmi);
	if (ret) {
		dev_err(dev, "Failed to enable audio clocks: %d\n", ret);
		goto err_bridge_remove;
	}

	/*disable all tx irq*/
	mtk_hdmi_disable_all_int(hdmi);
	/*clear all tx irq*/
	mtk_hdmi_clr_all_int_status(hdmi);

	hdmi->hdmi_irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	/*can.zeng todo, where to free_irq*/
	if (request_irq(hdmi->hdmi_irq, mtk_hdmi_htplg_isr,
		IRQF_TRIGGER_HIGH, "hdmiirq", NULL) < 0)
		HDMI_LOG("request hdmi interrupt failed.\n");
	else
		HDMI_LOG("request hdmi interrupt success\n");


	/*can.zeng todo
	 *only enable hpd irq for bring up,
	 *8696 enable hpd and hdcp2x re-auth irq.
	 *enable hdcp2x irq after bring up
	 */
	mtk_hdmi_enable_hpd_pord_irq(hdmi, true);

	/* enable clk for DPI read, write access */
	//mtk_hdmi_txpll_clk_init(hdmi);

	set_abist_pattern(hdmi, "0x1/0x5");

	HDMI_LOG("probe success\n");
	return 0;

err_bridge_remove:
	drm_bridge_remove(&hdmi->bridge);
	return ret;
}

static int mtk_drm_hdmi_remove(struct platform_device *pdev)
{
	struct mtk_hdmi *hdmi = platform_get_drvdata(pdev);

	drm_bridge_remove(&hdmi->bridge);
	mtk_hdmi_clk_disable_audio(hdmi);

	hdmitx_debug_uninit();

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mtk_hdmi_suspend(struct device *dev)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	mtk_hdmi_clk_disable_audio(hdmi);
	dev_dbg(dev, "hdmi suspend success!\n");
	return 0;
}

static int mtk_hdmi_resume(struct device *dev)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);
	int ret = 0;

	ret = mtk_hdmi_clk_enable_audio(hdmi);
	if (ret) {
		dev_err(dev, "hdmi resume failed!\n");
		return ret;
	}

	dev_dbg(dev, "hdmi resume success!\n");
	return 0;
}
#endif
static SIMPLE_DEV_PM_OPS(mtk_hdmi_pm_ops,
			 mtk_hdmi_suspend, mtk_hdmi_resume);

static const struct of_device_id mtk_drm_hdmi_of_ids[] = {
	{ .compatible = "mediatek,mt8195-hdmi", },
	{}
};

static struct platform_driver mtk_hdmi_driver = {
	.probe = mtk_drm_hdmi_probe,
	.remove = mtk_drm_hdmi_remove,
	.driver = {
		.name = "mediatek-drm-hdmi",
		.of_match_table = mtk_drm_hdmi_of_ids,
		.pm = &mtk_hdmi_pm_ops,
	},
};

static struct platform_driver * const mtk_hdmi_drivers[] = {
	&mtk_hdmi_phy_driver,
	&mtk_hdmi_ddc_driver,
#if 0
	&mtk_cec_driver,
#endif
	&mtk_hdmi_driver,
};

static int __init mtk_hdmitx_init(void)
{
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(mtk_hdmi_drivers); i++) {
		ret = platform_driver_register(mtk_hdmi_drivers[i]);
		if (ret < 0) {
#if 0
			DDPPR_ERR("Failed to register %s driver: %d\n",
			       mtk_hdmi_drivers[i]->driver.name, ret);
#endif
			goto err;
		}
	}

	return 0;

err:
	while (--i >= 0)
		platform_driver_unregister(mtk_hdmi_drivers[i]);

	return ret;
}

static void __exit mtk_hdmitx_exit(void)
{
	int i;

	for (i = ARRAY_SIZE(mtk_hdmi_drivers) - 1; i >= 0; i--)
		platform_driver_unregister(mtk_hdmi_drivers[i]);
}

module_init(mtk_hdmitx_init);
module_exit(mtk_hdmitx_exit);

MODULE_AUTHOR("Can Zeng <can.zeng@mediatek.com>");
MODULE_DESCRIPTION("MediaTek HDMI Driver");
MODULE_LICENSE("GPL v2");
