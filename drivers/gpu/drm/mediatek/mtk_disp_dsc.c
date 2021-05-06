// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#include "mtk_drm_crtc.h"
#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_gem.h"
#include "mtk_disp_drv.h"
#ifdef CONFIG_MTK_DPTX_SUPPORT
#include "mtk_dp_api.h"
#endif

#define DISP_REG_DSC_CON			0x0000
	#define DSC_EN BIT(0)
	#define DSC_DUAL_INOUT BIT(2)
	#define DSC_IN_SRC_SEL BIT(3)
	#define DSC_BYPASS BIT(4)
	#define DSC_RELAY BIT(5)
	#define DSC_EMPTY_FLAG_SEL		0xC000
	#define DSC_UFOE_SEL BIT(16)
	#define CON_FLD_DSC_EN		REG_FLD_MSB_LSB(0, 0)
	#define CON_FLD_DISP_DSC_BYPASS		REG_FLD_MSB_LSB(4, 4)

#define DISP_REG_DSC_PIC_W			0x0018
	#define CFG_FLD_PIC_WIDTH	REG_FLD_MSB_LSB(15, 0)
	#define CFG_FLD_PIC_HEIGHT_M1	REG_FLD_MSB_LSB(31, 16)

#define DISP_REG_DSC_PIC_H			0x001C

#define DISP_REG_DSC_SLICE_W		0x0020
	#define CFG_FLD_SLICE_WIDTH	REG_FLD_MSB_LSB(15, 0)

#define DISP_REG_DSC_SLICE_H		0x0024

#define DISP_REG_DSC_CHUNK_SIZE		0x0028

#define DISP_REG_DSC_BUF_SIZE		0x002C

#define DISP_REG_DSC_MODE			0x0030
	#define DSC_SLICE_MODE BIT(0)
	#define DSC_RGB_SWAP BIT(2)
#define DISP_REG_DSC_CFG			0x0034

#define DISP_REG_DSC_PAD			0x0038

#define DISP_REG_DSC_DBG_CON		0x0060
	#define DSC_CKSM_CAL_EN BIT(9)
#define DISP_REG_DSC_OBUF			0x0070
#define DISP_REG_DSC_PPS0			0x0080
#define DISP_REG_DSC_PPS1			0x0084
#define DISP_REG_DSC_PPS2			0x0088
#define DISP_REG_DSC_PPS3			0x008C
#define DISP_REG_DSC_PPS4			0x0090
#define DISP_REG_DSC_PPS5			0x0094
#define DISP_REG_DSC_PPS6			0x0098
#define DISP_REG_DSC_PPS7			0x009C
#define DISP_REG_DSC_PPS8			0x00A0
#define DISP_REG_DSC_PPS9			0x00A4
#define DISP_REG_DSC_PPS10			0x00A8
#define DISP_REG_DSC_PPS11			0x00AC
#define DISP_REG_DSC_PPS12			0x00B0
#define DISP_REG_DSC_PPS13			0x00B4
#define DISP_REG_DSC_PPS14			0x00B8
#define DISP_REG_DSC_PPS15			0x00BC
#define DISP_REG_DSC_PPS16			0x00C0
#define DISP_REG_DSC_PPS17			0x00C4
#define DISP_REG_DSC_PPS18			0x00C8
#define DISP_REG_DSC_PPS19			0x00CC

#if defined(CONFIG_MACH_MT6885) || defined(CONFIG_MACH_MT6893)
#define DISP_REG_DSC_SHADOW			0x0200
	#define DSC_FORCE_COMMIT BIT(1)
#elif defined(CONFIG_MACH_MT6873) || defined(CONFIG_MACH_MT6853) \
	|| defined(CONFIG_MACH_MT8195)
#define DISP_REG_DSC_SHADOW			0x0200
#define DSC_FORCE_COMMIT	BIT(0)
#define DSC_BYPASS_SHADOW	BIT(1)
#define DSC_READ_WORKING	BIT(2)
#endif

struct mtk_disp_dsc_data {
	bool support_shadow;
};


/**
 * struct mtk_disp_dsc - DISP_DSC driver structure
 * @ddp_comp - structure containing type enum and hardware resources
 */
struct mtk_disp_dsc {
	struct clk *clk;
	void __iomem *regs;
	struct mtk_ddp_comp	ddp_comp;
	struct cmdq_client_reg		cmdq_reg;
	const struct mtk_disp_dsc_data *data;
	int enable;
};

void mtk_dsc_start(struct device *dev)
{
	struct mtk_disp_dsc *dsc = dev_get_drvdata(dev);
	void __iomem *baddr = dsc->regs;
	int ret = 0;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		DRM_ERROR("Failed to enable power domain: %d\n", ret);

#if defined(CONFIG_MACH_MT6885) || defined(CONFIG_MACH_MT6893) \
	|| defined(CONFIG_MACH_MT6873) || defined(CONFIG_MACH_MT6853) \
	|| defined(CONFIG_MACH_MT8195)
	mtk_ddp_write_mask(NULL, DSC_FORCE_COMMIT,
		&dsc->cmdq_reg, baddr,
		DISP_REG_DSC_SHADOW, DSC_FORCE_COMMIT);
#endif

	if (dsc->enable) {
		/* DSC Empty flag always high */
		mtk_ddp_write_mask(NULL, 0x4000,
		&dsc->cmdq_reg, baddr,
		DISP_REG_DSC_CON, DSC_EMPTY_FLAG_SEL);

		/* DSC output buffer as FHD(plus) */
		mtk_ddp_write_mask(NULL, 0x800002C2,
		&dsc->cmdq_reg, baddr,
		DISP_REG_DSC_OBUF, 0xFFFFFFFF);
	}

	mtk_ddp_write_mask(NULL, DSC_EN,
		&dsc->cmdq_reg, baddr,
		DISP_REG_DSC_CON, DSC_EN);

	pr_info("dsc_start:0x%x\n", readl(baddr + DISP_REG_DSC_CON));
}

void mtk_dsc_stop(struct device *dev)
{
	struct mtk_disp_dsc *dsc = dev_get_drvdata(dev);
	void __iomem *baddr = dsc->regs;
	int ret = 0;

	mtk_ddp_write_mask(NULL, 0x0, &dsc->cmdq_reg, baddr, DISP_REG_DSC_CON, DSC_EN);
	pr_info("dsc_stop:0x%x\n",
			readl(baddr + DISP_REG_DSC_CON));

	ret = pm_runtime_put(dev);
	if (ret < 0)
		DRM_ERROR("Failed to disable power domain: %d\n", ret);
}

int mtk_dsc_clk_enable(struct device *dev)
{
	struct mtk_disp_dsc *dsc = dev_get_drvdata(dev);
	int ret = 0;

	ret = clk_prepare_enable(dsc->clk);

#if defined(CONFIG_MACH_MT6873) || defined(CONFIG_MACH_MT6853)
#if defined(CONFIG_DRM_MTK_SHADOW_REGISTER_SUPPORT)
	if (dsc->data->support_shadow) {
		/* Enable shadow register and read shadow register */
		mtk_ddp_write_mask_cpu(&dsc->ddp_comp, 0x0,
			DISP_REG_DSC_SHADOW, DSC_BYPASS_SHADOW);
	} else {
		/* Bypass shadow register and read shadow register */
		mtk_ddp_write_mask_cpu(&dsc->ddp_comp, DSC_BYPASS_SHADOW,
			DISP_REG_DSC_SHADOW, DSC_BYPASS_SHADOW);
	}
#else
	/* Bypass shadow register and read shadow register */
	mtk_ddp_write_mask_cpu(&dsc->ddp_comp, DSC_BYPASS_SHADOW,
		DISP_REG_DSC_SHADOW, DSC_BYPASS_SHADOW);
#endif
#endif

	return ret;
}

void mtk_dsc_clk_disable(struct device *dev)
{
	struct mtk_disp_dsc *dsc = dev_get_drvdata(dev);

	clk_disable_unprepare(dsc->clk);
}

static struct mtk_panel_dsc_params *mtk_dsc_default_setting(void)
{
	static struct mtk_panel_dsc_params dsc_params = {
		//.enable = 1,
		.enable = 0, // relay mode
		.ver = 2,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 0x12,//flatness_det_thr, 8bit
		.rct_on = 1,//default
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 13, //9,//11 for 10bit
		.bp_enable = 1,//align vend
		.bit_per_pixel = 128,//16*bpp
		.pic_height = 2160,
		.pic_width = 3840, /*for dp port 4k scenario*/
		.slice_height = 8,
		.slice_width = 1920,// frame_width/slice mode
		.chunk_size = 1920,
		.xmit_delay = 512, //410,
		.dec_delay = 1216, //526,
		.scale_value = 32,
		.increment_interval = 286, //488,
		.decrement_interval = 26, //7,
		.line_bpg_offset = 12, //12,
		.nfl_bpg_offset = 3511, //1294,
		.slice_bpg_offset = 916, //1302,
		.initial_offset = 6144,
		.final_offset = 4336,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
	};
#ifdef CONFIG_MTK_DPTX_SUPPORT
	u8 dsc_cap[16];

	mtk_dp_get_dsc_capability(dsc_cap);
	dsc_params.bp_enable = dsc_cap[6];
	//dsc_params.ver = dsc_cap[1];
#endif

	return &dsc_params;
}

//extern void mtk_dp_dsc_pps_send(u8 *PPS);
u8 PPS[128] = {
	0x12, 0x00, 0x00, 0x8d, 0x30, 0x80, 0x08, 0x70,
	0x0f, 0x00, 0x00, 0x08, 0x07, 0x80, 0x07, 0x80,
	0x02, 0x00, 0x04, 0xc0, 0x00, 0x20, 0x01, 0x1e,
	0x00, 0x1a, 0x00, 0x0c, 0x0d, 0xb7, 0x03, 0x94,
	0x18, 0x00, 0x10, 0xf0, 0x03, 0x0c, 0x20, 0x00,
	0x06, 0x0b, 0x0b, 0x33, 0x0e, 0x1c, 0x2a, 0x38,
	0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7b,
	0x7d, 0x7e, 0x01, 0x02, 0x01, 0x00, 0x09, 0x40,
	0x09, 0xbe, 0x19, 0xfc, 0x19, 0xfa, 0x19, 0xf8,
	0x1a, 0x38, 0x1a, 0x78, 0x22, 0xb6, 0x2a, 0xb6,
	0x2a, 0xf6, 0x2a, 0xf4, 0x43, 0x34, 0x63, 0x74,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

void mtk_dsc_config(struct device *dev, unsigned int w,
					unsigned int h, unsigned int vrefresh,
					unsigned int bpc, struct cmdq_pkt *handle)
{
	struct mtk_disp_dsc *dsc = dev_get_drvdata(dev);
	struct mtk_ddp_comp *comp = &dsc->ddp_comp;
	struct mtk_panel_dsc_params *dsc_params;

	u32 reg_val;
	unsigned int dsc_con = 0;
	unsigned int pic_group_width, slice_width, slice_height;
	unsigned int pic_height_ext_num, slice_group_width;
	unsigned int bit_per_pixel, chrunk_size, pad_num;
	unsigned int init_delay_limit, init_delay_height_min;
	unsigned int init_delay_height;

	/*if (!comp->mtk_crtc || (!comp->mtk_crtc->panel_ext
				&& !comp->mtk_crtc->is_dual_pipe))
		return;*/

	dsc_params = mtk_dsc_default_setting();

	if (dsc_params->enable == 1) {
		pr_info("comp_id:%d, w:%d, h:%d, slice_mode:%d,slice(%d,%d),bpp:%d\n",
			comp->id, w, h,
			dsc_params->slice_mode,	dsc_params->slice_width,
			dsc_params->slice_height, dsc_params->bit_per_pixel);

		pic_group_width = (w + 2)/3;
		slice_width = dsc_params->slice_width;
		slice_height = dsc_params->slice_height;
		pic_height_ext_num = (h + slice_height - 1) / slice_height;
		slice_group_width = (slice_width + 2)/3;
		/* 128=1/3, 196=1/2 */
		bit_per_pixel = dsc_params->bit_per_pixel;
		chrunk_size = (slice_width*bit_per_pixel/8/16);
		pad_num = (chrunk_size + 2)/3*3 - chrunk_size;

		dsc_con |= DSC_UFOE_SEL;
		if (comp->mtk_crtc->is_dual_pipe)
			dsc_con |= DSC_DUAL_INOUT | DSC_IN_SRC_SEL;

		mtk_ddp_write_relaxed(handle,
			dsc_con,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_CON);

		mtk_ddp_write_relaxed(handle,
			(pic_group_width - 1) << 16 | w,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PIC_W);

		mtk_ddp_write_relaxed(handle,
			(pic_height_ext_num * slice_height - 1) << 16 |
			(h - 1),
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PIC_H);

		mtk_ddp_write_relaxed(handle,
			(slice_group_width - 1) << 16 | slice_width,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_SLICE_W);

		mtk_ddp_write_relaxed(handle,
			(slice_group_width - 1) << 16 | slice_width,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_SLICE_W);

		mtk_ddp_write_relaxed(handle,
			(slice_width % 3) << 30 |
			(pic_height_ext_num - 1) << 16 |
			(slice_height - 1),
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_SLICE_H);

		mtk_ddp_write_relaxed(handle, chrunk_size,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_CHUNK_SIZE);

		mtk_ddp_write_relaxed(handle,	pad_num,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PAD);

		mtk_ddp_write_relaxed(handle,	chrunk_size * slice_height,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_BUF_SIZE);

		init_delay_limit =
			((128 + (dsc_params->xmit_delay + 2) / 3) * 3 +
			dsc_params->slice_width-1) / dsc_params->slice_width;
		init_delay_height_min =
			(init_delay_limit > 15) ? 15 : init_delay_limit;
		init_delay_height = init_delay_height_min;

		reg_val = (!!dsc_params->slice_mode) |
					(!!dsc_params->rgb_swap << 2) |
					(init_delay_height << 8);
		mtk_ddp_write_mask(handle, reg_val,
					&dsc->cmdq_reg, dsc->regs,
					DISP_REG_DSC_MODE, 0xFFFF);

		pr_info("comp_id:%d, init delay:%d\n",
			comp->id, reg_val);

		mtk_ddp_write_relaxed(handle,
			(dsc_params->dsc_cfg == 0) ? 0x22 : dsc_params->dsc_cfg,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_CFG);

		mtk_ddp_write_mask(handle, DSC_CKSM_CAL_EN,
					&dsc->cmdq_reg, dsc->regs,
					DISP_REG_DSC_DBG_CON, DSC_CKSM_CAL_EN);
#if defined(CONFIG_MACH_MT6885) || defined(CONFIG_MACH_MT6893) \
	|| defined(CONFIG_MACH_MT6873) || defined(CONFIG_MACH_MT6853) \
	|| defined(CONFIG_MACH_MT8195)
		mtk_ddp_write_mask(handle,
			(((dsc_params->ver & 0xf) == 2) ? 0x40 : 0x20),
			&dsc->cmdq_reg, comp->regs,
			DISP_REG_DSC_SHADOW, 0x60);
#endif
		if (dsc_params->dsc_line_buf_depth == 0)
			reg_val = 0x9;
		else
			reg_val = dsc_params->dsc_line_buf_depth;
		if (dsc_params->bit_per_channel == 0)
			reg_val |= (0x8 << 4);
		else
			reg_val |= (dsc_params->bit_per_channel << 4);
		if (dsc_params->bit_per_pixel == 0)
			reg_val |= (0x80 << 8);
		else
			reg_val |= (dsc_params->bit_per_pixel << 8);
		if (dsc_params->rct_on == 0)
			reg_val |= (0x1 << 18);
		else
			reg_val |= (dsc_params->rct_on << 18);
		reg_val |= (dsc_params->bp_enable << 19);
		mtk_ddp_write_relaxed(handle,	reg_val,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PPS0);

		if (dsc_params->xmit_delay == 0)
			reg_val = 0x200;
		else
			reg_val = (dsc_params->xmit_delay);
		if (dsc_params->dec_delay == 0)
			reg_val |= (0x268 << 16);
		else
			reg_val |= (dsc_params->dec_delay << 16);
		mtk_ddp_write_relaxed(handle,	reg_val,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PPS1);

		reg_val = ((dsc_params->scale_value == 0) ?
			0x20 : dsc_params->scale_value);
		reg_val |= ((dsc_params->increment_interval == 0) ?
			0x387 : dsc_params->increment_interval) << 16;
		mtk_ddp_write_relaxed(handle,	reg_val,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PPS2);

		reg_val = ((dsc_params->decrement_interval == 0) ?
			0xa : dsc_params->decrement_interval);
		reg_val |= ((dsc_params->line_bpg_offset == 0) ?
			0xc : dsc_params->line_bpg_offset) << 16;
		mtk_ddp_write_relaxed(handle,	reg_val,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PPS3);

		reg_val = ((dsc_params->nfl_bpg_offset == 0) ?
			0x319 : dsc_params->nfl_bpg_offset);
		reg_val |= ((dsc_params->slice_bpg_offset == 0) ?
			0x263 : dsc_params->slice_bpg_offset) << 16;
		mtk_ddp_write_relaxed(handle,	reg_val,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PPS4);

		reg_val = ((dsc_params->initial_offset == 0) ?
			0x1800 : dsc_params->initial_offset);
		reg_val |= ((dsc_params->final_offset == 0) ?
			0x10f0 : dsc_params->final_offset) << 16;
		mtk_ddp_write_relaxed(handle,	reg_val,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PPS5);

		reg_val = ((dsc_params->flatness_minqp == 0) ?
			0x3 : dsc_params->flatness_minqp);
		reg_val |= ((dsc_params->flatness_maxqp == 0) ?
			0xc : dsc_params->flatness_maxqp) << 8;
		reg_val |= ((dsc_params->rc_model_size == 0) ?
			0x2000 : dsc_params->rc_model_size) << 16;
		mtk_ddp_write_relaxed(handle,	reg_val,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PPS6);

		mtk_ddp_write(handle, 0x20000c03,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PPS6);
		mtk_ddp_write(handle, 0x330b0b06,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PPS7);
		mtk_ddp_write(handle, 0x382a1c0e,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PPS8);
		mtk_ddp_write(handle, 0x69625446,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PPS9);
		mtk_ddp_write(handle, 0x7b797770,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PPS10);
		mtk_ddp_write(handle, 0x00007e7d,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PPS11);
		mtk_ddp_write(handle, 0x00800880,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PPS12);
		mtk_ddp_write(handle, 0xf8c100a1,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_PPS13);
		if (comp->mtk_crtc->is_dual_pipe) {
			mtk_ddp_write(handle, 0xe8e3f0e3,
				&dsc->cmdq_reg, dsc->regs,
				DISP_REG_DSC_PPS14);
			mtk_ddp_write(handle, 0xe103e0e3,
				&dsc->cmdq_reg, dsc->regs,
				DISP_REG_DSC_PPS15);
			mtk_ddp_write(handle, 0xd944e123,
				&dsc->cmdq_reg, dsc->regs,
				DISP_REG_DSC_PPS16);
			mtk_ddp_write(handle, 0xd965d945,
				&dsc->cmdq_reg, dsc->regs,
				DISP_REG_DSC_PPS17);
			mtk_ddp_write(handle, 0xd188d165,
				&dsc->cmdq_reg, dsc->regs,
				DISP_REG_DSC_PPS18);
			mtk_ddp_write(handle, 0x0000d1ac,
				&dsc->cmdq_reg, dsc->regs,
				DISP_REG_DSC_PPS19);

			///mtk_dp_dsc_pps_send(PPS);
		} else {
			mtk_ddp_write(handle, 0xe8e3f0e3,
				&dsc->cmdq_reg, dsc->regs,
				DISP_REG_DSC_PPS14);
			mtk_ddp_write(handle, 0xe103e0e3,
				&dsc->cmdq_reg, dsc->regs,
				DISP_REG_DSC_PPS15);
			mtk_ddp_write(handle, 0xd943e123,
				&dsc->cmdq_reg, dsc->regs,
				DISP_REG_DSC_PPS16);
			mtk_ddp_write(handle, 0xd185d965,
				&dsc->cmdq_reg, dsc->regs,
				DISP_REG_DSC_PPS17);
			mtk_ddp_write(handle, 0xd1a7d1a5,
				&dsc->cmdq_reg, dsc->regs,
				DISP_REG_DSC_PPS18);
			mtk_ddp_write(handle, 0x0000d1ed,
				&dsc->cmdq_reg, dsc->regs,
				DISP_REG_DSC_PPS19);
		}

		dsc->enable = true;
	} else {
		/*enable dsc bypass mode*/
		mtk_ddp_write_mask(handle, DSC_BYPASS,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_CON, DSC_BYPASS);
		mtk_ddp_write_mask(handle, DSC_UFOE_SEL,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_CON, DSC_UFOE_SEL);
		mtk_ddp_write_mask(handle, DSC_DUAL_INOUT,
			&dsc->cmdq_reg, dsc->regs,
			DISP_REG_DSC_CON, DSC_DUAL_INOUT);
		dsc->enable = false;
	}
}

/*void mtk_dsc_dump(struct mtk_ddp_comp *comp)
{
	void __iomem *baddr = comp->regs;
	int i;

	pr_info("== comp_id:%d REGS ==\n", comp->id);

	pr_info("(0x000)DSC_START=0x%x\n", readl(baddr + DISP_REG_DSC_CON));
	pr_info("(0x020)DSC_SLICE_WIDTH=0x%x\n",
		readl(baddr + DISP_REG_DSC_SLICE_W));
	pr_info("(0x024)DSC_SLICE_HIGHT=0x%x\n",
		readl(baddr + DISP_REG_DSC_SLICE_H));
	pr_info("(0x000)DSC_WIDTH=0x%x\n", readl(baddr + DISP_REG_DSC_PIC_W));
	pr_info("(0x000)DSC_HEIGHT=0x%x\n", readl(baddr + DISP_REG_DSC_PIC_H));
#if defined(CONFIG_MACH_MT6885) || defined(CONFIG_MACH_MT6893) \
	|| defined(CONFIG_MACH_MT6873) || defined(CONFIG_MACH_MT6853)
	pr_info("(0x000)DSC_SHADOW=0x%x\n",
		readl(baddr + DISP_REG_DSC_SHADOW));
#endif
	pr_info("-- Start dump dsc registers --\n");
	for (i = 0; i < 204; i += 16) {
		pr_info("DSC+%x: 0x%x 0x%x 0x%x 0x%x\n", i, readl(baddr + i),
			 readl(baddr + i + 0x4), readl(baddr + i + 0x8),
			 readl(baddr + i + 0xc));
	}
}

int mtk_dsc_analysis(struct mtk_ddp_comp *comp)
{
	void __iomem *baddr = comp->regs;

	pr_info("== comp_id:%d ANALYSIS ==\n", comp->id);
	pr_info("en=%d, pic_w=%d, pic_h=%d, slice_w=%d, bypass=%d\n",
		 DISP_REG_GET_FIELD(CON_FLD_DSC_EN,
				baddr + DISP_REG_DSC_CON),
		 DISP_REG_GET_FIELD(CFG_FLD_PIC_WIDTH,
				baddr + DISP_REG_DSC_PIC_W),
		 DISP_REG_GET_FIELD(CFG_FLD_PIC_HEIGHT_M1,
				baddr + DISP_REG_DSC_PIC_H),
		 DISP_REG_GET_FIELD(CFG_FLD_SLICE_WIDTH,
				baddr + DISP_REG_DSC_SLICE_W),
		 DISP_REG_GET_FIELD(CON_FLD_DISP_DSC_BYPASS,
				baddr + DISP_REG_DSC_CON));

	return 0;
}*/

static int mtk_disp_dsc_bind(struct device *dev, struct device *master,
				  void *data)
{
	pr_info("%s\n", __func__);

	return 0;
}

static void mtk_disp_dsc_unbind(struct device *dev, struct device *master,
				 void *data)
{
	pr_info("%s\n", __func__);
}

static const struct component_ops mtk_disp_dsc_component_ops = {
	.bind = mtk_disp_dsc_bind,
	.unbind = mtk_disp_dsc_unbind,
};

static int mtk_disp_dsc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct mtk_disp_dsc *priv;
	int irq;
	int ret;

	pr_info("%s+\n", __func__);
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get dsc clk\n");
		return PTR_ERR(priv->clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->regs)) {
		dev_err(dev, "failed to ioremap dsc\n");
		return PTR_ERR(priv->regs);
	}

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif

	priv->data = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, priv);

	pm_runtime_enable(dev);

	ret = component_add(dev, &mtk_disp_dsc_component_ops);
	if (ret != 0) {
		dev_err(dev, "Failed to add component: %d\n", ret);
		pm_runtime_disable(dev);
	}

	pr_info("%s-\n", __func__);
	return ret;
}

static int mtk_disp_dsc_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_dsc_component_ops);

	pm_runtime_disable(&pdev->dev);
	return 0;
}

static const struct mtk_disp_dsc_data mt8195_dsc_driver_data = {
	.support_shadow = false,
};

static const struct of_device_id mtk_disp_dsc_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8195-disp-dsc",
	  .data = &mt8195_dsc_driver_data},
	{},
};

MODULE_DEVICE_TABLE(of, mtk_disp_dsc_driver_dt_match);

struct platform_driver mtk_disp_dsc_driver = {
	.probe = mtk_disp_dsc_probe,
	.remove = mtk_disp_dsc_remove,
	.driver = {
		.name = "mediatek-disp-dsc",
		.owner = THIS_MODULE,
		.of_match_table = mtk_disp_dsc_driver_dt_match,
	},
};
