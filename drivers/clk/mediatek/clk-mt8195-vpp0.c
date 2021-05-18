// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2020 MediaTek Inc.
// Author: Weiyi Lu <weiyi.lu@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8195-clk.h>

static const struct mtk_gate_regs vpp00_cg_regs = {
	.set_ofs = 0x24,
	.clr_ofs = 0x28,
	.sta_ofs = 0x20,
};

static const struct mtk_gate_regs vpp01_cg_regs = {
	.set_ofs = 0x30,
	.clr_ofs = 0x34,
	.sta_ofs = 0x2c,
};

static const struct mtk_gate_regs vpp02_cg_regs = {
	.set_ofs = 0x3c,
	.clr_ofs = 0x40,
	.sta_ofs = 0x38,
};

#define GATE_VPP00(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &vpp00_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_VPP01(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &vpp01_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_VPP02(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &vpp02_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate vpp0_clks[] = {
	/* VPP00 */
	GATE_VPP00(CLK_VPP0_MDP_FG, "vpp0_mdp_fg", "vpp_sel", 1),
	GATE_VPP00(CLK_VPP0_STITCH, "vpp0_stitch", "vpp_sel", 2),
	GATE_VPP00(CLK_VPP0_PADDING, "vpp0_padding", "vpp_sel", 7),
	GATE_VPP00(CLK_VPP0_MDP_TCC, "vpp0_mdp_tcc", "vpp_sel", 8),
	GATE_VPP00(CLK_VPP0_WARP0_ASYNC_TX, "vpp0_warp0_async_tx", "vpp_sel", 10),
	GATE_VPP00(CLK_VPP0_WARP1_ASYNC_TX, "vpp0_warp1_async_tx", "vpp_sel", 11),
	GATE_VPP00(CLK_VPP0_MUTEX, "vpp0_mutex", "vpp_sel", 13),
	GATE_VPP00(CLK_VPP0_VPP02VPP1_RELAY, "vpp0_vpp02vpp1_relay", "vpp_sel", 14),
	GATE_VPP00(CLK_VPP0_VPP12VPP0_ASYNC, "vpp0_vpp12vpp0_async", "vpp_sel", 15),
	GATE_VPP00(CLK_VPP0_MMSYSRAM_TOP, "vpp0_mmsysram_top", "vpp_sel", 16),
	GATE_VPP00(CLK_VPP0_MDP_AAL, "vpp0_mdp_aal", "vpp_sel", 17),
	GATE_VPP00(CLK_VPP0_MDP_RSZ, "vpp0_mdp_rsz", "vpp_sel", 18),
	/* VPP01 */
	GATE_VPP01(CLK_VPP0_SMI_COMMON, "vpp0_smi_common", "vpp_sel", 0),
	GATE_VPP01(CLK_VPP0_GALS_VDO0_LARB0, "vpp0_gals_vdo0_larb0", "vpp_sel", 1),
	GATE_VPP01(CLK_VPP0_GALS_VDO0_LARB1, "vpp0_gals_vdo0_larb1", "vpp_sel", 2),
	GATE_VPP01(CLK_VPP0_GALS_VENCSYS, "vpp0_gals_vencsys", "vpp_sel", 3),
	GATE_VPP01(CLK_VPP0_GALS_VENCSYS_CORE1, "vpp0_gals_vencsys_core1", "vpp_sel", 4),
	GATE_VPP01(CLK_VPP0_GALS_INFRA, "vpp0_gals_infra", "vpp_sel", 5),
	GATE_VPP01(CLK_VPP0_GALS_CAMSYS, "vpp0_gals_camsys", "vpp_sel", 6),
	GATE_VPP01(CLK_VPP0_GALS_VPP1_LARB5, "vpp0_gals_vpp1_larb5", "vpp_sel", 7),
	GATE_VPP01(CLK_VPP0_GALS_VPP1_LARB6, "vpp0_gals_vpp1_larb6", "vpp_sel", 8),
	GATE_VPP01(CLK_VPP0_SMI_REORDER, "vpp0_smi_reorder", "vpp_sel", 9),
	GATE_VPP01(CLK_VPP0_SMI_IOMMU, "vpp0_smi_iommu", "vpp_sel", 10),
	GATE_VPP01(CLK_VPP0_GALS_IMGSYS_CAMSYS, "vpp0_gals_imgsys_camsys", "vpp_sel", 11),
	GATE_VPP01(CLK_VPP0_MDP_RDMA, "vpp0_mdp_rdma", "vpp_sel", 12),
	GATE_VPP01(CLK_VPP0_MDP_WROT, "vpp0_mdp_wrot", "vpp_sel", 13),
	GATE_VPP01(CLK_VPP0_GALS_EMI0_EMI1, "vpp0_gals_emi0_emi1", "vpp_sel", 16),
	GATE_VPP01(CLK_VPP0_SMI_SUB_COMMON_REORDER, "vpp0_smi_sub_common_reorder", "vpp_sel", 17),
	GATE_VPP01(CLK_VPP0_SMI_RSI, "vpp0_smi_rsi", "vpp_sel", 18),
	GATE_VPP01(CLK_VPP0_SMI_COMMON_LARB4, "vpp0_smi_common_larb4", "vpp_sel", 19),
	GATE_VPP01(CLK_VPP0_GALS_VDEC_VDEC_CORE1, "vpp0_gals_vdec_vdec_core1", "vpp_sel", 20),
	GATE_VPP01(CLK_VPP0_GALS_VPP1_WPE, "vpp0_gals_vpp1_wpe", "vpp_sel", 21),
	GATE_VPP01(CLK_VPP0_GALS_VDO0_VDO1_VENCSYS_CORE1, "vpp0_gals_vdo0_vdo1_vencsys_core1",
		"vpp_sel", 22),
	GATE_VPP01(CLK_VPP0_FAKE_ENG, "vpp0_fake_eng", "vpp_sel", 23),
	GATE_VPP01(CLK_VPP0_MDP_HDR, "vpp0_mdp_hdr", "vpp_sel", 24),
	GATE_VPP01(CLK_VPP0_MDP_TDSHP, "vpp0_mdp_tdshp", "vpp_sel", 25),
	GATE_VPP01(CLK_VPP0_MDP_COLOR, "vpp0_mdp_color", "vpp_sel", 26),
	GATE_VPP01(CLK_VPP0_MDP_OVL, "vpp0_mdp_ovl", "vpp_sel", 27),
	/* VPP02 */
	GATE_VPP02(CLK_VPP0_WARP0_RELAY, "vpp0_warp0_relay", "wpe_vpp_sel", 0),
	GATE_VPP02(CLK_VPP0_WARP0_MDP_DL_ASYNC, "vpp0_warp0_mdp_dl_async", "wpe_vpp_sel", 1),
	GATE_VPP02(CLK_VPP0_WARP1_RELAY, "vpp0_warp1_relay", "wpe_vpp_sel", 2),
	GATE_VPP02(CLK_VPP0_WARP1_MDP_DL_ASYNC, "vpp0_warp1_mdp_dl_async", "wpe_vpp_sel", 3),
};

static const struct mtk_clk_desc vpp0_desc = {
	.clks = vpp0_clks,
	.num_clks = ARRAY_SIZE(vpp0_clks),
};

static const struct of_device_id of_match_clk_mt8195_vpp0[] = {
	{
		.compatible = "mediatek,mt8195-vppsys0",
		.data = &vpp0_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8195_vpp0_drv = {
	.probe = mtk_clk_simple_probe,
	.driver = {
		.name = "clk-mt8195-vpp0",
		.of_match_table = of_match_clk_mt8195_vpp0,
	},
};

builtin_platform_driver(clk_mt8195_vpp0_drv);
