// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2020 MediaTek Inc.
// Author: Weiyi Lu <weiyi.lu@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8195-clk.h>

static const struct mtk_gate_regs nna0_cg_regs = {
	.set_ofs = 0x104,
	.clr_ofs = 0x104,
	.sta_ofs = 0x104,
};

static const struct mtk_gate_regs nna1_cg_regs = {
	.set_ofs = 0x110,
	.clr_ofs = 0x110,
	.sta_ofs = 0x110,
};

static const struct mtk_gate_regs nna2_cg_regs = {
	.set_ofs = 0x90,
	.clr_ofs = 0x90,
	.sta_ofs = 0x90,
};

static const struct mtk_gate_regs nna3_cg_regs = {
	.set_ofs = 0x94,
	.clr_ofs = 0x94,
	.sta_ofs = 0x94,
};

static const struct mtk_gate_regs nna4_cg_regs = {
	.set_ofs = 0x98,
	.clr_ofs = 0x98,
	.sta_ofs = 0x98,
};

static const struct mtk_gate_regs nna5_cg_regs = {
	.set_ofs = 0x9c,
	.clr_ofs = 0x9c,
	.sta_ofs = 0x9c,
};

static const struct mtk_gate_regs nna6_cg_regs = {
	.set_ofs = 0xa0,
	.clr_ofs = 0xa0,
	.sta_ofs = 0xa0,
};

static const struct mtk_gate_regs nna7_cg_regs = {
	.set_ofs = 0xa4,
	.clr_ofs = 0xa4,
	.sta_ofs = 0xa4,
};

#define GATE_NNA0(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &nna0_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

#define GATE_NNA1(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &nna1_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

#define GATE_NNA2(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &nna2_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

#define GATE_NNA3(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &nna3_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

#define GATE_NNA4(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &nna4_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

#define GATE_NNA5(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &nna5_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

#define GATE_NNA6(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &nna6_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

#define GATE_NNA7(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &nna7_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate nna_clks[] = {
	/* NNA0 */
	GATE_NNA0(CLK_NNA_F26M, "nna_f26m", "clk26m", 0),
	/* NNA1 */
	GATE_NNA1(CLK_NNA_AXI, "nna_axi", "axi_sel", 0),
	/* NNA2 */
	GATE_NNA2(CLK_NNA_NNA0, "nna_nna0", "nna0_sel", 0),
	/* NNA3 */
	GATE_NNA3(CLK_NNA_NNA1, "nna_nna1", "nna0_sel", 0),
	/* NNA4 */
	GATE_NNA4(CLK_NNA_NNA0_EMI, "nna_nna0_emi", "mem_466m", 0),
	GATE_NNA4(CLK_NNA_CKGEN_MEM, "nna_ckgen_mem", "mem_466m", 4),
	/* NNA5 */
	GATE_NNA5(CLK_NNA_NNA1_EMI, "nna_nna1_emi", "mem_466m", 0),
	/* NNA6 */
	GATE_NNA6(CLK_NNA_NNA0_AXI, "nna_nna0_axi", "axi_sel", 0),
	/* NNA7 */
	GATE_NNA7(CLK_NNA_NNA1_AXI, "nna_nna1_axi", "axi_sel", 0),
};

static const struct mtk_clk_desc nna_desc = {
	.clks = nna_clks,
	.num_clks = ARRAY_SIZE(nna_clks),
};

static const struct of_device_id of_match_clk_mt8195_nna[] = {
	{
		.compatible = "mediatek,mt8195-nnasys",
		.data = &nna_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8195_nna_drv = {
	.probe = mtk_clk_simple_probe,
	.driver = {
		.name = "clk-mt8195-nna",
		.of_match_table = of_match_clk_mt8195_nna,
	},
};

builtin_platform_driver(clk_mt8195_nna_drv);
