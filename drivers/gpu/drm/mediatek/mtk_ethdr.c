// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */
#include <drm/drm_fourcc.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/soc/mediatek/mtk-cmdq.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>

#include "mtk_drm_crtc.h"
#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_drv.h"
#include "mtk_ethdr.h"

#define	ETHDR_CON_ALPHA		0xff

#define VDO1_CONFIG_SW0_RST_B 0x1D0

#define VDO1_CONFIG_SW1_RST_B 0x1D4
	#define HDR_ASYNC_RESET_BIT (BIT(19)|BIT(20)|BIT(21)|BIT(22)|BIT(23))
#define VDO1_CONFIG_HDR_BE_ASYNC_CFG_WD 0xE70
#define ROUND(x, y) ((x/y) + (((x%y) >= (y/2)) ? 1 : 0))
#define MIN(x, y) (((x) <= (y)) ? (x) : (y))

struct mtk_ethdr_data {
	bool tbd; //no use
};

struct mtk_ethdr_comp {
	struct clk *clk;
	void __iomem *regs;
	resource_size_t regs_pa;
	int irq;
	struct device *dev;
	enum mtk_ddp_comp_id id;
	struct cmdq_client_reg cmdq_base;
};


struct mtk_ethdr {
	struct device *dev;
	void __iomem *top_regs;
	resource_size_t top_regs_pa;
	struct cmdq_client_reg top_cmdq_base;
	struct mtk_ethdr_comp ethdr_comp[ETHDR_ID_MAX];
	const struct mtk_ethdr_data *data;
	struct clk *async_clk[ETHDR_ASYNC_ID_MAX];
	struct clk *top_clk;
};

static struct mtk_plane_state _stPlaneState[4] = {0};
static struct mtk_ethdr *g_ethdr;

static const char * const ethdr_comp_str[] = {
	"ETHDR_DISP_MIXER",
	"ETHDR_VDO_FE0",
	"ETHDR_VDO_FE1",
	"ETHDR_GFX_FE0",
	"ETHDR_GFX_FE1",
	"ETHDR_VDO_BE",
	"ETHDR_ADL_DS",
	"ETHDR_ID_MAX"
};

static const char * const ethdr_async_clk_str[] = {
	"hdr_vdo_fe0_async",
	"hdr_vdo_fe1_async",
	"hdr_gfx_fe0_async",
	"hdr_gfx_fe1_async",
	"hdr_vdo_be_async",
	"hdr_async_MAX"
};

void mtk_ethdr_layer_on(unsigned int idx, struct cmdq_pkt *cmdq_pkt)
{

	dev_dbg(g_ethdr->dev, "%s+ idx:%d", __func__, idx);

	if (idx < 4) {
		mtk_ddp_write_mask(cmdq_pkt, 1<<idx,
			&g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
			g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].regs, 0x24, 1<<idx);
	}
	dev_dbg(g_ethdr->dev, "%s-", __func__);
}

void mtk_ethdr_layer_off(unsigned int idx, struct cmdq_pkt *cmdq_pkt)
{

	dev_dbg(g_ethdr->dev, "%s+ idx:%d", __func__, idx);

	switch (idx) {
	case 0:
	{
		mtk_ddp_write_mask(cmdq_pkt, 0,
			&g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
			g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].regs, 0x30, ~0);
	}
	break;
	case 1:
	{
		mtk_ddp_write_mask(cmdq_pkt, 0,
			&g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
			g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].regs, 0x48, ~0);
	}
	break;
	case 2:
	{
		mtk_ddp_write_mask(cmdq_pkt, 0,
			&g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
			g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].regs, 0x60, ~0);
	}
	break;
	case 3:
	{
		mtk_ddp_write_mask(cmdq_pkt, 0,
			&g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
			g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].regs, 0x78, ~0);
	}
	break;
	default:
	{
		dev_dbg(g_ethdr->dev, "%s Wrong layer ID\n", __func__);
	}
	break;
	}

	dev_dbg(g_ethdr->dev, "%s-", __func__);
}

void mtk_ethdr_layer_config(unsigned int idx,
				 struct mtk_plane_state *state,
				 struct cmdq_pkt *cmdq_pkt)
{
	unsigned int alpha = 0xFF;
	unsigned int alpha_con = ETHDR_CON_ALPHA;
	unsigned int fmt = 0;

	memcpy(&_stPlaneState[idx], state, sizeof(struct mtk_plane_state));

	fmt = state->pending.format;

	if (state->base.fb && state->base.fb->format->has_alpha)
		alpha_con = true;

	if (alpha == 0xFF &&
		(fmt == DRM_FORMAT_RGBX8888 || fmt == DRM_FORMAT_BGRX8888 ||
		fmt == DRM_FORMAT_XRGB8888 || fmt == DRM_FORMAT_XBGR8888))
		alpha_con = 0;

	switch (idx) {
	case 0:
	{
		mtk_ddp_write_mask(cmdq_pkt,
			(state->pending.height)<<16|state->pending.width,
			&g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
			g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].regs,
			0x30, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(state->pending.y)<<16|state->pending.x,
			&g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
			g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].regs,
			0x34, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			alpha_con<<8|alpha,
			&g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
			g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].regs,
			0x28, 0x1FF);
	}
	break;
	case 1:
	{
		mtk_ddp_write_mask(cmdq_pkt,
			(state->pending.height)<<16|state->pending.width,
			&g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
			g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].regs,
			0x48, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(state->pending.y)<<16|state->pending.x,
			&g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
			g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].regs,
			0x4C, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			alpha_con<<8|alpha,
			&g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
			g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].regs,
			0x40, 0x1FF);

	}
	break;
	case 2:
	{
		mtk_ddp_write_mask(cmdq_pkt,
			(state->pending.height)<<16|state->pending.width,
			&g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
			g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].regs,
			0x60, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(state->pending.y)<<16|state->pending.x,
			&g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
			g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].regs,
			0x64, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			alpha_con<<8|alpha,
			&g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
			g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].regs,
			0x58, 0x1FF);
	}
	break;
	case 3:
	{
		mtk_ddp_write_mask(cmdq_pkt,
			(state->pending.height)<<16|state->pending.width,
			&g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
			g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].regs,
			0x78, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(state->pending.y)<<16|state->pending.x,
			&g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
			g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].regs,
			0x7C, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			alpha_con<<8|alpha,
			&g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
			g_ethdr->ethdr_comp[ETHDR_DISP_MIXER].regs,
			0x70, 0x1FF);
	}
	break;
	default:
	{
		dev_dbg(g_ethdr->dev, "%s Wrong layer ID\n", __func__);
	}
	break;
	}

}


void mtk_ethdr_config(struct device *dev, unsigned int w,
		    unsigned int h, unsigned int vrefresh,
		    unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_ethdr *priv = dev_get_drvdata(dev);
	pr_info("%s\n", __func__);

	mtk_ddp_write_mask(cmdq_pkt, (h<<16 | w/2), &priv->top_cmdq_base,
			priv->top_regs, VDO1_CONFIG_HDR_BE_ASYNC_CFG_WD, ~0);

	mtk_ddp_write_mask(cmdq_pkt, 0, &priv->top_cmdq_base,
			priv->top_regs, 0xD4C, ~0);

	/*mixer layer setting*/
	mtk_ddp_write_mask(cmdq_pkt, h<<16|w,
		&priv->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
		priv->ethdr_comp[ETHDR_DISP_MIXER].regs,
		0x18, ~0);
}

void mtk_ethdr_start(struct device *dev)
{
	struct mtk_ethdr *priv = dev_get_drvdata(dev);

	dev_info(dev, "%s\n", __func__);

	/*bypass ETHDR*/
	mtk_ddp_write_mask(NULL, 0xfd, &priv->ethdr_comp[ETHDR_VDO_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_VDO_FE0].regs, 0x804, ~0);
	mtk_ddp_write_mask(NULL, 0x80, &priv->ethdr_comp[ETHDR_VDO_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_VDO_FE0].regs, 0x9EC, ~0);
	mtk_ddp_write_mask(NULL, 0x12E, &priv->ethdr_comp[ETHDR_VDO_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_VDO_FE0].regs, 0x81C, ~0);
	mtk_ddp_write_mask(NULL, 0x0, &priv->ethdr_comp[ETHDR_VDO_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_VDO_FE0].regs, 0x618, ~0);
	mtk_ddp_write_mask(NULL, 0x2, &priv->ethdr_comp[ETHDR_VDO_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_VDO_FE0].regs, 0x61C, ~0);
	mtk_ddp_write_mask(NULL, 0x8001, &priv->ethdr_comp[ETHDR_VDO_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_VDO_FE0].regs, 0x6D0, ~0);
	mtk_ddp_write_mask(NULL, 0x8000, &priv->ethdr_comp[ETHDR_VDO_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_VDO_FE0].regs, 0x634, ~0);

	mtk_ddp_write_mask(NULL, 0xfd, &priv->ethdr_comp[ETHDR_VDO_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_VDO_FE1].regs, 0x804, ~0);
	mtk_ddp_write_mask(NULL, 0x80, &priv->ethdr_comp[ETHDR_VDO_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_VDO_FE1].regs, 0x9EC, ~0);
	mtk_ddp_write_mask(NULL, 0x12E, &priv->ethdr_comp[ETHDR_VDO_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_VDO_FE1].regs, 0x81C, ~0);
	mtk_ddp_write_mask(NULL, 0x0, &priv->ethdr_comp[ETHDR_VDO_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_VDO_FE1].regs, 0x618, ~0);
	mtk_ddp_write_mask(NULL, 0x2, &priv->ethdr_comp[ETHDR_VDO_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_VDO_FE1].regs, 0x61C, ~0);
	mtk_ddp_write_mask(NULL, 0x8001, &priv->ethdr_comp[ETHDR_VDO_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_VDO_FE1].regs, 0x6D0, ~0);
	mtk_ddp_write_mask(NULL, 0x8000, &priv->ethdr_comp[ETHDR_VDO_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_VDO_FE1].regs, 0x634, ~0);

	mtk_ddp_write_mask(NULL, 0x8001, &priv->ethdr_comp[ETHDR_GFX_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE0].regs, 0x100, ~0);
	mtk_ddp_write_mask(NULL, 0xE030, &priv->ethdr_comp[ETHDR_GFX_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE0].regs, 0x12c, ~0);
	mtk_ddp_write_mask(NULL, 0x1C0, &priv->ethdr_comp[ETHDR_GFX_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE0].regs, 0x134, ~0);
	mtk_ddp_write_mask(NULL, 0x1E69, &priv->ethdr_comp[ETHDR_GFX_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE0].regs, 0x138, ~0);
	mtk_ddp_write_mask(NULL, 0x1fd7, &priv->ethdr_comp[ETHDR_GFX_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE0].regs, 0x13C, ~0);
	mtk_ddp_write_mask(NULL, 0xba, &priv->ethdr_comp[ETHDR_GFX_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE0].regs, 0x140, ~0);
	mtk_ddp_write_mask(NULL, 0x275, &priv->ethdr_comp[ETHDR_GFX_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE0].regs, 0x144, ~0);
	mtk_ddp_write_mask(NULL, 0x3f, &priv->ethdr_comp[ETHDR_GFX_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE0].regs, 0x148, ~0);
	mtk_ddp_write_mask(NULL, 0x1f99, &priv->ethdr_comp[ETHDR_GFX_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE0].regs, 0x14c, ~0);
	mtk_ddp_write_mask(NULL, 0x1ea6, &priv->ethdr_comp[ETHDR_GFX_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE0].regs, 0x150, ~0);
	mtk_ddp_write_mask(NULL, 0x1c2, &priv->ethdr_comp[ETHDR_GFX_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE0].regs, 0x154, ~0);
	mtk_ddp_write_mask(NULL, 0xfd, &priv->ethdr_comp[ETHDR_GFX_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE0].regs, 0x204, ~0);
	mtk_ddp_write_mask(NULL, 0x80, &priv->ethdr_comp[ETHDR_GFX_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE0].regs, 0x3ec, ~0);
	mtk_ddp_write_mask(NULL, 0x20, &priv->ethdr_comp[ETHDR_GFX_FE0].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE0].regs, 0x21c, ~0);

	mtk_ddp_write_mask(NULL, 0x8001, &priv->ethdr_comp[ETHDR_GFX_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE1].regs, 0x100, ~0);
	mtk_ddp_write_mask(NULL, 0xE030, &priv->ethdr_comp[ETHDR_GFX_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE1].regs, 0x12c, ~0);
	mtk_ddp_write_mask(NULL, 0x1C0, &priv->ethdr_comp[ETHDR_GFX_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE1].regs, 0x134, ~0);
	mtk_ddp_write_mask(NULL, 0x1E69, &priv->ethdr_comp[ETHDR_GFX_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE1].regs, 0x138, ~0);
	mtk_ddp_write_mask(NULL, 0x1fd7, &priv->ethdr_comp[ETHDR_GFX_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE1].regs, 0x13C, ~0);
	mtk_ddp_write_mask(NULL, 0xba, &priv->ethdr_comp[ETHDR_GFX_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE1].regs, 0x140, ~0);
	mtk_ddp_write_mask(NULL, 0x275, &priv->ethdr_comp[ETHDR_GFX_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE1].regs, 0x144, ~0);
	mtk_ddp_write_mask(NULL, 0x3f, &priv->ethdr_comp[ETHDR_GFX_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE1].regs, 0x148, ~0);
	mtk_ddp_write_mask(NULL, 0x1f99, &priv->ethdr_comp[ETHDR_GFX_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE1].regs, 0x14c, ~0);
	mtk_ddp_write_mask(NULL, 0x1ea6, &priv->ethdr_comp[ETHDR_GFX_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE1].regs, 0x150, ~0);
	mtk_ddp_write_mask(NULL, 0x1c2, &priv->ethdr_comp[ETHDR_GFX_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE1].regs, 0x154, ~0);
	mtk_ddp_write_mask(NULL, 0xfd, &priv->ethdr_comp[ETHDR_GFX_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE1].regs, 0x204, ~0);
	mtk_ddp_write_mask(NULL, 0x80, &priv->ethdr_comp[ETHDR_GFX_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE1].regs, 0x3ec, ~0);
	mtk_ddp_write_mask(NULL, 0x20, &priv->ethdr_comp[ETHDR_GFX_FE1].cmdq_base,
		priv->ethdr_comp[ETHDR_GFX_FE1].regs, 0x21c, ~0);

	mtk_ddp_write_mask(NULL, 0x7e, &priv->ethdr_comp[ETHDR_VDO_BE].cmdq_base,
		priv->ethdr_comp[ETHDR_VDO_BE].regs, 0x204, ~0);
	mtk_ddp_write_mask(NULL, 0x00, &priv->ethdr_comp[ETHDR_VDO_BE].cmdq_base,
		priv->ethdr_comp[ETHDR_VDO_BE].regs, 0x320, ~0);
	mtk_ddp_write_mask(NULL, 0x01, &priv->ethdr_comp[ETHDR_VDO_BE].cmdq_base,
		priv->ethdr_comp[ETHDR_VDO_BE].regs, 0x3C8, ~0);

	mtk_ddp_write_mask(NULL, 0xffffffff,
		&priv->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
		priv->ethdr_comp[ETHDR_DISP_MIXER].regs, 0x120, ~0);
	mtk_ddp_write_mask(NULL, 0xffffffff,
		&priv->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
		priv->ethdr_comp[ETHDR_DISP_MIXER].regs, 0x124, ~0);
	mtk_ddp_write_mask(NULL, 0x00000888,
		&priv->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
		priv->ethdr_comp[ETHDR_DISP_MIXER].regs, 0x1C, ~0);
	mtk_ddp_write_mask(NULL, 0x000021ff,
		&priv->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
		priv->ethdr_comp[ETHDR_DISP_MIXER].regs, 0x28, ~0);
	mtk_ddp_write_mask(NULL, 0x000021ff,
		&priv->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
		priv->ethdr_comp[ETHDR_DISP_MIXER].regs, 0x40, ~0);
	mtk_ddp_write_mask(NULL, 0x000021ff,
		&priv->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
		priv->ethdr_comp[ETHDR_DISP_MIXER].regs, 0x58, ~0);
	mtk_ddp_write_mask(NULL, 0x000021ff,
		&priv->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
		priv->ethdr_comp[ETHDR_DISP_MIXER].regs, 0x70, ~0);
	mtk_ddp_write_mask(NULL, 0x0,
		&priv->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
		priv->ethdr_comp[ETHDR_DISP_MIXER].regs, 0x30, ~0);
	mtk_ddp_write_mask(NULL, 0x0fa50001,
		&priv->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
		priv->ethdr_comp[ETHDR_DISP_MIXER].regs, 0x24, ~0);
	mtk_ddp_write_mask(NULL, 0xFF000000,
		&priv->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
		priv->ethdr_comp[ETHDR_DISP_MIXER].regs, 0x20, ~0);
	mtk_ddp_write_mask(NULL, 0x00000001,
		&priv->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
		priv->ethdr_comp[ETHDR_DISP_MIXER].regs, 0xC, ~0);
}

void mtk_ethdr_stop(struct device *dev)
{
	struct mtk_ethdr *priv = dev_get_drvdata(dev);

	dev_info(dev, "%s\n", __func__);

	mtk_ddp_write_mask(NULL, 0,
		&priv->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
		priv->ethdr_comp[ETHDR_DISP_MIXER].regs, 0xC, ~0);

	mtk_ddp_write_mask(NULL, 1,
		&priv->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
		priv->ethdr_comp[ETHDR_DISP_MIXER].regs, 0x14, ~0);

	mtk_ddp_write_mask(NULL, 0, &priv->top_cmdq_base, priv->top_regs,
		VDO1_CONFIG_SW1_RST_B, HDR_ASYNC_RESET_BIT);

	mtk_ddp_write_mask(NULL, 0, &priv->top_cmdq_base, priv->top_regs,
		VDO1_CONFIG_SW0_RST_B, BIT(29));

	mtk_ddp_write_mask(NULL, 0,
		&priv->ethdr_comp[ETHDR_DISP_MIXER].cmdq_base,
		priv->ethdr_comp[ETHDR_DISP_MIXER].regs, 0x14, ~0);

	mtk_ddp_write_mask(NULL, HDR_ASYNC_RESET_BIT,
		&priv->top_cmdq_base, priv->top_regs,
		VDO1_CONFIG_SW1_RST_B, HDR_ASYNC_RESET_BIT);
	mtk_ddp_write_mask(NULL, BIT(29), &priv->top_cmdq_base, priv->top_regs,
		VDO1_CONFIG_SW0_RST_B, BIT(29));
}

int mtk_ethdr_clk_enable(struct device *dev)
{
	int i, ret;
	struct mtk_ethdr *priv = dev_get_drvdata(dev);

	dev_info(dev, "%s\n", __func__);
	pr_info("%s\n", __func__);

	pm_runtime_get_sync(dev);

	if (priv->top_clk != NULL) {
		ret = clk_prepare_enable(priv->top_clk);
		if (ret)
			dev_err(dev, "top_clk prepare enable failed\n");
	}

	for (i = 0; i < ETHDR_ASYNC_ID_MAX; i++) {
		if (priv->async_clk[i] != NULL) {
			ret = clk_prepare_enable(priv->async_clk[i]);
			if (ret)
				dev_err(dev, "async_clk[%d] prepare enable failed:\n", i);
		}
	}

	for (i = 0; i < ETHDR_ID_MAX; i++) {
		if (priv->ethdr_comp[i].clk != NULL) {
			ret = clk_prepare_enable(priv->ethdr_comp[i].clk);
			if (ret)
				dev_err(dev, "ethdr_comp[%d].clk prepare enable failed\n", i);
		}
	}

	return ret;
}

void mtk_ethdr_clk_disable(struct device *dev)
{
	struct mtk_ethdr *priv = dev_get_drvdata(dev);
	int i;

	dev_info(dev, "%s\n", __func__);
	for (i = 0; i < ETHDR_ASYNC_ID_MAX; i++)
		clk_disable_unprepare(priv->async_clk[i]);

	for (i = 0; i < ETHDR_ID_MAX; i++)
		clk_disable_unprepare(priv->ethdr_comp[i].clk);

	clk_disable_unprepare(priv->top_clk);

	pm_runtime_put(dev);
}

static int mtk_ethdr_bind(struct device *dev, struct device *master,
				void *data)
{
	dev_info(dev, "%s\n", __func__);
	return 0;
}

static void mtk_ethdr_unbind(struct device *dev, struct device *master,
	void *data)
{

}

static const struct component_ops mtk_ethdr_component_ops = {
	.bind	= mtk_ethdr_bind,
	.unbind = mtk_ethdr_unbind,
};

static int mtk_ethdr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_ethdr *priv;
	enum mtk_ddp_comp_id comp_id;
	int ret;
	int i;
	struct resource res;

	pr_info("%s+\n", __func__);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	priv->dev = dev;

	comp_id = mtk_ddp_comp_get_id(dev->of_node, MTK_DISP_ETHDR);
	if ((int)comp_id < 0) {
		dev_dbg(dev, "Failed to identify by alias: %d\n", comp_id);
		return comp_id;
	}

	/*Get Top Reg*/
	if (of_address_to_resource(dev->of_node, ETHDR_ID_MAX, &res) != 0) {
		dev_notice(dev, "No Top resource\n");
		return -EINVAL;
	}
	priv->top_regs_pa = res.start;
	priv->top_regs = of_iomap(dev->of_node, ETHDR_ID_MAX);
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->top_cmdq_base, ETHDR_ID_MAX);
	if (ret)
		dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif

	/* get top clk*/
	priv->top_clk = devm_clk_get(dev, "ethdr_top");
	if (IS_ERR(priv->top_clk)) {
		priv->top_clk = NULL;
		dev_notice(dev,
			"get ethdr top clock %d fail!\n");
		return PTR_ERR(priv->top_clk);
	}

	//set private comp info
	for (i = 0; i < ETHDR_ID_MAX; i++) {
		priv->ethdr_comp[i].id = DDP_COMPONENT_ETHDR;
		priv->ethdr_comp[i].dev = dev;

		/* get the clk for each pseudo ovl comp in the device node */
		priv->ethdr_comp[i].clk = of_clk_get(dev->of_node, i);
		if (IS_ERR(priv->ethdr_comp[i].clk)) {
			priv->ethdr_comp[i].clk = NULL;
			dev_notice(dev,
				"comp:%d get priviate comp %s clock %d fail!\n",
				comp_id, ethdr_comp_str[i]);
			return PTR_ERR(priv->ethdr_comp[i].clk);
		}

		if (of_address_to_resource(dev->of_node, i, &res) != 0) {
			dev_notice(dev, "Missing reg in for component %s\n",
			ethdr_comp_str[i]);
			return -EINVAL;
		}
		priv->ethdr_comp[i].regs_pa = res.start;
		priv->ethdr_comp[i].regs = of_iomap(dev->of_node, i);
		priv->ethdr_comp[i].irq = of_irq_get(dev->of_node, i);
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
		ret = cmdq_dev_get_client_reg(dev, &priv->ethdr_comp[i].cmdq_base, i);
		if (ret)
			dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif
		dev_info(dev, "[DRM]regs_pa:0x%lx, regs:0x%x, node:%s\n",
			(unsigned long)priv->ethdr_comp[i].regs_pa,
			priv->ethdr_comp[i].regs, ethdr_comp_str[i]);
	}

	for (i = 0; i < ETHDR_ASYNC_ID_MAX; i++) {
		priv->async_clk[i] =
			of_clk_get_by_name(dev->of_node, ethdr_async_clk_str[i]);
		if (IS_ERR(priv->async_clk[i])) {
			priv->async_clk[i] = NULL;
			dev_dbg(dev, "get async_clk[%d] failed!\n", i);
			return PTR_ERR(priv->async_clk[i]);
		}
	}

	priv->data = of_device_get_match_data(dev);
	g_ethdr = priv;

	platform_set_drvdata(pdev, priv);

	pm_runtime_enable(dev);

	ret = component_add(dev, &mtk_ethdr_component_ops);
	if (ret != 0) {
		dev_notice(dev, "Failed to add component: %d\n", ret);
		pm_runtime_disable(dev);
	}

	pr_info("%s-\n", __func__);

	return ret;
}

static int mtk_ethdr_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_ethdr_component_ops);

	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static const struct mtk_ethdr_data mt8195_ethdr_driver_data = {
	.tbd = false,
};

static const struct of_device_id mtk_ethdr_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8195-disp-ethdr",
	  .data = &mt8195_ethdr_driver_data},
	{},
};

MODULE_DEVICE_TABLE(of, mtk_ethdr_driver_dt_match);

struct platform_driver mtk_ethdr_driver = {
	.probe = mtk_ethdr_probe,
	.remove = mtk_ethdr_remove,
	.driver = {

			.name = "mediatek-disp-ethdr",
			.owner = THIS_MODULE,
			.of_match_table = mtk_ethdr_driver_dt_match,
		},
};
