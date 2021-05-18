// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2020 MediaTek Inc.
// Author: Weiyi Lu <weiyi.lu@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8195-clk.h>

static const struct mtk_gate_regs aud_src_cg_regs = {
	.set_ofs = 0x1004,
	.clr_ofs = 0x1004,
	.sta_ofs = 0x1004,
};

#define GATE_AUD_SRC(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &aud_src_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr)

static const struct mtk_gate aud_src_clks[] = {
	GATE_AUD_SRC(CLK_AUD_SRC_ASRC0, "aud_src_asrc0", "asm_h_sel", 0),
	GATE_AUD_SRC(CLK_AUD_SRC_ASRC1, "aud_src_asrc1", "asm_h_sel", 1),
	GATE_AUD_SRC(CLK_AUD_SRC_ASRC2, "aud_src_asrc2", "asm_h_sel", 2),
	GATE_AUD_SRC(CLK_AUD_SRC_ASRC3, "aud_src_asrc3", "asm_h_sel", 3),
	GATE_AUD_SRC(CLK_AUD_SRC_ASRC4, "aud_src_asrc4", "asm_h_sel", 4),
	GATE_AUD_SRC(CLK_AUD_SRC_ASRC5, "aud_src_asrc5", "asm_h_sel", 5),
	GATE_AUD_SRC(CLK_AUD_SRC_ASRC6, "aud_src_asrc6", "asm_h_sel", 6),
	GATE_AUD_SRC(CLK_AUD_SRC_ASRC7, "aud_src_asrc7", "asm_h_sel", 7),
	GATE_AUD_SRC(CLK_AUD_SRC_ASRC8, "aud_src_asrc8", "asm_h_sel", 8),
	GATE_AUD_SRC(CLK_AUD_SRC_ASRC9, "aud_src_asrc9", "asm_h_sel", 9),
	GATE_AUD_SRC(CLK_AUD_SRC_ASRC10, "aud_src_asrc10", "asm_h_sel", 10),
	GATE_AUD_SRC(CLK_AUD_SRC_ASRC11, "aud_src_asrc11", "asm_h_sel", 11),
};

static const struct mtk_clk_desc aud_src_desc = {
	.clks = aud_src_clks,
	.num_clks = ARRAY_SIZE(aud_src_clks),
};

static const struct of_device_id of_match_clk_mt8195_aud_src[] = {
	{
		.compatible = "mediatek,mt8195-audsys_src",
		.data = &aud_src_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8195_aud_src_drv = {
	.probe = mtk_clk_simple_probe,
	.driver = {
		.name = "clk-mt8195-aud_src",
		.of_match_table = of_match_clk_mt8195_aud_src,
	},
};

builtin_platform_driver(clk_mt8195_aud_src_drv);
