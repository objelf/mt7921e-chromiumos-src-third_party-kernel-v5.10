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

#include "mtk_drm_crtc.h"
#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_drv.h"
#include "mtk_mdp_rdma.h"
#include "mtk_ethdr.h"

#define DISP_MERGE_ENABLE	0x000
	#define VPP_MERGE_ENABLE BIT(0)
#define DISP_MERGE_RESET	0x004
#define DISP_MERGE_CFG_0	0x010
#define DISP_MERGE_CFG_1	0x014
#define DISP_MERGE_CFG_4	0x020
#define DISP_MERGE_CFG_5	0x024
#define DISP_MERGE_CFG_8	0x030
#define DISP_MERGE_CFG_9	0x034
#define DISP_MERGE_CFG_10	0x038
	#define CFG_10_NO_SWAP 0
	#define CFG_10_RG_SWAP 1
	#define CFG_10_GB_SWAP 2
	#define CFG_10_BR_SWAP 4
	#define CFG_10_RG_GB_SWAP 8
	#define CFG_10_RB_GB_SWAP 10
#define DISP_MERGE_CFG_11	0x03C
#define DISP_MERGE_CFG_12	0x040
	#define CFG12_10_10_1PI_2PO_BUF_MODE 6
	#define CFG12_11_10_1PI_2PO_MERGE 18
#define DISP_MERGE_CFG_13	0x044
#define DISP_MERGE_CFG_14	0x048
#define DISP_MERGE_CFG_15	0x04C
#define DISP_MERGE_CFG_17	0x054
#define DISP_MERGE_CFG_18	0x058
#define DISP_MERGE_CFG_19	0x05C
#define DISP_MERGE_CFG_20	0x060
#define DISP_MERGE_CFG_21	0x064
#define DISP_MERGE_CFG_22	0x068
#define DISP_MERGE_CFG_23	0x06C
#define DISP_MERGE_CFG_24	0x070
#define DISP_MERGE_CFG_25	0x074
#define DISP_MERGE_CFG_26	0x078
#define DISP_MERGE_CFG_27	0x07C
#define DISP_MERGE_CFG_28	0x080
#define DISP_MERGE_CFG_29	0x084
#define DISP_MERGE_MUTE_0	0xF00

/*VDO1 CONFIG*/
#define VDO1_CONFIG_SW0_RST_B 0x1D0
#define VDO1_CONFIG_MERGE0_ASYNC_CFG_WD 0xE30
#define VDO1_CONFIG_MERGE1_ASYNC_CFG_WD 0xE40
#define VDO1_CONFIG_MERGE2_ASYNC_CFG_WD 0xE50
#define VDO1_CONFIG_MERGE3_ASYNC_CFG_WD 0xE60
#define VDO1_CONFIG_VPP2_ASYNC_CFG_WD 0XEC0
#define VDO1_CONFIG_VPP3_ASYNC_CFG_WD 0XED0
#define VDO1_HDR_TOP_CFG 0xD00
	#define VDO1_HDR_TOP_CFG_HDR_ALPHA_SEL_MIXER_IN1 20
	#define VDO1_HDR_TOP_CFG_HDR_ALPHA_SEL_MIXER_IN2 21
	#define VDO1_HDR_TOP_CFG_HDR_ALPHA_SEL_MIXER_IN3 22
	#define VDO1_HDR_TOP_CFG_HDR_ALPHA_SEL_MIXER_IN4 23
#define VDO1_CONFIG_MIXER_IN1_ALPHA 0xD30
#define VDO1_CONFIG_MIXER_IN2_ALPHA 0xD34
#define VDO1_CONFIG_MIXER_IN3_ALPHA 0xD38
#define VDO1_CONFIG_MIXER_IN4_ALPHA 0xD3C

#define MTK_PSEUDO_OVL_LAYER_NUM 4
#define MTK_PSEUDO_OVL_SINGLE_PIPE_MAX_WIDTH 1920
#define VDO1_CONFIG_ALPHA_DEFAULT_VALUE 0x0100
#define MERGE_NUM (PSEUDO_OVL_DISP_MERGE3-PSEUDO_OVL_DISP_MERGE0+1)
#define RDMA_NUM (PSEUDO_OVL_MDP_RDMA7-PSEUDO_OVL_MDP_RDMA0+1)

struct mtk_disp_pseudo_ovl_data {
	unsigned int layer_nr;
};

struct pseudo_ovl_larb_data {
	unsigned int	larb_id;
	struct device	*dev;
};

struct mtk_disp_pseudo_ovl_config_struct {
	unsigned int fmt;
	unsigned int merge_mode;
	unsigned int in_w[2];
	unsigned int out_w[2];
	unsigned int in_h;
	unsigned int out_swap;
	unsigned int spilt_ofs[2];
};

enum mtk_pseudo_ovl_larb {
	PSEUDO_OVL_LARB2,
	PSEUDO_OVL_LARB3,
	PSEUDO_OVL_LARB_NUM
};

enum mtk_pseudo_ovl_comp_id {
	PSEUDO_OVL_MDP_RDMA0,
	PSEUDO_OVL_MDP_RDMA1,
	PSEUDO_OVL_MDP_RDMA2,
	PSEUDO_OVL_MDP_RDMA3,
	PSEUDO_OVL_MDP_RDMA4,
	PSEUDO_OVL_MDP_RDMA5,
	PSEUDO_OVL_MDP_RDMA6,
	PSEUDO_OVL_MDP_RDMA7,
	PSEUDO_OVL_DISP_MERGE0,
	PSEUDO_OVL_DISP_MERGE1,
	PSEUDO_OVL_DISP_MERGE2,
	PSEUDO_OVL_DISP_MERGE3,
	PSEUDO_OVL_ID_MAX
};

enum mtk_pseudo_ovl_index {
	MTK_PSEUDO_OVL_RDMA_BASE = 0,
	MTK_PSEUDO_OVL_L0_LEFT = MTK_PSEUDO_OVL_RDMA_BASE,
	MTK_PSEUDO_OVL_L0_RIGHT,
	MTK_PSEUDO_OVL_L1_LEFT,
	MTK_PSEUDO_OVL_L1_RIGHT,
	MTK_PSEUDO_OVL_L2_LEFT,
	MTK_PSEUDO_OVL_L2_RIGHT,
	MTK_PSEUDO_OVL_L3_LEFT,
	MTK_PSEUDO_OVL_L3_RIGHT,
	MTK_PSEUDO_OVL_MERGE_BASE,
	MTK_PSEUDO_OVL_L0_MERGE = MTK_PSEUDO_OVL_MERGE_BASE,
	MTK_PSEUDO_OVL_L1_MERGE,
	MTK_PSEUDO_OVL_L2_MERGE,
	MTK_PSEUDO_OVL_L3_MERGE
};

static const char * const pseudo_ovl_comp_str[] = {
	"PSEUDO_OVL_MDP_RDMA0",
	"PSEUDO_OVL_MDP_RDMA1",
	"PSEUDO_OVL_MDP_RDMA2",
	"PSEUDO_OVL_MDP_RDMA3",
	"PSEUDO_OVL_MDP_RDMA4",
	"PSEUDO_OVL_MDP_RDMA5",
	"PSEUDO_OVL_MDP_RDMA6",
	"PSEUDO_OVL_MDP_RDMA7",
	"PSEUDO_OVL_DISP_MERGE0",
	"PSEUDO_OVL_DISP_MERGE1",
	"PSEUDO_OVL_DISP_MERGE2",
	"PSEUDO_OVL_DISP_MERGE3",
	"PSEUDO_OVL_ID_MAX"
};

static unsigned int merge_async_offset[] = {
	VDO1_CONFIG_MERGE0_ASYNC_CFG_WD,
	VDO1_CONFIG_MERGE1_ASYNC_CFG_WD,
	VDO1_CONFIG_MERGE2_ASYNC_CFG_WD,
	VDO1_CONFIG_MERGE3_ASYNC_CFG_WD,
};

static unsigned int merge_async_reset_bit[] = {	25, 26, 27, 28};

static unsigned int alpha_source_sel_from_prev_reg[] = {
	VDO1_HDR_TOP_CFG_HDR_ALPHA_SEL_MIXER_IN1,
	VDO1_HDR_TOP_CFG_HDR_ALPHA_SEL_MIXER_IN2,
	VDO1_HDR_TOP_CFG_HDR_ALPHA_SEL_MIXER_IN3,
	VDO1_HDR_TOP_CFG_HDR_ALPHA_SEL_MIXER_IN4,
};

static unsigned int mixer_alpha_value[] = {
	VDO1_CONFIG_MIXER_IN1_ALPHA,
	VDO1_CONFIG_MIXER_IN2_ALPHA,
	VDO1_CONFIG_MIXER_IN3_ALPHA,
	VDO1_CONFIG_MIXER_IN4_ALPHA,
};

struct mtk_pseudo_ovl_comp {
	struct clk *clk;
	void __iomem *regs;
	resource_size_t regs_pa;
	int irq;
	struct device *larb_dev;
	struct device *dev;
	u32 larb_id;
	enum mtk_ddp_comp_id id;
	struct cmdq_client_reg cmdq_base;
};

struct mtk_disp_pseudo_ovl {
	struct cmdq_client_reg top_cmdq_base;
	void __iomem *top_regs;
	resource_size_t top_regs_pa;
	struct mtk_pseudo_ovl_comp pseudo_ovl_comp[PSEUDO_OVL_ID_MAX];
	struct device *larb_dev0;
	u32 larb_id0;
	struct device *larb_dev1;
	u32 larb_id1;
	struct clk *async_clk[MERGE_NUM];
	const struct mtk_disp_pseudo_ovl_data *data;
	struct icc_path *rdma_icc_path[RDMA_NUM];
};

static struct platform_device *pseudo_ovl_dev[PSEUDO_OVL_LARB_NUM] = {NULL};

static bool is_yuv(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
		return true;
	default:
		return false;
	}
}

static int mtk_pseudo_ovl_golden_setting(struct mtk_disp_pseudo_ovl *pseudo_ovl,
			struct cmdq_pkt *handle)
{
	struct mtk_pseudo_ovl_comp *priv_comp = NULL;
	int i;

	/*config rdma gs*/
	for (i = PSEUDO_OVL_MDP_RDMA0; i <= PSEUDO_OVL_MDP_RDMA7; i++) {
		priv_comp =
			&(pseudo_ovl->pseudo_ovl_comp[MTK_PSEUDO_OVL_RDMA_BASE+i]);
		mtk_mdp_rdma_golden_setting(priv_comp->regs, handle,
			&priv_comp->cmdq_base);
	}

	return 0;
}

static void mtk_pseudo_ovl_merge_config(struct mtk_pseudo_ovl_comp *comp,
		struct mtk_disp_pseudo_ovl_config_struct *pseudo_ovl_cfg,
		struct cmdq_pkt *cmdq_pkt)
{
	/* 8'd00 : Use external configure
	 * 8'd01 : CFG_11_11_1PI_1PO_BYPASS
	 * 8'd02 : CFG_11_11_2PI_2PO_BYPASS
	 * 8'd03 : CFG_10_10_2PI_1PO_BYPASS
	 * 8'd04 : CFG_10_10_2PI_2PO_BYPASS
	 * 8'd05 : CFG_10_10_1PI_1PO_BUF_MODE
	 * 8'd06 : CFG_10_10_1PI_2PO_BUF_MODE
	 * 8'd07 : CFG_10_10_2PI_1PO_BUF_MODE
	 * 8'd08 : CFG_10_10_2PI_2PO_BUF_MODE
	 * 8'd09 : CFG_10_01_1PI_1PO_BUF_MODE
	 * 8'd10 : CFG_10_01_2PI_1PO_BUF_MODE
	 * 8'd11 : CFG_01_10_1PI_1PO_BUF_MODE
	 * 8'd12 : CFG_01_10_1PI_2PO_BUF_MODE
	 * 8'd13 : CFG_01_01_1PI_1PO_BUF_MODE
	 * 8'd14 : CFG_10_11_1PI_1PO_SPLIT
	 * 8'd15 : CFG_10_11_2PI_1PO_SPLIT
	 * 8'd16 : CFG_01_11_1PI_1PO_SPLIT
	 * 8'd17 : CFG_11_10_1PI_1PO_MERGE
	 * 8'd18 : CFG_11_10_1PI_2PO_MERGE
	 * 8'd19 : CFG_10_10_1PI_1PO_TO422
	 * 8'd20 : CFG_10_10_1PI_2PO_TO444
	 * 8'd21 : CFG_10_10_2PI_2PO_TO444
	 * else  : CFG_11_11_1PI_1PO_BYPASS
	 */
	switch (pseudo_ovl_cfg->merge_mode) {
	case 1:
	case 2:
		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_0, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_1, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_4, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_5, ~0);

		break;
	case 3:
	case 4:
		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_0, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_4, ~0);

		break;
	case 5:
	case 6:
	case 7:
	case 8:
		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_0, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_4, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_24, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_25, ~0);

		break;
	case 9:
	case 10:
		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_0, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_5, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_24, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_25, ~0);

		break;
	case 11:
	case 12:
		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_1, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_4, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_24, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_25, ~0);

		break;
	case 13:
		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_1, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_5, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_24, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_25, ~0);

		break;
	case 14:

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_0, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_4, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_5, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_20, ~0);

		mtk_ddp_write_mask(cmdq_pkt,0, &comp->cmdq_base, comp->regs,
			DISP_MERGE_CFG_21, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->spilt_ofs[0] << 16 | pseudo_ovl_cfg->out_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_22, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->spilt_ofs[1] << 16 | pseudo_ovl_cfg->out_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_23, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_24, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_25, ~0);

		break;
	case 15:

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_0, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_4, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_5, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_20, ~0);

		mtk_ddp_write_mask(cmdq_pkt, 1, &comp->cmdq_base, comp->regs,
			DISP_MERGE_CFG_21, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->spilt_ofs[0] << 16 | pseudo_ovl_cfg->out_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_22, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->spilt_ofs[1] << 16 | pseudo_ovl_cfg->out_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_23, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_24, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_25, ~0);

		break;
	case 16:

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_1, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_4, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_5, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_20, ~0);

		mtk_ddp_write_mask(cmdq_pkt, 0, &comp->cmdq_base, comp->regs,
			DISP_MERGE_CFG_21, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->spilt_ofs[0] << 16 | pseudo_ovl_cfg->out_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_22, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->spilt_ofs[1] << 16 | pseudo_ovl_cfg->out_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_23, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_24, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_25, ~0);

		break;
	case 17:
	case 18:
		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_0, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_1, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_4, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_24, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_25, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_26, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_27, ~0);

		break;
	case 19:

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_0, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_4, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_24, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_25, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_28, ~0);

		mtk_ddp_write_mask(cmdq_pkt, 0x2, &comp->cmdq_base, comp->regs,
			DISP_MERGE_CFG_29, ~0);

		break;
	case 20:
	case 21:
		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_0, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_4, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_24, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_25, ~0);

		break;
	default:
		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_0, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->in_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_1, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[0]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_4, ~0);

		mtk_ddp_write_mask(cmdq_pkt,
			(pseudo_ovl_cfg->in_h << 16 | pseudo_ovl_cfg->out_w[1]),
			&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_5, ~0);

		break;
	}

	mtk_ddp_write_mask(cmdq_pkt,
		pseudo_ovl_cfg->merge_mode,
		&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_12, ~0);

	mtk_ddp_write_mask(cmdq_pkt,
		pseudo_ovl_cfg->out_swap,
		&comp->cmdq_base, comp->regs, DISP_MERGE_CFG_10, ~0);

	mtk_ddp_write_mask(cmdq_pkt, 1, &comp->cmdq_base, comp->regs,
		DISP_MERGE_ENABLE, ~0);
}

void mtk_pseudo_ovl_layer_on(struct device *dev, unsigned int idx,
		      struct cmdq_pkt *cmdq_pkt)
{
	dev_dbg(dev, "%s-%d\n", __func__, idx);

	mtk_ethdr_layer_on(idx, cmdq_pkt);
}

void mtk_pseudo_ovl_layer_off(struct device *dev, unsigned int idx,
		       struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_pseudo_ovl *pseudo_ovl = dev_get_drvdata(dev);
	struct mtk_pseudo_ovl_comp *priv_comp = NULL;

	dev_dbg(dev, "%s-%d\n", __func__, idx);

	mtk_ethdr_layer_off(idx, cmdq_pkt);

	priv_comp =
		&(pseudo_ovl->pseudo_ovl_comp[MTK_PSEUDO_OVL_MERGE_BASE+idx]);
	mtk_ddp_write_mask(cmdq_pkt, 0x0, &priv_comp->cmdq_base, priv_comp->regs,
		DISP_MERGE_ENABLE, 0x1);

	priv_comp =
		&(pseudo_ovl->pseudo_ovl_comp[MTK_PSEUDO_OVL_RDMA_BASE+2*idx]);
	mtk_mdp_rdma_stop(priv_comp->regs, cmdq_pkt, &priv_comp->cmdq_base);

	priv_comp =
		&(pseudo_ovl->pseudo_ovl_comp[MTK_PSEUDO_OVL_RDMA_BASE+2*idx+1]);
	mtk_mdp_rdma_stop(priv_comp->regs, cmdq_pkt, &priv_comp->cmdq_base);

}

unsigned int mtk_pseudo_ovl_layer_nr(struct device *dev)
{
	struct mtk_disp_pseudo_ovl *pseudo_ovl = dev_get_drvdata(dev);

	return pseudo_ovl->data->layer_nr;
}


void mtk_pseudo_ovl_layer_config(struct device *dev, unsigned int idx,
			   struct mtk_plane_state *state,
			   struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_pseudo_ovl *pseudo_ovl = dev_get_drvdata(dev);
	struct mtk_plane_pending_state *pending = &state->pending;
	struct mtk_disp_pseudo_ovl_config_struct pseudo_ovl_cfg = {0};
	struct mtk_mdp_rdma_cfg rdma_config = {0};
	struct mtk_pseudo_ovl_comp *priv_comp = NULL;
	bool force_output_rgb = true;
	bool use_dual_pipe = false;
	const struct drm_format_info *fmt_info = drm_format_info(pending->format);

	dev_dbg(dev, "%s+ idx:%d, enable:%d, fmt:0x%x\n", __func__, idx,
		pending->enable, pending->format);
	dev_dbg(dev, "addr 0x%lx, fb w:%d, {%d,%d,%d,%d}\n",
		pending->addr, pending->pitch/fmt_info->cpp[0],
		pending->x, pending->y, pending->width, pending->height);

	if (!pending->enable) {
		mtk_ethdr_layer_off(idx, cmdq_pkt);

		priv_comp =
			&(pseudo_ovl->pseudo_ovl_comp[MTK_PSEUDO_OVL_MERGE_BASE+idx]);
		mtk_ddp_write_mask(cmdq_pkt, 0x0, &priv_comp->cmdq_base, priv_comp->regs,
			DISP_MERGE_ENABLE, 0x1);

		priv_comp =
			&(pseudo_ovl->pseudo_ovl_comp[MTK_PSEUDO_OVL_RDMA_BASE+2*idx]);
		mtk_mdp_rdma_stop(priv_comp->regs, cmdq_pkt, &priv_comp->cmdq_base);

		priv_comp =
			&(pseudo_ovl->pseudo_ovl_comp[MTK_PSEUDO_OVL_RDMA_BASE+2*idx+1]);
		mtk_mdp_rdma_stop(priv_comp->regs, cmdq_pkt, &priv_comp->cmdq_base);

		return;
	}

	/*Decide single or dual pipe*/
	if (pending->width > MTK_PSEUDO_OVL_SINGLE_PIPE_MAX_WIDTH)
		use_dual_pipe = true;

	/*Config merge*/
	pseudo_ovl_cfg.fmt = pending->format;
	pseudo_ovl_cfg.out_w[0] = pending->width;
	pseudo_ovl_cfg.in_h = pending->height;
	pseudo_ovl_cfg.out_swap = CFG_10_NO_SWAP;
	if (use_dual_pipe) {
		pseudo_ovl_cfg.merge_mode = CFG12_11_10_1PI_2PO_MERGE;
		pseudo_ovl_cfg.in_w[0] = pending->width/2 + ((pending->width/2) % 2);
		pseudo_ovl_cfg.in_w[1] = pending->width/2 - ((pending->width/2) % 2);
	} else {
		pseudo_ovl_cfg.merge_mode = CFG12_10_10_1PI_2PO_BUF_MODE;
		pseudo_ovl_cfg.in_w[0] = pending->width;
	}
	priv_comp =
	&(pseudo_ovl->pseudo_ovl_comp[MTK_PSEUDO_OVL_MERGE_BASE+idx]);
	mtk_pseudo_ovl_merge_config(priv_comp, &pseudo_ovl_cfg, cmdq_pkt);

	/*Config merge ASYNC WD : W/2*/
	mtk_ddp_write_mask(cmdq_pkt, (pending->height << 16 | (pending->width/2)),
		&pseudo_ovl->top_cmdq_base, pseudo_ovl->top_regs,
		merge_async_offset[idx], ~0);

	/*Config RDMA left tile*/
	rdma_config.addr0 = pending->addr;
	rdma_config.addr1 = 0;
	rdma_config.addr2 = 0;
	rdma_config.x_left = 0;
	rdma_config.y_top = 0;
	/*Temp force convert to YUV for bypass ETHDR*/
	if (is_yuv(pending->format) && force_output_rgb)
		rdma_config.csc_enable = true;
	else
		rdma_config.csc_enable = false;
	rdma_config.profile = RDMA_CSC_FULL709_TO_RGB;
	rdma_config.fmt = pending->format;
	rdma_config.source_width =
				pending->pitch/fmt_info->cpp[0];
	rdma_config.width = pseudo_ovl_cfg.in_w[0];
	rdma_config.height = pending->height;
	rdma_config.encode_type = RDMA_ENCODE_NONE;
	rdma_config.block_size = RDMA_BLOCK_NONE;

	priv_comp =
		&(pseudo_ovl->pseudo_ovl_comp[MTK_PSEUDO_OVL_RDMA_BASE+2*idx]);
	mtk_mdp_rdma_config(priv_comp->regs, &rdma_config, cmdq_pkt,
		&priv_comp->cmdq_base, false);

	/*Config RDMA right tile*/
	rdma_config.x_left = pseudo_ovl_cfg.in_w[0];
	rdma_config.width = pseudo_ovl_cfg.in_w[1];
	priv_comp =
		&(pseudo_ovl->pseudo_ovl_comp[MTK_PSEUDO_OVL_RDMA_BASE+2*idx+1]);
	mtk_mdp_rdma_config(priv_comp->regs, &rdma_config, cmdq_pkt,
		&priv_comp->cmdq_base, false);

	/*Config VDO1 alpha source*/
	if (!fmt_info->has_alpha) {
		mtk_ddp_write_mask(cmdq_pkt, 1<<alpha_source_sel_from_prev_reg[idx],
			&pseudo_ovl->top_cmdq_base, pseudo_ovl->top_regs,
			VDO1_HDR_TOP_CFG, 1<<alpha_source_sel_from_prev_reg[idx]);

	} else {
		mtk_ddp_write_mask(cmdq_pkt, 0, &pseudo_ovl->top_cmdq_base,
			pseudo_ovl->top_regs, VDO1_HDR_TOP_CFG,
			1<<alpha_source_sel_from_prev_reg[idx]);
	}

	mtk_ddp_write_mask(cmdq_pkt,
		VDO1_CONFIG_ALPHA_DEFAULT_VALUE<<16|VDO1_CONFIG_ALPHA_DEFAULT_VALUE,
		&pseudo_ovl->top_cmdq_base, pseudo_ovl->top_regs,
		mixer_alpha_value[idx], ~0);

	/*enable merge*/
	priv_comp =
		&(pseudo_ovl->pseudo_ovl_comp[MTK_PSEUDO_OVL_MERGE_BASE+idx]);

	if (pending->enable) {
		mtk_ddp_write_mask(cmdq_pkt, 0x1, &priv_comp->cmdq_base,
			priv_comp->regs, DISP_MERGE_ENABLE, 0x1);

		mtk_ddp_write_mask(cmdq_pkt, 0x0, &priv_comp->cmdq_base,
			priv_comp->regs, DISP_MERGE_MUTE_0, 0x1);

	} else {
		mtk_ddp_write_mask(cmdq_pkt, 0x1, &priv_comp->cmdq_base,
			priv_comp->regs, DISP_MERGE_MUTE_0, 0x1);
		mtk_ddp_write_mask(cmdq_pkt, 0x0, &priv_comp->cmdq_base,
			priv_comp->regs, DISP_MERGE_ENABLE, 0x1);
	}

	/*Enable left tile RDMA*/
	priv_comp =
		&(pseudo_ovl->pseudo_ovl_comp[MTK_PSEUDO_OVL_RDMA_BASE+2*idx]);
	if (pending->enable)
		mtk_mdp_rdma_start(priv_comp->regs, cmdq_pkt, &priv_comp->cmdq_base);
	else
		mtk_mdp_rdma_stop(priv_comp->regs, cmdq_pkt, &priv_comp->cmdq_base);

	/*Enable right tile RDMA*/
	priv_comp =
	&(pseudo_ovl->pseudo_ovl_comp[MTK_PSEUDO_OVL_RDMA_BASE+2*idx+1]);
	if (pending->enable && use_dual_pipe)
		mtk_mdp_rdma_start(priv_comp->regs, cmdq_pkt, &priv_comp->cmdq_base);
	else
		mtk_mdp_rdma_stop(priv_comp->regs, cmdq_pkt, &priv_comp->cmdq_base);


	mtk_ethdr_layer_config(idx, state, cmdq_pkt);
	if (pending->enable)
		mtk_pseudo_ovl_layer_on(dev, idx, cmdq_pkt);
}

void mtk_pseudo_ovl_config(struct device *dev, unsigned int w,
		    unsigned int h, unsigned int vrefresh,
		    unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_pseudo_ovl *pseudo_ovl = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	/*Config VPP ASYNC WD: W*/
	mtk_ddp_write_mask(cmdq_pkt, (h<<16 | w/2), &pseudo_ovl->top_cmdq_base,
			pseudo_ovl->top_regs, VDO1_CONFIG_VPP2_ASYNC_CFG_WD, ~0);
	mtk_ddp_write_mask(cmdq_pkt, (h<<16 | w/2), &pseudo_ovl->top_cmdq_base,
			pseudo_ovl->top_regs, VDO1_CONFIG_VPP3_ASYNC_CFG_WD, ~0);

	/*Config merge 0~4 ASYNC WD : W/2*/
	mtk_ddp_write_mask(cmdq_pkt, (h<<16 | w/2), &pseudo_ovl->top_cmdq_base,
			pseudo_ovl->top_regs, VDO1_CONFIG_MERGE0_ASYNC_CFG_WD, ~0);
	mtk_ddp_write_mask(cmdq_pkt, (h<<16 | w/2), &pseudo_ovl->top_cmdq_base,
			pseudo_ovl->top_regs, VDO1_CONFIG_MERGE1_ASYNC_CFG_WD, ~0);
	mtk_ddp_write_mask(cmdq_pkt, (h<<16 | w/2), &pseudo_ovl->top_cmdq_base,
			pseudo_ovl->top_regs, VDO1_CONFIG_MERGE2_ASYNC_CFG_WD, ~0);
	mtk_ddp_write_mask(cmdq_pkt, (h<<16 | w/2), &pseudo_ovl->top_cmdq_base,
			pseudo_ovl->top_regs, VDO1_CONFIG_MERGE3_ASYNC_CFG_WD, ~0);

	/*golden setting*/
	mtk_pseudo_ovl_golden_setting(pseudo_ovl, cmdq_pkt);
}

void mtk_pseudo_ovl_start(struct device *dev)
{
	dev_dbg(dev, "%s\n", __func__);
}

void mtk_pseudo_ovl_stop(struct device *dev)
{
	u32 i;
	struct mtk_pseudo_ovl_comp *priv_comp = NULL;
	unsigned int reg;
	struct mtk_disp_pseudo_ovl *pseudo_ovl = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	for (i = 0; i < MTK_PSEUDO_OVL_LAYER_NUM; i++) {
		/*Disable left tile RDMA*/
		priv_comp =
		&(pseudo_ovl->pseudo_ovl_comp[MTK_PSEUDO_OVL_RDMA_BASE+2*i]);
		mtk_mdp_rdma_stop(priv_comp->regs, NULL, &priv_comp->cmdq_base);

		/*Disable right tile RDMA*/
		priv_comp =
		&(pseudo_ovl->pseudo_ovl_comp[MTK_PSEUDO_OVL_RDMA_BASE+2*i+1]);
		mtk_mdp_rdma_stop(priv_comp->regs, NULL, &priv_comp->cmdq_base);

		/*disable merge*/
		priv_comp =
		&(pseudo_ovl->pseudo_ovl_comp[MTK_PSEUDO_OVL_MERGE_BASE+i]);
		reg = readl(priv_comp->regs + DISP_MERGE_ENABLE);
		reg = reg & ~VPP_MERGE_ENABLE;
		writel_relaxed(reg, priv_comp->regs + DISP_MERGE_ENABLE);

		/*reset async*/
		mtk_ddp_write_mask(NULL, 0, &pseudo_ovl->top_cmdq_base,
				pseudo_ovl->top_regs, VDO1_CONFIG_SW0_RST_B,
				BIT(merge_async_reset_bit[i]));
		mtk_ddp_write_mask(NULL, BIT(merge_async_reset_bit[i]),
				&pseudo_ovl->top_cmdq_base,
				pseudo_ovl->top_regs, VDO1_CONFIG_SW0_RST_B,
				BIT(merge_async_reset_bit[i]));
	}
}

int mtk_pseudo_ovl_clk_enable(struct device *dev)
{
	u32 i;
	int ret;
	struct mtk_disp_pseudo_ovl *pseudo_ovl = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	pm_runtime_get_sync(dev);
	pm_runtime_get_sync(&pseudo_ovl_dev[PSEUDO_OVL_LARB2]->dev);
	pm_runtime_get_sync(&pseudo_ovl_dev[PSEUDO_OVL_LARB3]->dev);

	for (i = PSEUDO_OVL_MDP_RDMA0; i < PSEUDO_OVL_ID_MAX; i++)
		ret = clk_prepare_enable(pseudo_ovl->pseudo_ovl_comp[i].clk);

	/*ASYNC clk prepare*/
	for (i = 0; i < 4; i++) {
		if (pseudo_ovl->async_clk[i]) {
			ret = clk_prepare_enable(pseudo_ovl->async_clk[i]);
			if (ret)
				dev_err(dev, "clk prepare failed for merge%d async\n", i);
		}
	}

	return ret;
}

void mtk_pseudo_ovl_clk_disable(struct device *dev)
{
	u32 i;
	struct mtk_disp_pseudo_ovl *pseudo_ovl = dev_get_drvdata(dev);

	dev_info(dev, "%s\n", __func__);

	for (i = PSEUDO_OVL_MDP_RDMA0 ; i < PSEUDO_OVL_ID_MAX ; i++)
		clk_disable_unprepare(pseudo_ovl->pseudo_ovl_comp[i].clk);

	/*ASYNC clk unprepare*/
	for (i = 0; i < 4; i++) {
		if (pseudo_ovl->async_clk[i])
			clk_disable_unprepare(pseudo_ovl->async_clk[i]);
	}

	pm_runtime_put(&pseudo_ovl_dev[PSEUDO_OVL_LARB2]->dev);
	pm_runtime_put(&pseudo_ovl_dev[PSEUDO_OVL_LARB3]->dev);
	pm_runtime_put(dev);
}

static int mtk_disp_pseudo_ovl_bind(struct device *dev, struct device *master,
	void *data)
{
	return 0;
}

static void mtk_disp_pseudo_ovl_unbind(struct device *dev,
	struct device *master,
	void *data)
{

}

static const struct component_ops mtk_disp_pseudo_ovl_component_ops = {
	.bind	= mtk_disp_pseudo_ovl_bind,
	.unbind = mtk_disp_pseudo_ovl_unbind,
};

static int mtk_disp_pseudo_ovl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_disp_pseudo_ovl *priv;
	enum mtk_ddp_comp_id comp_id;
	int ret;
	int i;
	struct resource res;
	struct device_node *larb_node = NULL;
	struct platform_device *larb_pdev = NULL;
	unsigned int larb_id;
	char clk_name[30] = {'\0'};

	pr_info("%s+\n", __func__);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	comp_id = mtk_ddp_comp_get_id(dev->of_node, MTK_DISP_PSEUDO_OVL);
	if ((int)comp_id < 0) {
		dev_dbg(dev, "Failed to identify by alias: %d\n", comp_id);
		return comp_id;
	}

	/* handle larb resources */
	priv->larb_dev0 = NULL;
	larb_node = of_parse_phandle(dev->of_node, "mediatek,larb", 0);
	if (larb_node) {
		larb_pdev = of_find_device_by_node(larb_node);
		if (larb_pdev)
			priv->larb_dev0 = &larb_pdev->dev;
		of_node_put(larb_node);
	}

	if (!priv->larb_dev0)
		return 0;

	ret = of_property_read_u32_index(dev->of_node,
		"mediatek,smi-id", 0, &larb_id);
	if (ret) {
		dev_notice(priv->larb_dev0, "read smi-id failed:%d\n", ret);
		return 0;
	}
	priv->larb_id0 = larb_id;

	dev_dbg(dev, "%s- use larb smi-id:%d\n", __func__, priv->larb_id0);

	/*Get Top Reg*/
	if (of_address_to_resource(dev->of_node, PSEUDO_OVL_ID_MAX, &res) != 0) {
		dev_notice(dev, "No Top resource\n");
		return -EINVAL;
	}
	priv->top_regs_pa = res.start;
	priv->top_regs = of_iomap(dev->of_node, PSEUDO_OVL_ID_MAX);

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->top_cmdq_base, PSEUDO_OVL_ID_MAX);

	if (ret)
		dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif

	//set private comp info
	for (i = 0; i < PSEUDO_OVL_ID_MAX; i++) {
		priv->pseudo_ovl_comp[i].id = DDP_COMPONENT_PSEUDO_OVL;
		priv->pseudo_ovl_comp[i].dev = dev;

		/* get the clk for each pseudo ovl comp in the device node */
		priv->pseudo_ovl_comp[i].clk = of_clk_get(dev->of_node, i);
		if (IS_ERR(priv->pseudo_ovl_comp[i].clk)) {
			priv->pseudo_ovl_comp[i].clk = NULL;
			dev_dbg(dev, "comp:%d get priv comp %s clock %d fail!\n",
			comp_id, pseudo_ovl_comp_str[i]);
		}

		if (of_address_to_resource(dev->of_node, i, &res) != 0) {
			dev_notice(dev, "Missing reg in for component %s\n",
				pseudo_ovl_comp_str[i]);
			return -EINVAL;
		}
		priv->pseudo_ovl_comp[i].regs_pa = res.start;
		priv->pseudo_ovl_comp[i].regs = of_iomap(dev->of_node, i);
		priv->pseudo_ovl_comp[i].irq = of_irq_get(dev->of_node, i);

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
		ret = cmdq_dev_get_client_reg(dev,
					&priv->pseudo_ovl_comp[i].cmdq_base,
					i);
		if (ret)
			dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif

		dev_dbg(dev, "[DRM]regs_pa:0x%lx, regs:0x%p, node:%s\n",
			(unsigned long)priv->pseudo_ovl_comp[i].regs_pa,
			priv->pseudo_ovl_comp[i].regs, pseudo_ovl_comp_str[i]);
	}

	for (i = 0 ; i < MERGE_NUM; i++) {
		sprintf(clk_name, "vdo1_merge%d_async", i);
		priv->async_clk[i] = devm_clk_get(dev, clk_name);

		if (IS_ERR(priv->async_clk[i])) {
			ret = PTR_ERR(priv->async_clk[i]);
			dev_dbg(dev, "No merge %d async clock: %d\n", i, ret);
			priv->async_clk[i] = NULL;
		}
	}

	priv->data = of_device_get_match_data(dev);

	platform_set_drvdata(pdev, priv);

	pm_runtime_enable(dev);

	ret = component_add(dev, &mtk_disp_pseudo_ovl_component_ops);
	if (ret != 0) {
		dev_notice(dev, "Failed to add component: %d\n", ret);
		pm_runtime_disable(dev);
	}
	pr_info("%s-\n", __func__);
	return ret;
}

static int mtk_disp_pseudo_ovl_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_pseudo_ovl_component_ops);

	pm_runtime_disable(&pdev->dev);
	return 0;
}

static const struct mtk_disp_pseudo_ovl_data mt8195_pseudo_ovl_driver_data = {
	.layer_nr = 4,
};

static const struct of_device_id mtk_disp_pseudo_ovl_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8195-disp-pseudo-ovl",
	  .data = &mt8195_pseudo_ovl_driver_data},
	{},
};

MODULE_DEVICE_TABLE(of, mtk_disp_pseudo_ovl_driver_dt_match);

struct platform_driver mtk_disp_pseudo_ovl_driver = {
	.probe = mtk_disp_pseudo_ovl_probe,
	.remove = mtk_disp_pseudo_ovl_remove,
	.driver = {

			.name = "mediatek-disp-pseudo-ovl",
			.owner = THIS_MODULE,
			.of_match_table = mtk_disp_pseudo_ovl_driver_dt_match,
		},
};

static int mtk_pseudo_ovl_larb_probe(struct platform_device *pdev)
{
	struct device           *dev = &pdev->dev;
	struct pseudo_ovl_larb_data   *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(*dev), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret =
	of_property_read_u32(dev->of_node, "mediatek,larb-id", &data->larb_id);
	if (ret != 0) {
		dev_notice(dev, "[%s] failed to find larb-id\n", __func__);
		return ret;
	}
	platform_set_drvdata(pdev, data);

	if (data->larb_id == 2)
		pseudo_ovl_dev[PSEUDO_OVL_LARB2] = pdev;
	else if (data->larb_id == 3)
		pseudo_ovl_dev[PSEUDO_OVL_LARB3] = pdev;

	pm_runtime_enable(dev);
	dev_info(dev, "%s enable larb %d\n", __func__, data->larb_id);
	return ret;
}
static int mtk_pseudo_ovl_larb_remove(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "%s disable larb\n", __func__);
	return 0;
}

static const struct of_device_id mtk_pseudo_ovl_larb_match[] = {
	{.compatible = "mediatek,mt8195-pseudo-ovl-larb",},
	{},
};

MODULE_DEVICE_TABLE(of, mtk_pseudo_ovl_larb_match);

static struct platform_driver mtk_pseudo_ovl_larb_driver = {
	.probe  = mtk_pseudo_ovl_larb_probe,
	.remove = mtk_pseudo_ovl_larb_remove,
	.driver = {
		.name   = "mtk-pseudo-ovl-larb",
		.of_match_table = mtk_pseudo_ovl_larb_match,
	},
};
module_platform_driver(mtk_pseudo_ovl_larb_driver);

