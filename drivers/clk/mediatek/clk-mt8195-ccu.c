// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2020 MediaTek Inc.
// Author: Weiyi Lu <weiyi.lu@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8195-clk.h>

static const struct mtk_gate_regs ccu_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_CCU(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &ccu_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate ccu_clks[] = {
	GATE_CCU(CLK_CCU_LARB18, "ccu_larb18", "ccu_sel", 0),
	GATE_CCU(CLK_CCU_AHB, "ccu_ahb", "ccu_sel", 1),
	GATE_CCU(CLK_CCU_CCU0, "ccu_ccu0", "ccu_sel", 2),
	GATE_CCU(CLK_CCU_CCU1, "ccu_ccu1", "ccu_sel", 3),
};

static const struct mtk_clk_desc ccu_desc = {
	.clks = ccu_clks,
	.num_clks = ARRAY_SIZE(ccu_clks),
};

static const struct of_device_id of_match_clk_mt8195_ccu[] = {
	{
		.compatible = "mediatek,mt8195-ccusys",
		.data = &ccu_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8195_ccu_drv = {
	.probe = mtk_clk_simple_probe,
	.driver = {
		.name = "clk-mt8195-ccu",
		.of_match_table = of_match_clk_mt8195_ccu,
	},
};

builtin_platform_driver(clk_mt8195_ccu_drv);
