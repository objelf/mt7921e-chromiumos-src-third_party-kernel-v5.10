// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */
#include <drm/drm_fourcc.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/iommu.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "mtk_drm_drv.h"
#include "mtk_drm_ddp_comp.h"
#include "mtk_mdp_reg_rdma.h"
#include "mtk_mdp_rdma.h"

#define IRQ_LEVEL_IDLE		(0)
#define IRQ_LEVEL_ALL		(1)

#define IRQ_INT_EN_ALL	\
	(REG_FLD_MASK(MDP_RDMA_INTERRUPT_ENABLE_FLD_UNDERRUN_INT_EN) \
	| REG_FLD_MASK(MDP_RDMA_INTERRUPT_ENABLE_FLD_REG_UPDATE_INT_EN) \
	| REG_FLD_MASK(MDP_RDMA_INTERRUPT_ENABLE_FLD_FRAME_COMPLETE_INT_EN))

#define RDMA_DEBUG(fmt, ...)	pr_debug(fmt, ##__VA_ARGS__)

static unsigned int rdma_get_y_pitch(unsigned int fmt, unsigned int width)
{
	switch (fmt) {
	default:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		return 2 * width;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		return 3 * width;
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_BGRA1010102:
		return 4 * width;
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_YUYV:
		return 2 * width;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		return 1 * width;
	}
}

static unsigned int rdma_get_uv_pitch(unsigned int fmt, unsigned int width)
{
	switch (fmt) {
	default:
		return 0;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		return 4 * width;
	}
}

static unsigned int rdma_get_block_w(unsigned int mode)
{
	switch (mode) {
	default:
		return 0;
	case RDMA_BLOCK_8x8:
	case RDMA_BLOCK_8x16:
	case RDMA_BLOCK_8x32:
		return 8;
	case RDMA_BLOCK_16x8:
	case RDMA_BLOCK_16x16:
	case RDMA_BLOCK_16x32:
		return 16;
	case RDMA_BLOCK_32x8:
	case RDMA_BLOCK_32x16:
	case RDMA_BLOCK_32x32:
		return 32;
	}
}

static unsigned int rdma_get_block_h(unsigned int mode)
{
	switch (mode) {
	default:
		return 0;
	case RDMA_BLOCK_8x8:
	case RDMA_BLOCK_16x8:
	case RDMA_BLOCK_32x8:
		return 8;
	case RDMA_BLOCK_8x16:
	case RDMA_BLOCK_16x16:
	case RDMA_BLOCK_32x16:
		return 16;
	case RDMA_BLOCK_8x32:
	case RDMA_BLOCK_16x32:
	case RDMA_BLOCK_32x32:
		return 32;
	}
}


static unsigned int rdma_get_horizontal_shift_uv(unsigned int fmt)
{
	switch (fmt) {
	default:
		return 0;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		return 1;
	}
}

static unsigned int rdma_get_vertical_shift_uv(unsigned int fmt)
{
	switch (fmt) {
	default:
		return 0;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		return 1;
	}
}

static unsigned int rdma_get_bits_per_pixel_y(unsigned int fmt)
{
	switch (fmt) {
	default:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		return 16;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		return 24;
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		return 32;
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_YUYV:
		return 16;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		return 8;
	}
}

static unsigned int rdma_get_bits_per_pixel_uv(unsigned int fmt)
{
	switch (fmt) {
	default:
		return 0;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		return 16;
	}
}

static bool with_alpha(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_BGRA1010102:
		return true;
	default:
		return false;
	}
}

static unsigned int rdma_fmt_convert(unsigned int fmt)
{
	switch (fmt) {
	default:
	case DRM_FORMAT_RGB565:
		return RDMA_INPUT_FORMAT_RGB565;
	case DRM_FORMAT_BGR565:
		return RDMA_INPUT_FORMAT_RGB565 | RDMA_INPUT_SWAP;
	case DRM_FORMAT_RGB888:
		return RDMA_INPUT_FORMAT_RGB888;
	case DRM_FORMAT_BGR888:
		return RDMA_INPUT_FORMAT_RGB888 | RDMA_INPUT_SWAP;
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBA8888:
		return RDMA_INPUT_FORMAT_ARGB8888;
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_BGRA8888:
		return RDMA_INPUT_FORMAT_ARGB8888 | RDMA_INPUT_SWAP;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		return RDMA_INPUT_FORMAT_RGBA8888;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		return RDMA_INPUT_FORMAT_RGBA8888 | RDMA_INPUT_SWAP;
	case DRM_FORMAT_ABGR2101010:
		return RDMA_INPUT_FORMAT_RGBA8888 | RDMA_INPUT_SWAP | RDMA_INPUT_10BIT;
	case DRM_FORMAT_ARGB2101010:
		return RDMA_INPUT_FORMAT_RGBA8888 | RDMA_INPUT_10BIT;
	case DRM_FORMAT_RGBA1010102:
		return RDMA_INPUT_FORMAT_ARGB8888 | RDMA_INPUT_SWAP | RDMA_INPUT_10BIT;
	case DRM_FORMAT_BGRA1010102:
		return RDMA_INPUT_FORMAT_ARGB8888 | RDMA_INPUT_10BIT;
	case DRM_FORMAT_UYVY:
		return RDMA_INPUT_FORMAT_UYVY;
	case DRM_FORMAT_YUYV:
		return RDMA_INPUT_FORMAT_YUY2;
	}
}

void mtk_mdp_rdma_start(void __iomem *base,
	struct cmdq_pkt *cmdq_pkt,
	struct cmdq_client_reg *cmdq_base)
{
	unsigned int inten = IRQ_INT_EN_ALL;

	mtk_ddp_write_mask(cmdq_pkt, inten, cmdq_base, base,
		MDP_RDMA_INTERRUPT_ENABLE,
		IRQ_INT_EN_ALL);

	mtk_ddp_write_mask(cmdq_pkt, 1<<REG_FLD_SHIFT(MDP_RDMA_EN_FLD_ROT_ENABLE),
		cmdq_base, base,
		MDP_RDMA_EN,
		REG_FLD_MASK(MDP_RDMA_EN_FLD_ROT_ENABLE));
}

void mtk_mdp_rdma_stop(void __iomem *base,
	struct cmdq_pkt *cmdq_pkt,
	struct cmdq_client_reg *cmdq_base)
{

	mtk_ddp_write_mask(cmdq_pkt, 0<<REG_FLD_SHIFT(MDP_RDMA_EN_FLD_ROT_ENABLE),
		cmdq_base, base,
		MDP_RDMA_EN,
		REG_FLD_MASK(MDP_RDMA_EN_FLD_ROT_ENABLE));

	mtk_ddp_write_mask(cmdq_pkt, 0, cmdq_base, base, MDP_RDMA_INTERRUPT_ENABLE,
		IRQ_INT_EN_ALL);

	mtk_ddp_write_mask(cmdq_pkt, 0, cmdq_base, base, MDP_RDMA_INTERRUPT_STATUS,
		IRQ_INT_EN_ALL);

	mtk_ddp_write_mask(cmdq_pkt, 1, cmdq_base, base, MDP_RDMA_RESET,
		~0);
	mtk_ddp_write_mask(cmdq_pkt, 0, cmdq_base, base, MDP_RDMA_RESET,
		~0);
}

void mtk_mdp_rdma_golden_setting(void __iomem *base,
	struct cmdq_pkt *cmdq_pkt,
	struct cmdq_client_reg *cmdq_base)
{
	int read_request_type = 7;
	int command_div = 1;
	int ext_preultra_en = 1;
	int ultra_en = 0;
	int pre_ultra_en = 1;
	int ext_ultra_en = 1;

	int extrd_arb_max_0 = 3;
	int buf_resv_size_0 = 0;
	int issue_req_th_0 = 0;

	int ultra_th_high_con_0 = 156;
	int ultra_th_low_con_0 = 104;


	mtk_ddp_write_mask(cmdq_pkt,
		MDP_RDMA_GMCIF_CON_VAL_READ_REQUEST_TYPE(read_request_type)|
			MDP_RDMA_GMCIF_CON_VAL_COMMAND_DIV(command_div)|
			MDP_RDMA_GMCIF_CON_VAL_EXT_PREULTRA_EN(ext_preultra_en)|
			MDP_RDMA_GMCIF_CON_VAL_ULTRA_EN(ultra_en)|
			MDP_RDMA_GMCIF_CON_VAL_PRE_ULTRA_EN(pre_ultra_en)|
			MDP_RDMA_GMCIF_CON_VAL_EXT_ULTRA_EN(ext_ultra_en),
		cmdq_base,
		base,
		MDP_RDMA_GMCIF_CON,
		REG_FLD_MASK(MDP_RDMA_GMCIF_CON_FLD_READ_REQUEST_TYPE)|
			REG_FLD_MASK(MDP_RDMA_GMCIF_CON_FLD_COMMAND_DIV)|
			REG_FLD_MASK(MDP_RDMA_GMCIF_CON_FLD_EXT_PREULTRA_EN)|
			REG_FLD_MASK(MDP_RDMA_GMCIF_CON_FLD_ULTRA_EN)|
			REG_FLD_MASK(MDP_RDMA_GMCIF_CON_FLD_PRE_ULTRA_EN)|
			REG_FLD_MASK(MDP_RDMA_GMCIF_CON_FLD_EXT_ULTRA_EN));

	/*dma buf 0*/
	mtk_ddp_write_mask(cmdq_pkt,
		MDP_RDMA_DMABUF_CON_0_VAL_EXTRD_ARB_MAX_0(extrd_arb_max_0)|
			MDP_RDMA_DMABUF_CON_0_VAL_BUF_RESV_SIZE_0(buf_resv_size_0)|
			MDP_RDMA_DMABUF_CON_0_VAL_ISSUE_REQ_TH_0(issue_req_th_0),
		cmdq_base,
		base,
		MDP_RDMA_DMABUF_CON_0,
		REG_FLD_MASK(MDP_RDMA_DMABUF_CON_0_FLD_EXTRD_ARB_MAX_0)|
			REG_FLD_MASK(MDP_RDMA_DMABUF_CON_0_FLD_BUF_RESV_SIZE_0)|
			REG_FLD_MASK(MDP_RDMA_DMABUF_CON_0_FLD_ISSUE_REQ_TH_0));

	mtk_ddp_write_mask(cmdq_pkt,
		MDP_RDMA_ULTRA_TH_HIGH_CON_0_VAL_PRE_ULTRA_TH_HIGH_OFS_0
			(ultra_th_high_con_0),
		cmdq_base,
		base,
		MDP_RDMA_ULTRA_TH_HIGH_CON_0,
		REG_FLD_MASK
			(MDP_RDMA_ULTRA_TH_HIGH_CON_0_FLD_PRE_ULTRA_TH_HIGH_OFS_0));


	mtk_ddp_write_mask(cmdq_pkt,
		MDP_RDMA_ULTRA_TH_LOW_CON_0_VAL_PRE_ULTRA_TH_LOW_OFS_0
			(ultra_th_low_con_0),
		cmdq_base,
		base,
		MDP_RDMA_ULTRA_TH_LOW_CON_0,
		REG_FLD_MASK
			(MDP_RDMA_ULTRA_TH_LOW_CON_0_FLD_PRE_ULTRA_TH_LOW_OFS_0));

	/*dma buf 1*/
	mtk_ddp_write_mask(cmdq_pkt,
		MDP_RDMA_DMABUF_CON_1_VAL_EXTRD_ARB_MAX_1(0)|
			MDP_RDMA_DMABUF_CON_1_VAL_BUF_RESV_SIZE_1(0)|
			MDP_RDMA_DMABUF_CON_1_VAL_ISSUE_REQ_TH_1(0),
		cmdq_base,
		base,
		MDP_RDMA_DMABUF_CON_1,
		REG_FLD_MASK(MDP_RDMA_DMABUF_CON_1_FLD_EXTRD_ARB_MAX_1)|
			REG_FLD_MASK(MDP_RDMA_DMABUF_CON_1_FLD_BUF_RESV_SIZE_1)|
			REG_FLD_MASK(MDP_RDMA_DMABUF_CON_1_FLD_ISSUE_REQ_TH_1));

	mtk_ddp_write_mask(cmdq_pkt,
		MDP_RDMA_ULTRA_TH_HIGH_CON_1_VAL_PRE_ULTRA_TH_HIGH_OFS_1(0),
		cmdq_base,
		base,
		MDP_RDMA_ULTRA_TH_HIGH_CON_1,
		REG_FLD_MASK
			(MDP_RDMA_ULTRA_TH_LOW_CON_1_FLD_PRE_ULTRA_TH_LOW_OFS_1));

	mtk_ddp_write_mask(cmdq_pkt,
		MDP_RDMA_ULTRA_TH_LOW_CON_1_VAL_PRE_ULTRA_TH_LOW_OFS_1(0),
		cmdq_base,
		base,
		MDP_RDMA_ULTRA_TH_LOW_CON_1,
		REG_FLD_MASK
			(MDP_RDMA_ULTRA_TH_LOW_CON_1_FLD_PRE_ULTRA_TH_LOW_OFS_1));


	/*dma buf 2*/
	mtk_ddp_write_mask(cmdq_pkt,
		MDP_RDMA_DMABUF_CON_2_VAL_EXTRD_ARB_MAX_2(0)|
			MDP_RDMA_DMABUF_CON_2_VAL_BUF_RESV_SIZE_2(0)|
			MDP_RDMA_DMABUF_CON_2_VAL_ISSUE_REQ_TH_2(0),
		cmdq_base,
		base,
		MDP_RDMA_DMABUF_CON_2,
		REG_FLD_MASK(MDP_RDMA_DMABUF_CON_2_FLD_EXTRD_ARB_MAX_2)|
			REG_FLD_MASK(MDP_RDMA_DMABUF_CON_2_FLD_BUF_RESV_SIZE_2)|
			REG_FLD_MASK(MDP_RDMA_DMABUF_CON_2_FLD_ISSUE_REQ_TH_2));

	mtk_ddp_write_mask(cmdq_pkt,
		MDP_RDMA_ULTRA_TH_HIGH_CON_2_VAL_PRE_ULTRA_TH_HIGH_OFS_2(0),
		cmdq_base,
		base,
		MDP_RDMA_ULTRA_TH_HIGH_CON_2,
		REG_FLD_MASK
			(MDP_RDMA_ULTRA_TH_HIGH_CON_2_FLD_PRE_ULTRA_TH_HIGH_OFS_2));

	mtk_ddp_write_mask(cmdq_pkt,
		MDP_RDMA_ULTRA_TH_LOW_CON_2_VAL_PRE_ULTRA_TH_LOW_OFS_2(0),
		cmdq_base,
		base,
		MDP_RDMA_ULTRA_TH_LOW_CON_2,
		REG_FLD_MASK
			(MDP_RDMA_ULTRA_TH_LOW_CON_2_FLD_PRE_ULTRA_TH_LOW_OFS_2));

	/*dma buf 3*/
	mtk_ddp_write_mask(cmdq_pkt,
		MDP_RDMA_DMABUF_CON_3_VAL_EXTRD_ARB_MAX_3(0)|
			MDP_RDMA_DMABUF_CON_3_VAL_BUF_RESV_SIZE_3(0)|
			MDP_RDMA_DMABUF_CON_3_VAL_ISSUE_REQ_TH_3(0),
		cmdq_base,
		base,
		MDP_RDMA_DMABUF_CON_3,
		REG_FLD_MASK(MDP_RDMA_DMABUF_CON_3_FLD_EXTRD_ARB_MAX_3)|
			REG_FLD_MASK(MDP_RDMA_DMABUF_CON_3_FLD_BUF_RESV_SIZE_3)|
			REG_FLD_MASK(MDP_RDMA_DMABUF_CON_3_FLD_ISSUE_REQ_TH_3));

	mtk_ddp_write_mask(cmdq_pkt,
		MDP_RDMA_ULTRA_TH_HIGH_CON_3_VAL_PRE_ULTRA_TH_HIGH_OFS_3(0),
		cmdq_base,
		base,
		MDP_RDMA_ULTRA_TH_HIGH_CON_3,
		REG_FLD_MASK
			(MDP_RDMA_ULTRA_TH_HIGH_CON_3_FLD_PRE_ULTRA_TH_HIGH_OFS_3));

	mtk_ddp_write_mask(cmdq_pkt,
		MDP_RDMA_ULTRA_TH_LOW_CON_3_VAL_PRE_ULTRA_TH_LOW_OFS_3(0),
		cmdq_base,
		base,
		MDP_RDMA_ULTRA_TH_LOW_CON_3,
		REG_FLD_MASK
			(MDP_RDMA_ULTRA_TH_LOW_CON_3_FLD_PRE_ULTRA_TH_LOW_OFS_3));
}

void mtk_mdp_rdma_config(void __iomem *base,
	struct mtk_mdp_rdma_cfg *cfg,
	struct cmdq_pkt *cmdq_pkt,
	struct cmdq_client_reg *cmdq_base,
	bool sec_on)
{
	unsigned int sourceImagePitchY =
			rdma_get_y_pitch(cfg->fmt, cfg->source_width);
	unsigned int sourceImagePitchUV =
			rdma_get_uv_pitch(cfg->fmt, cfg->source_width);
	unsigned int ringYStartLine = 0;
	unsigned int ringYEndLine = cfg->height - 1;
	unsigned int videoBlockWidth = rdma_get_block_w(cfg->block_size);
	unsigned int videoBlockHeight = rdma_get_block_h(cfg->block_size);
	unsigned int frameModeShift = 0;
	unsigned int horizontalShiftUV = rdma_get_horizontal_shift_uv(cfg->fmt);
	unsigned int verticalShiftUV = rdma_get_vertical_shift_uv(cfg->fmt);
	unsigned int bitsPerPixelY = rdma_get_bits_per_pixel_y(cfg->fmt);
	unsigned int bitsPerPixelUV = rdma_get_bits_per_pixel_uv(cfg->fmt);
	unsigned int videoBlockShiftW = 0;
	unsigned int videoBlockShiftH = 0;

	unsigned int tile_x_left = cfg->x_left;
	unsigned int tile_y_top = cfg->y_top;
	unsigned int offset_y = 0;
	unsigned int offset_u = 0;
	unsigned int offset_v = 0;

	RDMA_DEBUG("sourceImagePitchY = %d\n", sourceImagePitchY);
	RDMA_DEBUG("sourceImagePitchUV = %d\n", sourceImagePitchUV);
	RDMA_DEBUG("ringYStartLine = %d\n", ringYStartLine);
	RDMA_DEBUG("ringYEndLine = %d\n", ringYEndLine);
	RDMA_DEBUG("videoBlockWidth = %d\n", videoBlockWidth);
	RDMA_DEBUG("videoBlockHeight = %d\n", videoBlockHeight);
	RDMA_DEBUG("frameModeShift = %d\n", frameModeShift);
	RDMA_DEBUG("horizontalShiftUV = %d\n", horizontalShiftUV);
	RDMA_DEBUG("verticalShiftUV = %d\n", verticalShiftUV);
	RDMA_DEBUG("bitsPerPixelY = %d\n", bitsPerPixelY);
	RDMA_DEBUG("bitsPerPixelUV = %d\n", bitsPerPixelUV);
	RDMA_DEBUG("videoBlockShiftW = %d\n", videoBlockShiftW);
	RDMA_DEBUG("videoBlockShiftH = %d\n", videoBlockShiftH);

	/******** Frame ********/
	mtk_ddp_write_mask(cmdq_pkt,
		1<<REG_FLD_SHIFT(MDP_RDMA_SRC_CON_FLD_UNIFORM_CONFIG),
		cmdq_base,
		base,
		MDP_RDMA_SRC_CON,
		REG_FLD_MASK(MDP_RDMA_SRC_CON_FLD_UNIFORM_CONFIG));



	// Setup format and swap
	mtk_ddp_write_mask(cmdq_pkt, rdma_fmt_convert(cfg->fmt),
		cmdq_base, base, MDP_RDMA_SRC_CON,
		REG_FLD_MASK(MDP_RDMA_SRC_CON_FLD_SWAP)|
			REG_FLD_MASK(MDP_RDMA_SRC_CON_FLD_SRC_FORMAT)|
			REG_FLD_MASK(MDP_RDMA_SRC_CON_FLD_BIT_NUMBER));


	if ((!cfg->csc_enable) && with_alpha(cfg->fmt)) {
		mtk_ddp_write_mask(cmdq_pkt,
			1<<REG_FLD_SHIFT(MDP_RDMA_SRC_CON_FLD_OUTPUT_ARGB),
			cmdq_base, base, MDP_RDMA_SRC_CON,
			REG_FLD_MASK(MDP_RDMA_SRC_CON_FLD_OUTPUT_ARGB));
	} else {
		mtk_ddp_write_mask(cmdq_pkt,
			0<<REG_FLD_SHIFT(MDP_RDMA_SRC_CON_FLD_OUTPUT_ARGB),
			cmdq_base, base, MDP_RDMA_SRC_CON,
			REG_FLD_MASK(MDP_RDMA_SRC_CON_FLD_OUTPUT_ARGB));
	}

	// Setup start address
	mtk_ddp_write_mask(cmdq_pkt, cfg->addr0, cmdq_base, base,
		MDP_RDMA_SRC_BASE_0,
		REG_FLD_MASK(MDP_RDMA_SRC_BASE_0_FLD_SRC_BASE_0));

	mtk_ddp_write_mask(cmdq_pkt, cfg->addr1, cmdq_base, base,
		MDP_RDMA_SRC_BASE_1,
		REG_FLD_MASK(MDP_RDMA_SRC_BASE_1_FLD_SRC_BASE_1));

	mtk_ddp_write_mask(cmdq_pkt, cfg->addr2, cmdq_base, base,
		MDP_RDMA_SRC_BASE_2,
		REG_FLD_MASK(MDP_RDMA_SRC_BASE_2_FLD_SRC_BASE_2));


	// Setup source frame pitch
	mtk_ddp_write_mask(cmdq_pkt, sourceImagePitchY, cmdq_base, base,
		MDP_RDMA_MF_BKGD_SIZE_IN_BYTE,
		REG_FLD_MASK(MDP_RDMA_MF_BKGD_SIZE_IN_BYTE_FLD_MF_BKGD_WB));

	mtk_ddp_write_mask(cmdq_pkt, sourceImagePitchUV, cmdq_base, base,
		MDP_RDMA_SF_BKGD_SIZE_IN_BYTE,
		REG_FLD_MASK(MDP_RDMA_SF_BKGD_SIZE_IN_BYTE_FLD_SF_BKGD_WB));


	// Setup AFBC
	if (cfg->encode_type == RDMA_ENCODE_AFBC) {

		mtk_ddp_write_mask(cmdq_pkt,
			(cfg->source_width)<<REG_FLD_SHIFT(
				MDP_RDMA_MF_BKGD_SIZE_IN_PIXEL_FLD_MF_BKGD_WP),
			cmdq_base, base,
			MDP_RDMA_MF_BKGD_SIZE_IN_PIXEL,
			REG_FLD_MASK(MDP_RDMA_MF_BKGD_SIZE_IN_PIXEL_FLD_MF_BKGD_WP));

		mtk_ddp_write_mask(cmdq_pkt,
			(cfg->height)<<REG_FLD_SHIFT(
				MDP_RDMA_MF_BKGD_H_SIZE_IN_PIXEL_FLD_MF_BKGD_HP),
			cmdq_base, base,
			MDP_RDMA_MF_BKGD_H_SIZE_IN_PIXEL,
			REG_FLD_MASK(MDP_RDMA_MF_BKGD_H_SIZE_IN_PIXEL_FLD_MF_BKGD_HP));

		mtk_ddp_write_mask(cmdq_pkt,
			1<<REG_FLD_SHIFT(MDP_RDMA_COMP_CON_FLD_AFBC_YUV_TRANSFORM),
			cmdq_base, base,
			MDP_RDMA_COMP_CON,
			REG_FLD_MASK(MDP_RDMA_COMP_CON_FLD_AFBC_YUV_TRANSFORM));

		mtk_ddp_write_mask(cmdq_pkt,
			1<<REG_FLD_SHIFT(MDP_RDMA_COMP_CON_FLD_UFBDC_EN),
			cmdq_base, base,
			MDP_RDMA_COMP_CON,
			REG_FLD_MASK(MDP_RDMA_COMP_CON_FLD_UFBDC_EN));

		mtk_ddp_write_mask(cmdq_pkt,
			1<<REG_FLD_SHIFT(MDP_RDMA_COMP_CON_FLD_AFBC_EN),
			cmdq_base, base,
			MDP_RDMA_COMP_CON,
			REG_FLD_MASK(MDP_RDMA_COMP_CON_FLD_AFBC_EN));

	} else {
		mtk_ddp_write_mask(cmdq_pkt,
			0<<REG_FLD_SHIFT(MDP_RDMA_COMP_CON_FLD_AFBC_YUV_TRANSFORM),
			cmdq_base, base,
			MDP_RDMA_COMP_CON,
			REG_FLD_MASK(MDP_RDMA_COMP_CON_FLD_AFBC_YUV_TRANSFORM));

		mtk_ddp_write_mask(cmdq_pkt,
			0<<REG_FLD_SHIFT(MDP_RDMA_COMP_CON_FLD_UFBDC_EN),
			cmdq_base, base,
			MDP_RDMA_COMP_CON,
			REG_FLD_MASK(MDP_RDMA_COMP_CON_FLD_UFBDC_EN));

		mtk_ddp_write_mask(cmdq_pkt,
			0<<REG_FLD_SHIFT(MDP_RDMA_COMP_CON_FLD_AFBC_EN),
			cmdq_base, base,
			MDP_RDMA_COMP_CON,
			REG_FLD_MASK(MDP_RDMA_COMP_CON_FLD_AFBC_EN));
	}

	// Setup Ouptut 10b (csc, 8.2)
	mtk_ddp_write_mask(cmdq_pkt,
		1<<REG_FLD_SHIFT(MDP_RDMA_CON_FLD_OUTPUT_10B),
		cmdq_base, base,
		MDP_RDMA_CON,
		REG_FLD_MASK(MDP_RDMA_CON_FLD_OUTPUT_10B));

	// Setup Simple Mode
	mtk_ddp_write_mask(cmdq_pkt,
		1<<REG_FLD_SHIFT(MDP_RDMA_CON_FLD_SIMPLE_MODE),
		cmdq_base, base,
		MDP_RDMA_CON,
		REG_FLD_MASK(MDP_RDMA_CON_FLD_SIMPLE_MODE));

	// Setup Color Space Conversion
	mtk_ddp_write_mask(cmdq_pkt,
		(cfg->csc_enable)<<REG_FLD_SHIFT(MDP_RDMA_TRANSFORM_0_FLD_TRANS_EN),
		cmdq_base, base,
		MDP_RDMA_TRANSFORM_0,
		REG_FLD_MASK(MDP_RDMA_TRANSFORM_0_FLD_TRANS_EN));

	mtk_ddp_write_mask(cmdq_pkt,
		(cfg->profile)<<REG_FLD_SHIFT(MDP_RDMA_TRANSFORM_0_FLD_int_matrix_sel),
		cmdq_base, base,
		MDP_RDMA_TRANSFORM_0,
		REG_FLD_MASK(MDP_RDMA_TRANSFORM_0_FLD_int_matrix_sel));


	/******** Tile ********/

	if (cfg->block_size == RDMA_BLOCK_NONE) {
		ringYStartLine = tile_y_top;

		offset_y  = (tile_x_left * bitsPerPixelY >> 3)
			+ ringYStartLine * sourceImagePitchY;
		offset_u  = ((tile_x_left >> horizontalShiftUV)
			* bitsPerPixelUV >> 3)
			| (ringYStartLine >> verticalShiftUV)
			* sourceImagePitchUV;
		offset_v  = ((tile_x_left >> horizontalShiftUV)
			* bitsPerPixelUV >> 3)
			| (ringYStartLine >> verticalShiftUV)
			* sourceImagePitchUV;
	} else {
		// Alignment X left in block boundary
		tile_x_left = ((tile_x_left >> videoBlockShiftW)
			<< videoBlockShiftW);
		// Alignment Y top  in block boundary
		tile_y_top  = ((tile_y_top  >> videoBlockShiftH)
			<< videoBlockShiftH);

		offset_y = (tile_x_left * (videoBlockHeight << frameModeShift)
			* bitsPerPixelY >> 3)
			| (tile_y_top >> videoBlockShiftH) * sourceImagePitchY;
		offset_u = ((tile_x_left >> horizontalShiftUV)
			* ((videoBlockHeight >> verticalShiftUV)
			<< frameModeShift) * bitsPerPixelUV >> 3)
			| (tile_y_top >> videoBlockShiftH) * sourceImagePitchUV;
		offset_v = ((tile_x_left >> horizontalShiftUV)
			* ((videoBlockHeight >> verticalShiftUV)
			<< frameModeShift) * bitsPerPixelUV >> 3)
			| (tile_y_top >> videoBlockShiftH) * sourceImagePitchUV;
	}

	// Setup AFBC
	if (cfg->encode_type == RDMA_ENCODE_AFBC) {
		// tile x left
		mtk_ddp_write_mask(cmdq_pkt,
			tile_x_left<<
			REG_FLD_SHIFT(MDP_RDMA_SRC_OFFSET_WP_FLD_SRC_OFFSET_WP),
			cmdq_base, base,
			MDP_RDMA_SRC_OFFSET_WP,
			REG_FLD_MASK(MDP_RDMA_SRC_OFFSET_WP_FLD_SRC_OFFSET_WP));

		// tile y top
		mtk_ddp_write_mask(cmdq_pkt,
			tile_y_top<<
			REG_FLD_SHIFT(MDP_RDMA_SRC_OFFSET_HP_FLD_SRC_OFFSET_HP),
			cmdq_base, base,
			MDP_RDMA_SRC_OFFSET_HP,
			REG_FLD_MASK(MDP_RDMA_SRC_OFFSET_HP_FLD_SRC_OFFSET_HP));
	}

	// Set Y pixel offset
	mtk_ddp_write_mask(cmdq_pkt,
		offset_y<<REG_FLD_SHIFT(MDP_RDMA_SRC_OFFSET_0_FLD_SRC_OFFSET_0),
		cmdq_base, base,
		MDP_RDMA_SRC_OFFSET_0,
		REG_FLD_MASK(MDP_RDMA_SRC_OFFSET_0_FLD_SRC_OFFSET_0));

	// Set U pixel offset
	mtk_ddp_write_mask(cmdq_pkt,
		offset_u<<REG_FLD_SHIFT(MDP_RDMA_SRC_OFFSET_1_FLD_SRC_OFFSET_1),
		cmdq_base, base,
		MDP_RDMA_SRC_OFFSET_1,
		REG_FLD_MASK(MDP_RDMA_SRC_OFFSET_1_FLD_SRC_OFFSET_1));

	// Set V pixel offset
	mtk_ddp_write_mask(cmdq_pkt,
		offset_v<<REG_FLD_SHIFT(MDP_RDMA_SRC_OFFSET_2_FLD_SRC_OFFSET_2),
		cmdq_base, base,
		MDP_RDMA_SRC_OFFSET_2,
		REG_FLD_MASK(MDP_RDMA_SRC_OFFSET_2_FLD_SRC_OFFSET_2));

	// Set source size
	mtk_ddp_write_mask(cmdq_pkt,
		(cfg->width)<<REG_FLD_SHIFT(MDP_RDMA_MF_SRC_SIZE_FLD_MF_SRC_W),
		cmdq_base, base,
		MDP_RDMA_MF_SRC_SIZE,
		REG_FLD_MASK(MDP_RDMA_MF_SRC_SIZE_FLD_MF_SRC_W));

	mtk_ddp_write_mask(cmdq_pkt,
		cfg->height<<REG_FLD_SHIFT(MDP_RDMA_MF_SRC_SIZE_FLD_MF_SRC_H),
		cmdq_base, base,
		MDP_RDMA_MF_SRC_SIZE,
		REG_FLD_MASK(MDP_RDMA_MF_SRC_SIZE_FLD_MF_SRC_H));

	// Set target (clip) size
	mtk_ddp_write_mask(cmdq_pkt,
		(cfg->width)<<REG_FLD_SHIFT(MDP_RDMA_MF_CLIP_SIZE_FLD_MF_CLIP_W),
		cmdq_base, base,
		MDP_RDMA_MF_CLIP_SIZE,
		REG_FLD_MASK(MDP_RDMA_MF_CLIP_SIZE_FLD_MF_CLIP_W));

	mtk_ddp_write_mask(cmdq_pkt,
		cfg->height<<REG_FLD_SHIFT(MDP_RDMA_MF_CLIP_SIZE_FLD_MF_CLIP_H),
		cmdq_base, base,
		MDP_RDMA_MF_CLIP_SIZE,
		REG_FLD_MASK(MDP_RDMA_MF_CLIP_SIZE_FLD_MF_CLIP_H));

	// Set crop offset
	mtk_ddp_write_mask(cmdq_pkt,
		0<<REG_FLD_SHIFT(MDP_RDMA_MF_OFFSET_1_FLD_MF_OFFSET_W_1),
		cmdq_base, base,
		MDP_RDMA_MF_OFFSET_1,
		REG_FLD_MASK(MDP_RDMA_MF_OFFSET_1_FLD_MF_OFFSET_W_1));

	mtk_ddp_write_mask(cmdq_pkt,
		0<<REG_FLD_SHIFT(MDP_RDMA_MF_OFFSET_1_FLD_MF_OFFSET_H_1),
		cmdq_base, base,
		MDP_RDMA_MF_OFFSET_1,
		REG_FLD_MASK(MDP_RDMA_MF_OFFSET_1_FLD_MF_OFFSET_H_1));

	// Target Line
	mtk_ddp_write_mask(cmdq_pkt,
		(cfg->height)<<
		REG_FLD_SHIFT(MDP_RDMA_TARGET_LINE_FLD_LINE_THRESHOLD),
		cmdq_base, base,
		MDP_RDMA_TARGET_LINE,
		REG_FLD_MASK(MDP_RDMA_TARGET_LINE_FLD_LINE_THRESHOLD));

	mtk_ddp_write_mask(cmdq_pkt,
		1<<REG_FLD_SHIFT(MDP_RDMA_TARGET_LINE_FLD_TARGET_LINE_EN),
		cmdq_base, base,
		MDP_RDMA_TARGET_LINE,
		REG_FLD_MASK(MDP_RDMA_TARGET_LINE_FLD_TARGET_LINE_EN));
}

