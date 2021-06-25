// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Jie Qiu <jie.qiu@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <video/videomode.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_of.h>
#include <drm/drm_simple_kms_helper.h>

#include "mtk_disp_drv.h"
#include "mtk_drm_ddp_comp.h"

#define DPINTF_EN			0x00
#define EN				BIT(0)

#define DPINTF_RET			0x04
#define RST				BIT(0)
#define RST_SEL				BIT(16)

#define DPINTF_INTEN		0x08
#define INT_VSYNC_EN			BIT(0)
#define INT_VDE_EN			BIT(1)
#define INT_UNDERFLOW_EN		BIT(2)
#define INT_TARGET_LINE_EN		BIT(3)

#define DPINTF_INTSTA		0x0C
#define INT_VSYNC_STA			BIT(0)
#define INT_VDE_STA			BIT(1)
#define INT_UNDERFLOW_STA		BIT(2)
#define INT_TARGET_LINE_STA		BIT(3)

#define DPINTF_CON			0x10
#define BG_ENABLE			BIT(0)
#define INTL_EN				BIT(2)
#define TDFP_EN				BIT(3)
#define VS_LODD_EN			BIT(16)
#define VS_LEVEN_EN			BIT(17)
#define VS_RODD_EN			BIT(18)
#define VS_REVEN			BIT(19)
#define FAKE_DE_LODD			BIT(20)
#define FAKE_DE_LEVEN			BIT(21)
#define FAKE_DE_RODD			BIT(22)
#define FAKE_DE_REVEN			BIT(23)
#define YUV422_EN			BIT(24)
#define CLPF_EN				BIT(25)
#define MATRIX_EN			BIT(26)
#define INTERNAL_CG_EN			BIT(27)
#define LOWPOWER_EN			BIT(28)
#define INPUT_2P_EN			BIT(29)
#define EXT_VSYNC_EN			BIT(30)

#define DPINTF_OUTPUT_SETTING	0x14
#define PIXEL_SWAP			BIT(0)
#define CH_SWAP				BIT(1)
#define CH_SWAP_MASK			(0x7 << 1)
#define SWAP_RGB			(0x00 << 1)
#define SWAP_GBR			(0x01 << 1)
#define SWAP_BRG			(0x02 << 1)
#define SWAP_RBG			(0x03 << 1)
#define SWAP_GRB			(0x04 << 1)
#define SWAP_BGR			(0x05 << 1)
#define B_MASK				BIT(4)
#define G_MASK				BIT(5)
#define R_MASK				BIT(6)
#define DE_MASK				BIT(8)
#define HS_MASK				BIT(9)
#define VS_MASK				BIT(10)
#define HSYNC_POL			BIT(13)
#define VSYNC_POL			BIT(14)
#define OUT_BIT				BIT(16)
#define OUT_BIT_MASK			(0x3 << 18)
#define OUT_BIT_8			(0x00 << 18)
#define OUT_BIT_10			(0x01 << 18)
#define OUT_BIT_12			(0x02 << 18)
#define OUT_BIT_16			(0x03 << 18)

#define DPINTF_SIZE		0x18
#define HSIZE				0
#define HSIZE_MASK			(0xffff << 0)
#define VSIZE				16
#define VSIZE_MASK			(0xffff << 16)

#define DPINTF_TGEN_HWIDTH		0x20
#define HPW				0
#define HPW_MASK			(0xffff << 0)

#define DPINTF_TGEN_HPORCH		0x24
#define HBP				0
#define HBP_MASK			(0xffff << 0)
#define HFP				16
#define HFP_MASK			(0xffff << 16)

#define DPINTF_TGEN_VWIDTH		0x28
#define VSYNC_WIDTH_SHIFT		0
#define VSYNC_WIDTH_MASK		(0xffff << 0)
#define VSYNC_HALF_LINE_SHIFT		16
#define VSYNC_HALF_LINE_MASK		BIT(16)


#define DPINTF_TGEN_VPORCH		0x2C
#define VSYNC_BACK_PORCH_SHIFT		0
#define VSYNC_BACK_PORCH_MASK		(0xffff << 0)
#define VSYNC_FRONT_PORCH_SHIFT		16
#define VSYNC_FRONT_PORCH_MASK		(0xffff << 16)

#define DPINTF_BG_HCNTL		0x30
#define BG_RIGHT			(0xffff << 0)
#define BG_LEFT				(0xffff << 16)

#define DPINTF_BG_VCNTL		0x34
#define BG_BOT				(0xffff << 0)
#define BG_TOP				(0xffff << 16)

#define DPINTF_BG_COLOR		0x38
#define BG_B				(0x3ff << 0)
#define BG_G				(0x3ff << 10)
#define BG_R				(0x3ff << 20)

#define DPINTF_FIFO_CTL		0x3C
#define FIFO_VALID_SET			(0x1F << 0)
#define FIFO_RST_SEL			BIT(8)
#define FIFO_RD_MASK			BIT(12)

#define DPINTF_STATUS		0x40
#define VCOUNTER			(0x3ffff << 0)
#define DPINTF_BUSY			BIT(24)
#define FIELD				BIT(28)
#define TDLR				BIT(29)

#define DPINTF_TGEN_VWIDTH_LEVEN	0x68
#define DPINTF_TGEN_VPORCH_LEVEN	0x6C
#define DPINTF_TGEN_VWIDTH_RODD	0x70
#define DPINTF_TGEN_VPORCH_RODD	0x74
#define DPINTF_TGEN_VWIDTH_REVEN	0x78
#define DPINTF_TGEN_VPORCH_REVEN	0x7C

#define DPINTF_CLPF_SETTING	0x94
#define CLPF_TYPE			(0x3 << 0)
#define ROUND_EN			BIT(4)

#define DPINTF_Y_LIMIT		0x98
#define Y_LIMINT_BOT			0
#define Y_LIMINT_BOT_MASK		(0xFFF << 0)
#define Y_LIMINT_TOP			16
#define Y_LIMINT_TOP_MASK		(0xFFF << 16)

#define DPINTF_C_LIMIT		0x9C
#define C_LIMIT_BOT			0
#define C_LIMIT_BOT_MASK		(0xFFF << 0)
#define C_LIMIT_TOP			16
#define C_LIMIT_TOP_MASK		(0xFFF << 16)

#define DPINTF_YUV422_SETTING	0xA0
#define UV_SWAP				BIT(0)
#define CR_DELSEL			BIT(4)
#define CB_DELSEL			BIT(5)
#define Y_DELSEL			BIT(6)
#define DE_DELSEL			BIT(7)

#define DPINTF_MATRIX_SET		0xB4
#define INT_MATRIX_SEL_MASK	0x1f
#define RGB_TO_JPEG			0x00
#define RGB_TO_FULL709			0x01
#define RGB_TO_BT601			0x02
#define RGB_TO_BT709			0x03
#define JPEG_TO_RGB			0x04
#define FULL709_TO_RGB			0x05
#define BT601_TO_RGB			0x06
#define BT709_TO_RGB			0x07
#define JPEG_TO_BT601			0x08
#define JPEG_TO_BT709			0x09
#define BT601_TO_JPEG			0xA
#define BT709_TO_JPEG			0xB
#define BT709_TO_BT601			0xC
#define BT601_TO_BT709			0xD
#define JPEG_TO_CERGB			0x14
#define FULL709_TO_CERGB		0x15
#define BT601_TO_CERGB			0x16
#define BT709_TO_CERGB			0x17
#define RGB_TO_CERGB			0x1C

#define MATRIX_BIT_MASK	(0x3 << 8)
#define EXT_MATRIX_EN	BIT(12)

enum mtk_dpintf_out_bit_num {
	MTK_DPINTF_OUT_BIT_NUM_8BITS,
	MTK_DPINTF_OUT_BIT_NUM_10BITS,
	MTK_DPINTF_OUT_BIT_NUM_12BITS,
	MTK_DPINTF_OUT_BIT_NUM_16BITS
};

enum mtk_dpintf_out_yc_map {
	MTK_DPINTF_OUT_YC_MAP_RGB,
	MTK_DPINTF_OUT_YC_MAP_CYCY,
	MTK_DPINTF_OUT_YC_MAP_YCYC,
	MTK_DPINTF_OUT_YC_MAP_CY,
	MTK_DPINTF_OUT_YC_MAP_YC
};

enum mtk_dpintf_out_channel_swap {
	MTK_DPINTF_OUT_CHANNEL_SWAP_RGB,
	MTK_DPINTF_OUT_CHANNEL_SWAP_GBR,
	MTK_DPINTF_OUT_CHANNEL_SWAP_BRG,
	MTK_DPINTF_OUT_CHANNEL_SWAP_RBG,
	MTK_DPINTF_OUT_CHANNEL_SWAP_GRB,
	MTK_DPINTF_OUT_CHANNEL_SWAP_BGR
};

enum mtk_dpintf_out_color_format {
	MTK_DPINTF_COLOR_FORMAT_RGB,
	MTK_DPINTF_COLOR_FORMAT_RGB_FULL,
	MTK_DPINTF_COLOR_FORMAT_YCBCR_444,
	MTK_DPINTF_COLOR_FORMAT_YCBCR_422,
	MTK_DPINTF_COLOR_FORMAT_XV_YCC,
	MTK_DPINTF_COLOR_FORMAT_YCBCR_444_FULL,
	MTK_DPINTF_COLOR_FORMAT_YCBCR_422_FULL
};

enum TVDPLL_CLK {
	TVDPLL_PLL = 0,
	TVDPLL_D2 = 1,
	TVDPLL_D4 = 2,
	TVDPLL_D8 = 3,
	TVDPLL_D16 = 4,
};

struct mtk_dpintf {
	struct drm_encoder encoder;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_connector *connector;
	void __iomem *regs;
	struct device *dev;
	struct clk *hf_fmm_ck;
	struct clk *hf_fdp_ck;
	struct clk *pclk;
	struct clk *pclk_src[5];
	int irq;
	struct drm_display_mode mode;
	const struct mtk_dpintf_conf *conf;
	enum mtk_dpintf_out_color_format color_format;
	enum mtk_dpintf_out_yc_map yc_map;
	enum mtk_dpintf_out_bit_num bit_num;
	enum mtk_dpintf_out_channel_swap channel_swap;
	int refcount;
};

static inline struct mtk_dpintf *bridge_to_dpintf(struct drm_bridge *b)
{
	return container_of(b, struct mtk_dpintf, bridge);
}

enum mtk_dpintf_polarity {
	MTK_DPINTF_POLARITY_RISING,
	MTK_DPINTF_POLARITY_FALLING,
};

struct mtk_dpintf_polarities {
	enum mtk_dpintf_polarity de_pol;
	enum mtk_dpintf_polarity ck_pol;
	enum mtk_dpintf_polarity hsync_pol;
	enum mtk_dpintf_polarity vsync_pol;
};

struct mtk_dpintf_sync_param {
	u32 sync_width;
	u32 front_porch;
	u32 back_porch;
	bool shift_half_line;
};

struct mtk_dpintf_yc_limit {
	u16 y_top;
	u16 y_bottom;
	u16 c_top;
	u16 c_bottom;
};

struct mtk_dpintf_conf {
	unsigned int (*cal_factor)(int clock);
	u32 reg_h_fre_con;
	bool edge_sel_en;
};

static void mtk_dpintf_mask(struct mtk_dpintf *dpintf, u32 offset, u32 val, u32 mask)
{
	u32 tmp = readl(dpintf->regs + offset) & ~mask;

	tmp |= (val & mask);
	writel(tmp, dpintf->regs + offset);
}

static void mtk_dpintf_sw_reset(struct mtk_dpintf *dpintf, bool reset)
{
	mtk_dpintf_mask(dpintf, DPINTF_RET, reset ? RST : 0, RST);
}

static void mtk_dpintf_enable(struct mtk_dpintf *dpintf)
{
	mtk_dpintf_mask(dpintf, DPINTF_EN, EN, EN);
}

static void mtk_dpintf_disable(struct mtk_dpintf *dpintf)
{
	mtk_dpintf_mask(dpintf, DPINTF_EN, 0, EN);
}

static void mtk_dpintf_config_hsync(struct mtk_dpintf *dpintf,
				 struct mtk_dpintf_sync_param *sync)
{
	mtk_dpintf_mask(dpintf, DPINTF_TGEN_HWIDTH,
		     sync->sync_width << HPW, HPW_MASK);
	mtk_dpintf_mask(dpintf, DPINTF_TGEN_HPORCH,
		     sync->back_porch << HBP, HBP_MASK);
	mtk_dpintf_mask(dpintf, DPINTF_TGEN_HPORCH, sync->front_porch << HFP,
		     HFP_MASK);
}

static void mtk_dpintf_config_vsync(struct mtk_dpintf *dpintf,
				 struct mtk_dpintf_sync_param *sync,
				 u32 width_addr, u32 porch_addr)
{
	mtk_dpintf_mask(dpintf, width_addr,
		     sync->sync_width << VSYNC_WIDTH_SHIFT,
		     VSYNC_WIDTH_MASK);
	mtk_dpintf_mask(dpintf, width_addr,
		     sync->shift_half_line << VSYNC_HALF_LINE_SHIFT,
		     VSYNC_HALF_LINE_MASK);
	mtk_dpintf_mask(dpintf, porch_addr,
		     sync->back_porch << VSYNC_BACK_PORCH_SHIFT,
		     VSYNC_BACK_PORCH_MASK);
	mtk_dpintf_mask(dpintf, porch_addr,
		     sync->front_porch << VSYNC_FRONT_PORCH_SHIFT,
		     VSYNC_FRONT_PORCH_MASK);
}

static void mtk_dpintf_config_vsync_lodd(struct mtk_dpintf *dpintf,
				      struct mtk_dpintf_sync_param *sync)
{
	mtk_dpintf_config_vsync(dpintf, sync, DPINTF_TGEN_VWIDTH, DPINTF_TGEN_VPORCH);
}

static void mtk_dpintf_config_vsync_leven(struct mtk_dpintf *dpintf,
				       struct mtk_dpintf_sync_param *sync)
{
	mtk_dpintf_config_vsync(dpintf, sync, DPINTF_TGEN_VWIDTH_LEVEN,
			     DPINTF_TGEN_VPORCH_LEVEN);
}

static void mtk_dpintf_config_vsync_rodd(struct mtk_dpintf *dpintf,
				      struct mtk_dpintf_sync_param *sync)
{
	mtk_dpintf_config_vsync(dpintf, sync, DPINTF_TGEN_VWIDTH_RODD,
			     DPINTF_TGEN_VPORCH_RODD);
}

static void mtk_dpintf_config_vsync_reven(struct mtk_dpintf *dpintf,
				       struct mtk_dpintf_sync_param *sync)
{
	mtk_dpintf_config_vsync(dpintf, sync, DPINTF_TGEN_VWIDTH_REVEN,
			     DPINTF_TGEN_VPORCH_REVEN);
}

static void mtk_dpintf_config_pol(struct mtk_dpintf *dpintf,
			       struct mtk_dpintf_polarities *dpintf_pol)
{
	unsigned int pol;

	pol = (dpintf_pol->hsync_pol == MTK_DPINTF_POLARITY_RISING ? 0 : HSYNC_POL) |
	      (dpintf_pol->vsync_pol == MTK_DPINTF_POLARITY_RISING ? 0 : VSYNC_POL);
	mtk_dpintf_mask(dpintf, DPINTF_OUTPUT_SETTING, pol,
		     HSYNC_POL | VSYNC_POL);
}

static void mtk_dpintf_config_3d(struct mtk_dpintf *dpintf, bool en_3d)
{
	mtk_dpintf_mask(dpintf, DPINTF_CON, en_3d ? TDFP_EN : 0, TDFP_EN);
}

static void mtk_dpintf_config_interface(struct mtk_dpintf *dpintf, bool inter)
{
	mtk_dpintf_mask(dpintf, DPINTF_CON, inter ? INTL_EN : 0, INTL_EN);
}

static void mtk_dpintf_config_fb_size(struct mtk_dpintf *dpintf, u32 width, u32 height)
{
	mtk_dpintf_mask(dpintf, DPINTF_SIZE, width << HSIZE, HSIZE_MASK);
	mtk_dpintf_mask(dpintf, DPINTF_SIZE, height << VSIZE, VSIZE_MASK);
}

static void mtk_dpintf_config_channel_limit(struct mtk_dpintf *dpintf,
					 struct mtk_dpintf_yc_limit *limit)
{
	mtk_dpintf_mask(dpintf, DPINTF_Y_LIMIT, limit->y_bottom << Y_LIMINT_BOT,
		     Y_LIMINT_BOT_MASK);
	mtk_dpintf_mask(dpintf, DPINTF_Y_LIMIT, limit->y_top << Y_LIMINT_TOP,
		     Y_LIMINT_TOP_MASK);
	mtk_dpintf_mask(dpintf, DPINTF_C_LIMIT, limit->c_bottom << C_LIMIT_BOT,
		     C_LIMIT_BOT_MASK);
	mtk_dpintf_mask(dpintf, DPINTF_C_LIMIT, limit->c_top << C_LIMIT_TOP,
		     C_LIMIT_TOP_MASK);
}

static void mtk_dpintf_config_bit_num(struct mtk_dpintf *dpintf,
				   enum mtk_dpintf_out_bit_num num)
{
	u32 val;

	switch (num) {
	case MTK_DPINTF_OUT_BIT_NUM_8BITS:
		val = OUT_BIT_8;
		break;
	case MTK_DPINTF_OUT_BIT_NUM_10BITS:
		val = OUT_BIT_10;
		break;
	case MTK_DPINTF_OUT_BIT_NUM_12BITS:
		val = OUT_BIT_12;
		break;
	case MTK_DPINTF_OUT_BIT_NUM_16BITS:
		val = OUT_BIT_16;
		break;
	default:
		val = OUT_BIT_8;
		break;
	}
	mtk_dpintf_mask(dpintf, DPINTF_OUTPUT_SETTING, val,
		     OUT_BIT_MASK);
}

static void mtk_dpintf_config_channel_swap(struct mtk_dpintf *dpintf,
					enum mtk_dpintf_out_channel_swap swap)
{
	u32 val;

	switch (swap) {
	case MTK_DPINTF_OUT_CHANNEL_SWAP_RGB:
		val = SWAP_RGB;
		break;
	case MTK_DPINTF_OUT_CHANNEL_SWAP_GBR:
		val = SWAP_GBR;
		break;
	case MTK_DPINTF_OUT_CHANNEL_SWAP_BRG:
		val = SWAP_BRG;
		break;
	case MTK_DPINTF_OUT_CHANNEL_SWAP_RBG:
		val = SWAP_RBG;
		break;
	case MTK_DPINTF_OUT_CHANNEL_SWAP_GRB:
		val = SWAP_GRB;
		break;
	case MTK_DPINTF_OUT_CHANNEL_SWAP_BGR:
		val = SWAP_BGR;
		break;
	default:
		val = SWAP_RGB;
		break;
	}

	mtk_dpintf_mask(dpintf, DPINTF_OUTPUT_SETTING, val, CH_SWAP_MASK);
}

static void mtk_dpintf_config_yuv422_enable(struct mtk_dpintf *dpintf, bool enable)
{
	mtk_dpintf_mask(dpintf, DPINTF_CON, enable ? YUV422_EN : 0, YUV422_EN);
}

static void mtk_dpintf_config_color_format(struct mtk_dpintf *dpintf,
					enum mtk_dpintf_out_color_format format)
{
	if ((format == MTK_DPINTF_COLOR_FORMAT_YCBCR_444) ||
	    (format == MTK_DPINTF_COLOR_FORMAT_YCBCR_444_FULL)) {
		mtk_dpintf_config_yuv422_enable(dpintf, false);
		mtk_dpintf_config_channel_swap(dpintf, MTK_DPINTF_OUT_CHANNEL_SWAP_BGR);
	} else if ((format == MTK_DPINTF_COLOR_FORMAT_YCBCR_422) ||
		   (format == MTK_DPINTF_COLOR_FORMAT_YCBCR_422_FULL)) {
		mtk_dpintf_config_yuv422_enable(dpintf, true);
		mtk_dpintf_config_channel_swap(dpintf, MTK_DPINTF_OUT_CHANNEL_SWAP_RGB);
	} else {
		mtk_dpintf_config_yuv422_enable(dpintf, false);
		mtk_dpintf_config_channel_swap(dpintf, MTK_DPINTF_OUT_CHANNEL_SWAP_RGB);
	}
}

static void mtk_dpintf_power_off(struct mtk_dpintf *dpintf)
{
	if (WARN_ON(dpintf->refcount == 0))
		return;

	if (--dpintf->refcount != 0)
		return;

	mtk_dpintf_disable(dpintf);
	clk_disable_unprepare(dpintf->hf_fdp_ck);
	clk_disable_unprepare(dpintf->hf_fmm_ck);
	clk_disable_unprepare(dpintf->pclk);
	clk_disable_unprepare(dpintf->pclk_src[TVDPLL_PLL]);
}

static int mtk_dpintf_power_on(struct mtk_dpintf *dpintf)
{
	int ret;
	struct videomode vm = { 0 };
	unsigned int clksrc = TVDPLL_D2;
	unsigned long pll_rate;

	pr_info("%s +", __func__);

	drm_display_mode_to_videomode(&dpintf->mode, &vm);

	if (++dpintf->refcount != 1) {

		pr_info("%s -", __func__);
		return 0;
	}

	if (vm.pixelclock < 70000000)
		clksrc = TVDPLL_D16;
	else if (vm.pixelclock < 200000000)
		clksrc = TVDPLL_D8;
	else
		clksrc = TVDPLL_D4;

	pll_rate = vm.pixelclock * (1 << clksrc);


	clk_set_rate(dpintf->pclk_src[TVDPLL_PLL], pll_rate / 4);
	clk_prepare_enable(dpintf->pclk_src[TVDPLL_PLL]);
	clk_prepare_enable(dpintf->pclk);
	clk_set_parent(dpintf->pclk, dpintf->pclk_src[clksrc]);

	ret = clk_prepare_enable(dpintf->hf_fmm_ck);
	if (ret < 0)
		dev_err(dpintf->dev, "%s Failed to enable hf_fmm_ck clock: %d\n",
			__func__, ret);
	ret = clk_prepare_enable(dpintf->hf_fdp_ck);
	if (ret < 0)
		dev_err(dpintf->dev, "%s Failed to enable hf_fdp_ck clock: %d\n", __func__, ret);

	dev_info(dpintf->dev, "%s :dpintf->pclk_src[TVDPLL_PLL] =  %ld\n", __func__, clk_get_rate(dpintf->pclk_src[TVDPLL_PLL]));
	dev_info(dpintf->dev, "%s :dpintf->pclk =  %ld\n", __func__, clk_get_rate(dpintf->pclk));
	dev_info(dpintf->dev, "%s :dpintf->hf_fmm_ck =  %ld\n", __func__, clk_get_rate(dpintf->hf_fmm_ck));
	dev_info(dpintf->dev, "%s :dpintf->hf_fdp_ck =  %ld\n", __func__, clk_get_rate(dpintf->hf_fdp_ck));

	pr_info("%s -", __func__);
	return 0;

//err_pixel:
	clk_disable_unprepare(dpintf->hf_fdp_ck);
	clk_disable_unprepare(dpintf->hf_fmm_ck);
	clk_disable_unprepare(dpintf->pclk);
	clk_disable_unprepare(dpintf->pclk_src[TVDPLL_PLL]);
//err_refcount:
	dpintf->refcount--;
	return ret;
}

static int mtk_dpintf_set_display_mode(struct mtk_dpintf *dpintf,
				    struct drm_display_mode *mode)
{
	struct mtk_dpintf_yc_limit limit;
	struct mtk_dpintf_polarities dpintf_pol;
	struct mtk_dpintf_sync_param hsync;
	struct mtk_dpintf_sync_param vsync_lodd = { 0 };
	struct mtk_dpintf_sync_param vsync_leven = { 0 };
	struct mtk_dpintf_sync_param vsync_rodd = { 0 };
	struct mtk_dpintf_sync_param vsync_reven = { 0 };
	struct videomode vm = { 0 };

	/* let pll_rate can fix the valid range of tvdpll (1G~2GHz) */
	drm_display_mode_to_videomode(mode, &vm);

	limit.c_bottom = 0x0000;
	limit.c_top = 0xFFF;
	limit.y_bottom = 0x0000;
	limit.y_top = 0xFFF;

	dpintf_pol.ck_pol = MTK_DPINTF_POLARITY_FALLING;
	dpintf_pol.de_pol = MTK_DPINTF_POLARITY_RISING;
	dpintf_pol.hsync_pol = vm.flags & DISPLAY_FLAGS_HSYNC_HIGH ?
			    MTK_DPINTF_POLARITY_FALLING : MTK_DPINTF_POLARITY_RISING;
	dpintf_pol.vsync_pol = vm.flags & DISPLAY_FLAGS_VSYNC_HIGH ?
			    MTK_DPINTF_POLARITY_FALLING : MTK_DPINTF_POLARITY_RISING;
	hsync.sync_width = vm.hsync_len / 4;
	hsync.back_porch = vm.hback_porch / 4;
	hsync.front_porch = vm.hfront_porch / 4;
	hsync.shift_half_line = false;
	vsync_lodd.sync_width = vm.vsync_len;
	vsync_lodd.back_porch = vm.vback_porch;
	vsync_lodd.front_porch = vm.vfront_porch;
	vsync_lodd.shift_half_line = false;

	if (vm.flags & DISPLAY_FLAGS_INTERLACED &&
	    mode->flags & DRM_MODE_FLAG_3D_MASK) {
		vsync_leven = vsync_lodd;
		vsync_rodd = vsync_lodd;
		vsync_reven = vsync_lodd;
		vsync_leven.shift_half_line = true;
		vsync_reven.shift_half_line = true;
	} else if (vm.flags & DISPLAY_FLAGS_INTERLACED &&
		   !(mode->flags & DRM_MODE_FLAG_3D_MASK)) {
		vsync_leven = vsync_lodd;
		vsync_leven.shift_half_line = true;
	} else if (!(vm.flags & DISPLAY_FLAGS_INTERLACED) &&
		   mode->flags & DRM_MODE_FLAG_3D_MASK) {
		vsync_rodd = vsync_lodd;
	}
	mtk_dpintf_sw_reset(dpintf, true);
	mtk_dpintf_config_pol(dpintf, &dpintf_pol);

	mtk_dpintf_config_hsync(dpintf, &hsync);
	mtk_dpintf_config_vsync_lodd(dpintf, &vsync_lodd);
	mtk_dpintf_config_vsync_rodd(dpintf, &vsync_rodd);
	mtk_dpintf_config_vsync_leven(dpintf, &vsync_leven);
	mtk_dpintf_config_vsync_reven(dpintf, &vsync_reven);

	mtk_dpintf_config_3d(dpintf, !!(mode->flags & DRM_MODE_FLAG_3D_MASK));
	mtk_dpintf_config_interface(dpintf, !!(vm.flags &
					 DISPLAY_FLAGS_INTERLACED));
	if (vm.flags & DISPLAY_FLAGS_INTERLACED)
		mtk_dpintf_config_fb_size(dpintf, vm.hactive, vm.vactive >> 1);
	else
		mtk_dpintf_config_fb_size(dpintf, vm.hactive, vm.vactive);

	mtk_dpintf_config_channel_limit(dpintf, &limit);
	mtk_dpintf_config_bit_num(dpintf, dpintf->bit_num);
	mtk_dpintf_config_channel_swap(dpintf, dpintf->channel_swap);
	mtk_dpintf_config_color_format(dpintf, dpintf->color_format);

	mtk_dpintf_mask(dpintf, DPINTF_CON, INPUT_2P_EN, INPUT_2P_EN);
	mtk_dpintf_sw_reset(dpintf, false);

	mtk_dpintf_enable(dpintf);

	return 0;
}

static int mtk_dpintf_bridge_attach(struct drm_bridge *bridge,
				 enum drm_bridge_attach_flags flags)
{
	struct mtk_dpintf *dpintf = bridge_to_dpintf(bridge);

	return drm_bridge_attach(bridge->encoder, dpintf->next_bridge,
				 &dpintf->bridge, DRM_BRIDGE_ATTACH_NO_CONNECTOR);
}

static void mtk_dpintf_bridge_mode_set(struct drm_bridge *bridge,
				const struct drm_display_mode *mode,
				const struct drm_display_mode *adjusted_mode)
{
	struct mtk_dpintf *dpintf = bridge_to_dpintf(bridge);

	drm_mode_copy(&dpintf->mode, adjusted_mode);
}

static void mtk_dpintf_bridge_disable(struct drm_bridge *bridge)
{
	struct mtk_dpintf *dpintf = bridge_to_dpintf(bridge);

	mtk_dpintf_power_off(dpintf);
}

static void mtk_dpintf_bridge_enable(struct drm_bridge *bridge)
{
	struct mtk_dpintf *dpintf = bridge_to_dpintf(bridge);

	pr_info("%s +", __func__);

	mtk_dpintf_power_on(dpintf);
	mtk_dpintf_set_display_mode(dpintf, &dpintf->mode);
	pr_info("%s -", __func__);
}

static const struct drm_bridge_funcs mtk_dpintf_bridge_funcs = {
	.attach = mtk_dpintf_bridge_attach,
	.mode_set = mtk_dpintf_bridge_mode_set,
	.disable = mtk_dpintf_bridge_disable,
	.enable = mtk_dpintf_bridge_enable,
};

void mtk_dpintf_start(struct device *dev)
{
	struct mtk_dpintf *dpintf = dev_get_drvdata(dev);

	pr_info("%s +", __func__);

	mtk_dpintf_power_on(dpintf);

	pr_info("%s -", __func__);
}

void mtk_dpintf_stop(struct device *dev)
{
	struct mtk_dpintf *dpintf = dev_get_drvdata(dev);

	mtk_dpintf_power_off(dpintf);
}

int mtk_dpintf_encoder_index(struct device *dev)
{
	struct mtk_dpintf *dpintf = dev_get_drvdata(dev);
	int encoder_index = drm_encoder_index(&dpintf->encoder);

	dev_dbg(dev, "encoder index:%d", encoder_index);
	return encoder_index;
}

static int mtk_dpintf_bind(struct device *dev, struct device *master, void *data)
{
	struct mtk_dpintf *dpintf = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	int ret;

	ret = drm_simple_encoder_init(drm_dev, &dpintf->encoder,
				      DRM_MODE_ENCODER_TMDS);
	if (ret) {
		dev_err(dev, "Failed to initialize decoder: %d\n", ret);
		return ret;
	}

	dpintf->encoder.possible_crtcs = mtk_drm_find_possible_crtc_by_comp(drm_dev, dpintf->dev);

	ret = drm_bridge_attach(&dpintf->encoder, &dpintf->bridge, NULL, DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret) {
		dev_err(dev, "Failed to attach bridge: %d\n", ret);
		goto err_cleanup;
	}

	dpintf->connector = drm_bridge_connector_init(drm_dev, &dpintf->encoder);
	if (IS_ERR(dpintf->connector)) {
		dev_err(dev, "Unable to create bridge connector\n");
		ret = PTR_ERR(dpintf->connector);
		goto err_cleanup;
	}
	drm_connector_attach_encoder(dpintf->connector, &dpintf->encoder);

	dpintf->bit_num = MTK_DPINTF_OUT_BIT_NUM_8BITS;
	dpintf->channel_swap = MTK_DPINTF_OUT_CHANNEL_SWAP_RGB;
	dpintf->yc_map = MTK_DPINTF_OUT_YC_MAP_RGB;
	dpintf->color_format = MTK_DPINTF_COLOR_FORMAT_RGB;

	return 0;

err_cleanup:
	drm_encoder_cleanup(&dpintf->encoder);
	return ret;
}

static void mtk_dpintf_unbind(struct device *dev, struct device *master,
			   void *data)
{
	struct mtk_dpintf *dpintf = dev_get_drvdata(dev);

	drm_encoder_cleanup(&dpintf->encoder);
}

static const struct component_ops mtk_dpintf_component_ops = {
	.bind = mtk_dpintf_bind,
	.unbind = mtk_dpintf_unbind,
};

static int mtk_dpintf_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_dpintf *dpintf;
	struct resource *mem;
	int ret;

	dpintf = devm_kzalloc(dev, sizeof(*dpintf), GFP_KERNEL);
	if (!dpintf)
		return -ENOMEM;

	dpintf->dev = dev;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dpintf->regs = devm_ioremap_resource(dev, mem);
	if (IS_ERR(dpintf->regs)) {
		ret = PTR_ERR(dpintf->regs);
		dev_err(dev, "Failed to ioremap mem resource: %d\n", ret);
		return ret;
	}

	/* Get dp intf clk
	 * Input pixel clock(hf_fmm_ck) frequency needs to be > hf_fdp_ck * 4
	 * Otherwise FIFO will underflow
	 */
	dpintf->hf_fmm_ck = devm_clk_get(dev, "hf_fmm_ck");
	if (IS_ERR(dpintf->hf_fmm_ck)) {
		ret = PTR_ERR(dpintf->hf_fmm_ck);
		dev_err(dev, "Failed to get hf_fmm_ck clock: %d\n", ret);
		return ret;
	}
	dpintf->hf_fdp_ck = devm_clk_get(dev, "hf_fdp_ck");
	if (IS_ERR(dpintf->hf_fdp_ck)) {
		ret = PTR_ERR(dpintf->hf_fdp_ck);
		dev_err(dev, "Failed to get hf_fdp_ck clock: %d\n", ret);
		return ret;
	}

	dpintf->pclk = devm_clk_get(dev, "MUX_DP");
	dpintf->pclk_src[0] = devm_clk_get(dev, "DPI_CK");
	dpintf->pclk_src[1] = devm_clk_get(dev, "TVDPLL_D2");
	dpintf->pclk_src[2] = devm_clk_get(dev, "TVDPLL_D4");
	dpintf->pclk_src[3] = devm_clk_get(dev, "TVDPLL_D8");
	dpintf->pclk_src[4] = devm_clk_get(dev, "TVDPLL_D16");

	dpintf->irq = platform_get_irq(pdev, 0);
	if (dpintf->irq <= 0) {
		dev_err(dev, "Failed to get irq: %d\n", dpintf->irq);
		return -EINVAL;
	}

	ret = drm_of_find_panel_or_bridge(dev->of_node, 0, 0,
					  NULL, &dpintf->next_bridge);
	if (ret)
		return ret;

	dev_info(dev, "Found bridge node: %pOF\n", dpintf->next_bridge->of_node);

	platform_set_drvdata(pdev, dpintf);

	dpintf->bridge.funcs = &mtk_dpintf_bridge_funcs;
	dpintf->bridge.of_node = dev->of_node;
	dpintf->bridge.type = DRM_MODE_CONNECTOR_DPI;

	drm_bridge_add(&dpintf->bridge);

	ret = component_add(dev, &mtk_dpintf_component_ops);
	if (ret) {
		drm_bridge_remove(&dpintf->bridge);
		dev_err(dev, "Failed to add component: %d\n", ret);
		return ret;
	}

	return 0;
}

static int mtk_dpintf_remove(struct platform_device *pdev)
{
	struct mtk_dpintf *dpintf = platform_get_drvdata(pdev);

	component_del(&pdev->dev, &mtk_dpintf_component_ops);
	drm_bridge_remove(&dpintf->bridge);

	return 0;
}

static const struct of_device_id mtk_dpintf_of_ids[] = {
	{ .compatible = "mediatek,mt8195-dpintf" },
	{ },
};

struct platform_driver mtk_dpintf_driver = {
	.probe = mtk_dpintf_probe,
	.remove = mtk_dpintf_remove,
	.driver = {
		.name = "mediatek-dpintf",
		.of_match_table = mtk_dpintf_of_ids,
	},
};

