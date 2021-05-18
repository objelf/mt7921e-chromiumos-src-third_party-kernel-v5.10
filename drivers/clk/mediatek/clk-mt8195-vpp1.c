// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2020 MediaTek Inc.
// Author: Weiyi Lu <weiyi.lu@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8195-clk.h>

static const struct mtk_gate_regs vpp10_cg_regs = {
	.set_ofs = 0x104,
	.clr_ofs = 0x108,
	.sta_ofs = 0x100,
};

static const struct mtk_gate_regs vpp11_cg_regs = {
	.set_ofs = 0x114,
	.clr_ofs = 0x118,
	.sta_ofs = 0x110,
};

#define GATE_VPP10(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &vpp10_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_VPP11(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &vpp11_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate vpp1_clks[] = {
	/* VPP10 */
	GATE_VPP10(CLK_VPP1_SVPP1_MDP_OVL, "vpp1_svpp1_mdp_ovl", "vpp_sel", 0),
	GATE_VPP10(CLK_VPP1_SVPP1_MDP_TCC, "vpp1_svpp1_mdp_tcc", "vpp_sel", 1),
	GATE_VPP10(CLK_VPP1_SVPP1_MDP_WROT, "vpp1_svpp1_mdp_wrot", "vpp_sel", 2),
	GATE_VPP10(CLK_VPP1_SVPP1_VPP_PAD, "vpp1_svpp1_vpp_pad", "vpp_sel", 3),
	GATE_VPP10(CLK_VPP1_SVPP2_MDP_WROT, "vpp1_svpp2_mdp_wrot", "vpp_sel", 4),
	GATE_VPP10(CLK_VPP1_SVPP2_VPP_PAD, "vpp1_svpp2_vpp_pad", "vpp_sel", 5),
	GATE_VPP10(CLK_VPP1_SVPP3_MDP_WROT, "vpp1_svpp3_mdp_wrot", "vpp_sel", 6),
	GATE_VPP10(CLK_VPP1_SVPP3_VPP_PAD, "vpp1_svpp3_vpp_pad", "vpp_sel", 7),
	GATE_VPP10(CLK_VPP1_SVPP1_MDP_RDMA, "vpp1_svpp1_mdp_rdma", "vpp_sel", 8),
	GATE_VPP10(CLK_VPP1_SVPP1_MDP_FG, "vpp1_svpp1_mdp_fg", "vpp_sel", 9),
	GATE_VPP10(CLK_VPP1_SVPP2_MDP_RDMA, "vpp1_svpp2_mdp_rdma", "vpp_sel", 10),
	GATE_VPP10(CLK_VPP1_SVPP2_MDP_FG, "vpp1_svpp2_mdp_fg", "vpp_sel", 11),
	GATE_VPP10(CLK_VPP1_SVPP3_MDP_RDMA, "vpp1_svpp3_mdp_rdma", "vpp_sel", 12),
	GATE_VPP10(CLK_VPP1_SVPP3_MDP_FG, "vpp1_svpp3_mdp_fg", "vpp_sel", 13),
	GATE_VPP10(CLK_VPP1_VPP_SPLIT, "vpp1_vpp_split", "vpp_sel", 14),
	GATE_VPP10(CLK_VPP1_SVPP2_VDO0_DL_RELAY, "vpp1_svpp2_vdo0_dl_relay", "vpp_sel", 15),
	GATE_VPP10(CLK_VPP1_SVPP1_MDP_TDSHP, "vpp1_svpp1_mdp_tdshp", "vpp_sel", 16),
	GATE_VPP10(CLK_VPP1_SVPP1_MDP_COLOR, "vpp1_svpp1_mdp_color", "vpp_sel", 17),
	GATE_VPP10(CLK_VPP1_SVPP3_VDO1_DL_RELAY, "vpp1_svpp3_vdo1_dl_relay", "vpp_sel", 18),
	GATE_VPP10(CLK_VPP1_SVPP2_VPP_MERGE, "vpp1_svpp2_vpp_merge", "vpp_sel", 19),
	GATE_VPP10(CLK_VPP1_SVPP2_MDP_COLOR, "vpp1_svpp2_mdp_color", "vpp_sel", 20),
	GATE_VPP10(CLK_VPP1_VPPSYS1_GALS, "vpp1_vppsys1_gals", "vpp_sel", 21),
	GATE_VPP10(CLK_VPP1_SVPP3_VPP_MERGE, "vpp1_svpp3_vpp_merge", "vpp_sel", 22),
	GATE_VPP10(CLK_VPP1_SVPP3_MDP_COLOR, "vpp1_svpp3_mdp_color", "vpp_sel", 23),
	GATE_VPP10(CLK_VPP1_VPPSYS1_LARB, "vpp1_vppsys1_larb", "vpp_sel", 24),
	GATE_VPP10(CLK_VPP1_SVPP1_MDP_RSZ, "vpp1_svpp1_mdp_rsz", "vpp_sel", 25),
	GATE_VPP10(CLK_VPP1_SVPP1_MDP_HDR, "vpp1_svpp1_mdp_hdr", "vpp_sel", 26),
	GATE_VPP10(CLK_VPP1_SVPP1_MDP_AAL, "vpp1_svpp1_mdp_aal", "vpp_sel", 27),
	GATE_VPP10(CLK_VPP1_SVPP2_MDP_HDR, "vpp1_svpp2_mdp_hdr", "vpp_sel", 28),
	GATE_VPP10(CLK_VPP1_SVPP2_MDP_AAL, "vpp1_svpp2_mdp_aal", "vpp_sel", 29),
	GATE_VPP10(CLK_VPP1_DL_ASYNC, "vpp1_dl_async", "vpp_sel", 30),
	GATE_VPP10(CLK_VPP1_LARB5_FAKE_ENG, "vpp1_larb5_fake_eng", "vpp_sel", 31),
	/* VPP11 */
	GATE_VPP11(CLK_VPP1_SVPP3_MDP_HDR, "vpp1_svpp3_mdp_hdr", "vpp_sel", 0),
	GATE_VPP11(CLK_VPP1_SVPP3_MDP_AAL, "vpp1_svpp3_mdp_aal", "vpp_sel", 1),
	GATE_VPP11(CLK_VPP1_SVPP2_VDO1_DL_RELAY, "vpp1_svpp2_vdo1_dl_relay", "vpp_sel", 2),
	GATE_VPP11(CLK_VPP1_LARB6_FAKE_ENG, "vpp1_larb6_fake_eng", "vpp_sel", 3),
	GATE_VPP11(CLK_VPP1_SVPP2_MDP_RSZ, "vpp1_svpp2_mdp_rsz", "vpp_sel", 4),
	GATE_VPP11(CLK_VPP1_SVPP3_MDP_RSZ, "vpp1_svpp3_mdp_rsz", "vpp_sel", 5),
	GATE_VPP11(CLK_VPP1_SVPP3_VDO0_DL_RELAY, "vpp1_svpp3_vdo0_dl_relay", "vpp_sel", 6),
	GATE_VPP11(CLK_VPP1_DISP_MUTEX, "vpp1_disp_mutex", "vpp_sel", 7),
	GATE_VPP11(CLK_VPP1_SVPP2_MDP_TDSHP, "vpp1_svpp2_mdp_tdshp", "vpp_sel", 8),
	GATE_VPP11(CLK_VPP1_SVPP3_MDP_TDSHP, "vpp1_svpp3_mdp_tdshp", "vpp_sel", 9),
	GATE_VPP11(CLK_VPP1_VPP0_DL1_RELAY, "vpp1_vpp0_dl1_relay", "vpp_sel", 10),
	GATE_VPP11(CLK_VPP1_HDMI_META, "vpp1_hdmi_meta", "hdmirx_p", 11),
	GATE_VPP11(CLK_VPP1_VPP_SPLIT_HDMI, "vpp1_vpp_split_hdmi", "hdmirx_p", 12),
	GATE_VPP11(CLK_VPP1_DGI_IN, "vpp1_dgi_in", "in_dgi", 13),
	GATE_VPP11(CLK_VPP1_DGI_OUT, "vpp1_dgi_out", "dgi_out_sel", 14),
	GATE_VPP11(CLK_VPP1_VPP_SPLIT_DGI, "vpp1_vpp_split_dgi", "dgi_out_sel", 15),
	GATE_VPP11(CLK_VPP1_VPP0_DL_ASYNC, "vpp1_vpp0_dl_async", "vpp_sel", 16),
	GATE_VPP11(CLK_VPP1_VPP0_DL_RELAY, "vpp1_vpp0_dl_relay", "vpp_sel", 17),
	GATE_VPP11(CLK_VPP1_VPP_SPLIT_26M, "vpp1_vpp_split_26m", "clk26m", 26),
};

static const struct mtk_clk_desc vpp1_desc = {
	.clks = vpp1_clks,
	.num_clks = ARRAY_SIZE(vpp1_clks),
};

static const struct of_device_id of_match_clk_mt8195_vpp1[] = {
	{
		.compatible = "mediatek,mt8195-vppsys1",
		.data = &vpp1_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8195_vpp1_drv = {
	.probe = mtk_clk_simple_probe,
	.driver = {
		.name = "clk-mt8195-vpp1",
		.of_match_table = of_match_clk_mt8195_vpp1,
	},
};

builtin_platform_driver(clk_mt8195_vpp1_drv);
