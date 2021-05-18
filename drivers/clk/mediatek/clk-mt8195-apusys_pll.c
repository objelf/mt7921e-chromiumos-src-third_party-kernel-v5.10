// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2020 MediaTek Inc.
// Author: Weiyi Lu <weiyi.lu@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8195-clk.h>

#define MT8195_PLL_FMAX		(3800UL * MHZ)
#define MT8195_PLL_FMIN		(1500UL * MHZ)
#define MT8195_INTEGER_BITS	8

#define PLL(_id, _name, _reg, _pwr_reg, _en_mask, _flags,	\
			_rst_bar_mask, _pcwbits, _pd_reg, _pd_shift,	\
			_tuner_reg, _tuner_en_reg, _tuner_en_bit,	\
			_pcw_reg, _pcw_shift, _pcw_chg_reg,				\
			_en_reg, _pll_en_bit) {					\
		.id = _id,						\
		.name = _name,						\
		.reg = _reg,						\
		.pwr_reg = _pwr_reg,					\
		.en_mask = _en_mask,					\
		.flags = _flags,					\
		.rst_bar_mask = _rst_bar_mask,				\
		.fmax = MT8195_PLL_FMAX,				\
		.fmin = MT8195_PLL_FMIN,				\
		.pcwbits = _pcwbits,					\
		.pcwibits = MT8195_INTEGER_BITS,			\
		.pd_reg = _pd_reg,					\
		.pd_shift = _pd_shift,					\
		.tuner_reg = _tuner_reg,				\
		.tuner_en_reg = _tuner_en_reg,				\
		.tuner_en_bit = _tuner_en_bit,				\
		.pcw_reg = _pcw_reg,					\
		.pcw_shift = _pcw_shift,				\
		.pcw_chg_reg = _pcw_chg_reg,				\
		.en_reg = _en_reg,					\
		.pll_en_bit = _pll_en_bit,				\
	}

static const struct mtk_pll_data apusys_plls[] = {
	PLL(CLK_APUSYS_PLL_APUPLL, "apusys_pll_apupll", 0x008, 0x014, 0,
		0, 0, 22, 0x00c, 24, 0, 0, 0, 0x00c, 0, 0, 0, 0),
	PLL(CLK_APUSYS_PLL_NPUPLL, "apusys_pll_npupll", 0x018, 0x024, 0,
		0, 0, 22, 0x01c, 24, 0, 0, 0, 0x01c, 0, 0, 0, 0),
	PLL(CLK_APUSYS_PLL_APUPLL1, "apusys_pll_apupll1", 0x028, 0x034, 0,
		0, 0, 22, 0x02c, 24, 0, 0, 0, 0x02c, 0, 0, 0, 0),
	PLL(CLK_APUSYS_PLL_APUPLL2, "apusys_pll_apupll2", 0x038, 0x044, 0,
		0, 0, 22, 0x03c, 24, 0, 0, 0, 0x03c, 0, 0, 0, 0),
};

static int clk_mt8195_apusys_pll_probe(struct platform_device *pdev)
{
	struct clk_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_APUSYS_PLL_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	mtk_clk_register_plls(node, apusys_plls, ARRAY_SIZE(apusys_plls), clk_data);

	return of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
}

static const struct of_device_id of_match_clk_mt8195_apusys_pll[] = {
	{ .compatible = "mediatek,mt8195-apusys_pll", },
	{}
};

static struct platform_driver clk_mt8195_apusys_pll_drv = {
	.probe = clk_mt8195_apusys_pll_probe,
	.driver = {
		.name = "clk-mt8195-apusys_pll",
		.of_match_table = of_match_clk_mt8195_apusys_pll,
	},
};

builtin_platform_driver(clk_mt8195_apusys_pll_drv);
