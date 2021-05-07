// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/mtk-mmsys.h>

#include "mtk-mmsys.h"
#include "mt8183-mmsys.h"
#include "mt8195-mmsys.h"

static const struct mtk_mmsys_driver_data mt2701_mmsys_driver_data = {
	.clk_driver = "clk-mt2701-mm",
	.routes = mmsys_default_routing_table,
	.num_routes = ARRAY_SIZE(mmsys_default_routing_table),
};

static const struct mtk_mmsys_driver_data mt2712_mmsys_driver_data = {
	.clk_driver = "clk-mt2712-mm",
	.routes = mmsys_default_routing_table,
	.num_routes = ARRAY_SIZE(mmsys_default_routing_table),
};

static const struct mtk_mmsys_driver_data mt6779_mmsys_driver_data = {
	.clk_driver = "clk-mt6779-mm",
};

static const struct mtk_mmsys_driver_data mt6797_mmsys_driver_data = {
	.clk_driver = "clk-mt6797-mm",
};

static const struct mtk_mmsys_driver_data mt8173_mmsys_driver_data = {
	.clk_driver = "clk-mt8173-mm",
	.routes = mmsys_default_routing_table,
	.num_routes = ARRAY_SIZE(mmsys_default_routing_table),
};

static const struct mtk_mmsys_driver_data mt8183_mmsys_driver_data = {
	.clk_driver = "clk-mt8183-mm",
	.routes = mmsys_mt8183_routing_table,
	.num_routes = ARRAY_SIZE(mmsys_mt8183_routing_table),
};

static const struct mtk_mmsys_driver_data mt8195_mmsys_driver_data = {
	.clk_driver = "clk-mt8195-vdo0",
	.routes = mmsys_mt8195_routing_table,
	.num_routes = ARRAY_SIZE(mmsys_mt8195_routing_table),
	.indep_sub_disp = true,
	.clk_driver1 = "clk-mt8195-vdo1",
	.indep_sub_disp_comp_len = ARRAY_SIZE(mmsys_mt8195_sub_disp_comp),
	.indep_sub_disp_comp = mmsys_mt8195_sub_disp_comp,
};

struct mtk_mmsys {
	void __iomem *regs;
	void __iomem *regs1;
	const struct mtk_mmsys_driver_data *data;
};

static bool mtk_mmsys_comp_in_sub_disp(struct device *dev,
	enum mtk_ddp_comp_id comp_id)
{
	int i;
	struct mtk_mmsys *mmsys = dev_get_drvdata(dev);

	if (mmsys->data->indep_sub_disp_comp_len) {
		for (i = 0; i < mmsys->data->indep_sub_disp_comp_len; i++) {
			if(mmsys->data->indep_sub_disp_comp[i] == comp_id)
				return true;
		}
	}
	return false;
}

static void __iomem * mtk_mmsys_get_comp_config_base(struct device *dev,
                       enum mtk_ddp_comp_id comp_id)
{
	struct mtk_mmsys *mmsys = dev_get_drvdata(dev);

	if (mtk_mmsys_comp_in_sub_disp(dev, comp_id))
		return mmsys->regs1;
	else
		return mmsys->regs;
}

void mtk_mmsys_ddp_connect(struct device *dev,
			   enum mtk_ddp_comp_id cur,
			   enum mtk_ddp_comp_id next)
{
	struct mtk_mmsys *mmsys = dev_get_drvdata(dev);
	const struct mtk_mmsys_routes *routes = mmsys->data->routes;
	void __iomem *reg_base = mtk_mmsys_get_comp_config_base(dev, cur);
	u32 reg;
	int i;

	for (i = 0; i < mmsys->data->num_routes; i++)
		if (cur == routes[i].from_comp && next == routes[i].to_comp) {
			reg = readl_relaxed(reg_base + routes[i].addr) | routes[i].val;
			writel_relaxed(reg, reg_base + routes[i].addr);
		}
}
EXPORT_SYMBOL_GPL(mtk_mmsys_ddp_connect);

void mtk_mmsys_ddp_disconnect(struct device *dev,
			      enum mtk_ddp_comp_id cur,
			      enum mtk_ddp_comp_id next)
{
	struct mtk_mmsys *mmsys = dev_get_drvdata(dev);
	const struct mtk_mmsys_routes *routes = mmsys->data->routes;
	void __iomem *reg_base = mtk_mmsys_get_comp_config_base(dev, cur);
	u32 reg;
	int i;

	for (i = 0; i < mmsys->data->num_routes; i++)
		if (cur == routes[i].from_comp && next == routes[i].to_comp) {
			reg = readl_relaxed(reg_base + routes[i].addr) & ~routes[i].val;
			writel_relaxed(reg, reg_base + routes[i].addr);
		}
}
EXPORT_SYMBOL_GPL(mtk_mmsys_ddp_disconnect);

static int mtk_mmsys_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct platform_device *clks;
	struct platform_device *clks1;
	struct platform_device *drm;
	struct mtk_mmsys *mmsys;
	int ret;

	mmsys = devm_kzalloc(dev, sizeof(*mmsys), GFP_KERNEL);
	if (!mmsys)
		return -ENOMEM;

	mmsys->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mmsys->regs)) {
		ret = PTR_ERR(mmsys->regs);
		dev_err(dev, "Failed to ioremap mmsys registers: %d\n", ret);
		return ret;
	}

	mmsys->data = of_device_get_match_data(&pdev->dev);

	if (mmsys->data->indep_sub_disp) {
		mmsys->regs1 = devm_platform_ioremap_resource(pdev, 1);
		if (IS_ERR(mmsys->regs1)) {
			ret = PTR_ERR(mmsys->regs1);
			dev_err(dev, "Failed to ioremap mmsys registers 1: %d\n", ret);
			return ret;
		}
	}

	platform_set_drvdata(pdev, mmsys);

	clks = platform_device_register_data(&pdev->dev, mmsys->data->clk_driver,
					     PLATFORM_DEVID_AUTO, NULL, 0);
	if (IS_ERR(clks))
		return PTR_ERR(clks);

	if (mmsys->data->indep_sub_disp) {
		clks1 = platform_device_register_data(&pdev->dev,
					mmsys->data->clk_driver1,
					PLATFORM_DEVID_AUTO, NULL, 0);
		if (IS_ERR(clks1))
			return PTR_ERR(clks1);
	}

	drm = platform_device_register_data(&pdev->dev, "mediatek-drm",
					    PLATFORM_DEVID_AUTO, NULL, 0);
	if (IS_ERR(drm)) {
		platform_device_unregister(clks);
		return PTR_ERR(drm);
	}

	return 0;
}

static const struct of_device_id of_match_mtk_mmsys[] = {
	{
		.compatible = "mediatek,mt2701-mmsys",
		.data = &mt2701_mmsys_driver_data,
	},
	{
		.compatible = "mediatek,mt2712-mmsys",
		.data = &mt2712_mmsys_driver_data,
	},
	{
		.compatible = "mediatek,mt6779-mmsys",
		.data = &mt6779_mmsys_driver_data,
	},
	{
		.compatible = "mediatek,mt6797-mmsys",
		.data = &mt6797_mmsys_driver_data,
	},
	{
		.compatible = "mediatek,mt8173-mmsys",
		.data = &mt8173_mmsys_driver_data,
	},
	{
		.compatible = "mediatek,mt8183-mmsys",
		.data = &mt8183_mmsys_driver_data,
	},
	{
		.compatible = "mediatek,mt8195-vdosys",
		.data = &mt8195_mmsys_driver_data,
	},
	{ }
};

static struct platform_driver mtk_mmsys_drv = {
	.driver = {
		.name = "mtk-mmsys",
		.of_match_table = of_match_mtk_mmsys,
	},
	.probe = mtk_mmsys_probe,
};

builtin_platform_driver(mtk_mmsys_drv);
